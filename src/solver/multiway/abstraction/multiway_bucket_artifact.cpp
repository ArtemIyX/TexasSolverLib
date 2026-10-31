#include "solver/multiway/abstraction/multiway_bucket_artifact.hpp"
#include "multiway_bucket_artifact_detail.hpp"
#include "core/canonical_combo.hpp"
#include "core/fingerprint.hpp"
#include "core/poker.hpp"
#include "solver/multiway/engine/multiway_memory.hpp"

#include <algorithm>
#include <fstream>
#include <limits>
#include <iterator>
#include <stdexcept>
#include <utility>
#include <cstring>
#include <atomic>
#include <thread>
#ifdef _WIN32
	#ifndef NOMINMAX
		#define NOMINMAX
	#endif
	#include <windows.h>
#else
	#include <sys/mman.h>
	#include <sys/stat.h>
	#include <fcntl.h>
	#include <unistd.h>
#endif

namespace texas::solver::multiway
{

	using core::canonical_combos;
	using core::CanonicalComboId;
	using core::is_card_index;
	using core::rank_of;
	using core::Street;
	using core::suit_of;
	namespace
	{

		std::uint32_t mapped_u32(const std::uint8_t* p) noexcept
		{
			return static_cast<std::uint32_t>(p[0]) | (static_cast<std::uint32_t>(p[1]) << 8U) | (static_cast<std::uint32_t>(p[2]) << 16U) | (static_cast<std::uint32_t>(p[3]) << 24U);
		}

		std::uint64_t feature_hash(const MultiwayBucketFeatures& features, Street street) noexcept
		{
			auto hash = texas::core::fingerprint::FNV1A_OFFSET;
			const auto append = [&hash](std::uint64_t value) noexcept {
				texas::core::fingerprint::append_u64(hash, value);
			};
			append(static_cast<std::uint8_t>(street));
			append(features.board_rank_mask);
			append(features.board_suit_mask);
			append(features.board_pair_count);
			append(features.board_size);
			append(features.hole_high_rank);
			append(features.hole_low_rank);
			append(features.hole_suited);
			append(features.hole_pairs_board);
			append(features.hole_suit_matches_board);
			return hash;
		}

		bool request_less(const MultiwayBucketBoardRequest& left, const MultiwayBucketBoardRequest& right)
		{
			if (left.street != right.street)
				return left.street < right.street;
			return left.canonical_board < right.canonical_board;
		}

		bool are_valid_compact_cards(const std::uint8_t* cards, std::size_t count) noexcept
		{
			if (cards == nullptr && count != 0U)
				return false;
			for (std::size_t index = 0U; index < count; ++index)
			{
				if (!is_card_index(cards[index]))
					return false;
				for (std::size_t prior = 0U; prior < index; ++prior)
				{
					if (cards[index] == cards[prior])
						return false;
				}
			}
			return true;
		}

		void validate_mapped_assignments_parallel(const std::vector<MultiwayBucketTable>& tables)
		{
			if (tables.empty())
				return;
			const auto hardware = std::max(1U, std::thread::hardware_concurrency());
			const auto worker_count = std::min<std::size_t>(tables.size(), std::min<std::size_t>(hardware, 16U));
			std::atomic<bool> invalid{ false };
			std::vector<std::thread> workers;
			workers.reserve(worker_count);
			for (std::size_t worker = 0U; worker < worker_count; ++worker)
			{
				workers.emplace_back([&, worker] {
					for (std::size_t index = worker; index < tables.size() && !invalid.load(std::memory_order_relaxed);
						index += worker_count)
					{
						const auto& table = tables[index];
						for (std::size_t assignment = 0U; assignment < table.assignment_count(); ++assignment)
						{
							const auto bucket = table.assignment_at(assignment);
							const auto& hole = canonical_combos().cards(static_cast<CanonicalComboId>(assignment));
							bool blocked = false;
							for (const auto card : table.canonical_board())
								blocked = blocked || card == hole[0] || card == hole[1];
							if ((blocked && bucket != MULTIWAY_INVALID_BUCKET) || (!blocked && (bucket == MULTIWAY_INVALID_BUCKET || bucket >= table.bucket_count())))
							{
								invalid.store(true, std::memory_order_relaxed);
								return;
							}
						}
					}
				});
			}
			for (auto& worker : workers)
				worker.join();
			if (invalid.load(std::memory_order_relaxed))
			{
				throw std::invalid_argument("multiway bucket artifact contains an out-of-range assignment");
			}
		}

	} // namespace

	MultiwayBucketBaselineProfile MultiwayBucketBaselineProfile::standard() noexcept
	{
		return {};
	}

	void MultiwayBucketBaselineProfile::validate() const
	{
		if (schema_version != MULTIWAY_BUCKET_ARTIFACT_SCHEMA_VERSION || feature_version == 0U || flop_bucket_count == 0U || turn_bucket_count == 0U || river_bucket_count == 0U)
		{
			throw std::invalid_argument("multiway bucket baseline profile is invalid");
		}
	}

	std::uint32_t MultiwayBucketBaselineProfile::bucket_count(Street street) const
	{
		validate();
		switch (street)
		{
			case Street::Flop:
				return flop_bucket_count;
			case Street::Turn:
				return turn_bucket_count;
			case Street::River:
				return river_bucket_count;
			default:
				throw std::invalid_argument("multiway buckets require a postflop street");
		}
	}

	bool is_multiway_canonical_board(Street street, const std::vector<std::uint8_t>& board) noexcept
	{
		const auto expected_size = bucket_artifact_detail::board_size_for(street);
		if (expected_size == 0U || board.size() != expected_size || !are_valid_compact_cards(board.data(), board.size()))
		{
			return false;
		}
		return std::is_sorted(board.begin(), board.end());
	}

	bool is_multiway_canonical_board(Street street, MultiwayBoardView board) noexcept
	{
		const auto expected_size = bucket_artifact_detail::board_size_for(street);
		if (expected_size == 0U || board.size() != expected_size)
			return false;
		std::uint8_t prior = 0U;
		for (std::size_t index = 0U; index < board.size(); ++index)
		{
			const auto card = board[index];
			if (!is_card_index(card) || (index != 0U && card < prior))
				return false;
			for (std::size_t before = 0U; before < index; ++before)
				if (board[before] == card)
					return false;
			prior = card;
		}
		return true;
	}

	MultiwayBucketFeatures make_multiway_bucket_features(
		Street street,
		const std::vector<std::uint8_t>& canonical_board,
		const std::array<std::uint8_t, 2>& hole)
	{
		if (!is_multiway_canonical_board(street, canonical_board) || !are_valid_compact_cards(hole.data(), hole.size()) || std::find(canonical_board.begin(), canonical_board.end(), hole[0]) != canonical_board.end() || std::find(canonical_board.begin(), canonical_board.end(), hole[1]) != canonical_board.end())
		{
			throw std::invalid_argument("multiway bucket features require a live hole pair and canonical board");
		}

		MultiwayBucketFeatures features;
		features.board_size = static_cast<std::uint8_t>(canonical_board.size());
		std::array<std::uint8_t, 15U> rank_counts = {};
		for (const auto card : canonical_board)
		{
			const auto rank = rank_of(card);
			const auto suit = suit_of(card);
			features.board_rank_mask |= static_cast<std::uint16_t>(1U << rank);
			features.board_suit_mask |= static_cast<std::uint8_t>(1U << suit);
			++rank_counts[rank];
			if (rank == rank_of(hole[0]) || rank == rank_of(hole[1]))
				++features.hole_pairs_board;
			if (suit == suit_of(hole[0]))
				++features.hole_suit_matches_board;
			if (suit == suit_of(hole[1]))
				++features.hole_suit_matches_board;
		}
		for (const auto count : rank_counts)
		{
			if (count >= 2U)
				++features.board_pair_count;
		}
		features.hole_high_rank = std::max(rank_of(hole[0]), rank_of(hole[1]));
		features.hole_low_rank = std::min(rank_of(hole[0]), rank_of(hole[1]));
		features.hole_suited = suit_of(hole[0]) == suit_of(hole[1]) ? 1U : 0U;
		return features;
	}

	std::uint32_t assign_multiway_baseline_bucket(
		const MultiwayBucketFeatures& features,
		const MultiwayBucketBaselineProfile& profile,
		Street street)
	{
		const auto count = profile.bucket_count(street);
		const auto versioned_hash = feature_hash(features, street) ^ (profile.feature_version * 0x9e3779b97f4a7c15ULL);
		return static_cast<std::uint32_t>(versioned_hash % count);
	}

	MultiwayBucketTable build_multiway_baseline_bucket_table(
		const MultiwayModelIdentity& identity,
		Street street,
		std::vector<std::uint8_t> canonical_board,
		const MultiwayBucketBaselineProfile& profile)
	{
		identity.validate();
		profile.validate();
		if (!is_multiway_canonical_board(street, canonical_board))
		{
			throw std::invalid_argument("multiway bucket builder requires a canonical sorted board");
		}
		const auto count = profile.bucket_count(street);
		std::vector<std::uint32_t> assignments(MULTIWAY_HOLE_COMBINATION_COUNT, MULTIWAY_INVALID_BUCKET);
		for (CanonicalComboId id = 0U; id < MULTIWAY_HOLE_COMBINATION_COUNT; ++id)
		{
			const auto& hole = canonical_combos().cards(id);
			if (std::find(canonical_board.begin(), canonical_board.end(), hole[0]) != canonical_board.end() || std::find(canonical_board.begin(), canonical_board.end(), hole[1]) != canonical_board.end())
			{
				continue;
			}
			assignments[id] = assign_multiway_baseline_bucket(
				make_multiway_bucket_features(street, canonical_board, hole), profile, street);
		}
		return { identity, street, std::move(canonical_board), count, std::move(assignments) };
	}

	MultiwayBucketTable build_multiway_baseline_bucket_table_fixed_board(
		const MultiwayModelIdentity& identity, Street street,
		const std::array<std::uint8_t, 5U>& canonical_board,
		const MultiwayBucketBaselineProfile& profile)
	{
		identity.validate();
		profile.validate();
		const auto board_size = street == Street::Flop ? 3U : street == Street::Turn ? 4U
			: street == Street::River												 ? 5U
																					 : 0U;
		if (board_size == 0U || !std::is_sorted(canonical_board.begin(), canonical_board.begin() + board_size) || !are_valid_compact_cards(canonical_board.data(), board_size))
		{
			throw std::invalid_argument("multiway bucket builder requires a canonical sorted board");
		}
		const auto count = profile.bucket_count(street);
		std::vector<std::uint32_t> assignments(MULTIWAY_HOLE_COMBINATION_COUNT, MULTIWAY_INVALID_BUCKET);
		MultiwayBucketFeatures features;
		features.board_size = static_cast<std::uint8_t>(board_size);
		std::array<std::uint8_t, 15U> rank_counts{};
		for (std::size_t index = 0U; index < board_size; ++index)
		{
			const auto card = canonical_board[index];
			const auto rank = rank_of(card);
			const auto suit = suit_of(card);
			features.board_rank_mask |= static_cast<std::uint16_t>(1U << rank);
			features.board_suit_mask |= static_cast<std::uint8_t>(1U << suit);
			++rank_counts[rank];
		}
		for (const auto count_at_rank : rank_counts)
			if (count_at_rank >= 2U)
				++features.board_pair_count;
		for (CanonicalComboId id = 0U; id < MULTIWAY_HOLE_COMBINATION_COUNT; ++id)
		{
			const auto& hole = canonical_combos().cards(id);
			bool blocked = false;
			for (std::size_t board_index = 0U; board_index < board_size; ++board_index)
			{
				blocked = blocked || canonical_board[board_index] == hole[0] || canonical_board[board_index] == hole[1];
			}
			if (blocked)
				continue;
			features.hole_pairs_board = 0U;
			features.hole_suit_matches_board = 0U;
			const auto first_rank = rank_of(hole[0]);
			const auto second_rank = rank_of(hole[1]);
			const auto first_suit = suit_of(hole[0]);
			const auto second_suit = suit_of(hole[1]);
			for (std::size_t board_index = 0U; board_index < board_size; ++board_index)
			{
				const auto card = canonical_board[board_index];
				if (rank_of(card) == first_rank || rank_of(card) == second_rank)
					++features.hole_pairs_board;
				if (suit_of(card) == first_suit)
					++features.hole_suit_matches_board;
				if (suit_of(card) == second_suit)
					++features.hole_suit_matches_board;
			}
			features.hole_high_rank = std::max(first_rank, second_rank);
			features.hole_low_rank = std::min(first_rank, second_rank);
			features.hole_suited = first_suit == second_suit ? 1U : 0U;
			assignments[id] = assign_multiway_baseline_bucket(features, profile, street);
		}
		std::vector<std::uint8_t> board(canonical_board.begin(), canonical_board.begin() + board_size);
		return { identity, street, std::move(board), count, std::move(assignments) };
	}

	MultiwayBucketRegistry build_multiway_baseline_bucket_registry(
		const MultiwayModelIdentity& identity,
		const std::vector<MultiwayBucketBoardRequest>& boards,
		const MultiwayBucketBaselineProfile& profile)
	{
		profile.validate();
		if (boards.empty())
			throw std::invalid_argument("multiway bucket builder requires boards");
		std::vector<MultiwayBucketTable> tables;
		tables.reserve(boards.size());
		for (const auto& board : boards)
		{
			tables.push_back(build_multiway_baseline_bucket_table(
				identity, board.street, board.canonical_board, profile));
		}
		return MultiwayBucketRegistry(std::move(tables));
	}

	void validate_multiway_bucket_coverage(
		const MultiwayBucketRegistry& registry,
		const std::vector<MultiwayBucketBoardRequest>& required_boards)
	{
		if (required_boards.empty())
			throw std::invalid_argument("multiway bucket coverage requires boards");
		std::vector<MultiwayBucketBoardRequest> ordered = required_boards;
		std::sort(ordered.begin(), ordered.end(), request_less);
		for (std::size_t index = 0U; index < ordered.size(); ++index)
		{
			const auto& board = ordered[index];
			if (!is_multiway_canonical_board(board.street, board.canonical_board))
			{
				throw std::invalid_argument("multiway bucket coverage requires canonical boards");
			}
			if (index != 0U && !request_less(ordered[index - 1U], board))
			{
				throw std::invalid_argument("multiway bucket coverage has duplicate board requests");
			}
			const auto& table = registry.table(board.street, board.canonical_board);
			if (table.bucket_count() == 0U)
				throw std::logic_error("multiway bucket coverage found empty table");
		}
	}

	void validate_multiway_bucket_registry_profile(
		const MultiwayBucketRegistry& registry,
		const MultiwayModelIdentity& expected_identity,
		const MultiwayBucketBaselineProfile& expected_profile)
	{
		expected_identity.validate();
		expected_profile.validate();
		if (registry.identity() != expected_identity)
			throw std::invalid_argument("multiway bucket artifact model identity mismatch");
		for (const auto& table : registry.tables())
		{
			if (table.identity() != expected_identity || table.bucket_count() != expected_profile.bucket_count(table.street()))
				throw std::invalid_argument("multiway bucket artifact profile mismatch");
		}
	}

	MultiwayBucketRegistry::MappedArtifact::~MappedArtifact()
	{
#ifdef _WIN32
		if (data)
			UnmapViewOfFile(data);
		if (mapping)
			CloseHandle(mapping);
		if (file != -1)
			CloseHandle(reinterpret_cast<HANDLE>(file));
#else
		if (data)
			munmap(const_cast<std::uint8_t*>(data), size);
		if (file >= 0)
			close(static_cast<int>(file));
#endif
	}

	MultiwayBucketRegistry load_multiway_bucket_registry(
		const std::filesystem::path& path,
		MultiwayProcessMemorySnapshotCollection* process_memory_snapshots)
	{
		constexpr std::uint64_t kV4HeaderBytes = 4U + 4U + bucket_artifact_detail::kIdentityFieldCount * 8U + 4U;
		constexpr std::uint64_t kHeaderBytes = 4U + 4U + bucket_artifact_detail::kIdentityFieldCount * 8U + 4U + 4U + 6U * 8U;
		constexpr std::uint64_t kSmallestTableBytes = 2U + 3U + 4U + MULTIWAY_HOLE_COMBINATION_COUNT * 4U;
		std::error_code error;
		const auto bytes = std::filesystem::file_size(path, error);
		if (error || bytes < kV4HeaderBytes)
			throw std::invalid_argument("multiway bucket artifact is truncated");
		auto mapped = std::make_unique<MultiwayBucketRegistry::MappedArtifact>();
#ifdef _WIN32
		mapped->file = reinterpret_cast<std::intptr_t>(CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr));
		if (mapped->file == reinterpret_cast<std::intptr_t>(INVALID_HANDLE_VALUE))
			throw std::runtime_error("multiway bucket artifact cannot be opened");
		mapped->mapping = CreateFileMappingW(reinterpret_cast<HANDLE>(mapped->file), nullptr, PAGE_READONLY, 0, 0, nullptr);
		mapped->data = static_cast<const std::uint8_t*>(MapViewOfFile(mapped->mapping, FILE_MAP_READ, 0, 0, 0));
#else
		mapped->file = open(path.c_str(), O_RDONLY);
		if (mapped->file < 0)
			throw std::runtime_error("multiway bucket artifact cannot be opened");
		mapped->data = static_cast<const std::uint8_t*>(mmap(nullptr, static_cast<std::size_t>(bytes), PROT_READ, MAP_PRIVATE, mapped->file, 0));
		if (mapped->data == MAP_FAILED)
			mapped->data = nullptr;
#endif
		mapped->size = static_cast<std::size_t>(bytes);
		if (mapped->data == nullptr)
			throw std::runtime_error("cannot map multiway bucket artifact");
		const auto* input = mapped->data;
		if (std::memcmp(input, bucket_artifact_detail::kMagic, 4U) != 0)
		{
			throw std::invalid_argument("multiway bucket artifact has invalid header");
		}
		std::size_t cursor = 4U;
		const auto read_byte = [&]() { if (cursor >= mapped->size) throw std::invalid_argument("multiway bucket artifact is truncated"); return input[cursor++]; };
		const auto read32 = [&]() { if (mapped->size - cursor < 4U) throw std::invalid_argument("multiway bucket artifact is truncated"); const auto v = mapped_u32(input + cursor); cursor += 4U; return v; };
		const auto read64 = [&]() { if (mapped->size - cursor < 8U) throw std::invalid_argument("multiway bucket artifact is truncated"); std::uint64_t v=0; for (int i=0;i<8;++i) v |= static_cast<std::uint64_t>(input[cursor++]) << (i*8); return v; };
		const auto schema = read32();
		if (schema != MULTIWAY_BUCKET_ARTIFACT_SCHEMA_VERSION && schema != MULTIWAY_BUCKET_ARTIFACT_SCHEMA_V4)
			throw std::invalid_argument("multiway bucket artifact schema is unsupported");
		MultiwayModelIdentity identity;
		visit_multiway_model_identity_fields(identity, [&read64](std::uint64_t& field) { field = read64(); });
		identity.validate();
		const auto table_count = read32();
		if (table_count == 0U || (schema == MULTIWAY_BUCKET_ARTIFACT_SCHEMA_V4 && table_count > (bytes - kV4HeaderBytes) / kSmallestTableBytes))
		{
			throw std::invalid_argument("multiway bucket artifact table count is invalid");
		}
		std::uint32_t assignment_width = 4U;
		std::uint64_t directory_offset = 0U, directory_length = 0U, assignment_offset = 0U, assignment_length = 0U;
		if (schema == MULTIWAY_BUCKET_ARTIFACT_SCHEMA_VERSION)
		{
			assignment_width = read32();
			directory_offset = read64();
			directory_length = read64();
			assignment_offset = read64();
			assignment_length = read64();
			const auto directory_hash = read64();
			const auto assignment_hash = read64();
			if ((assignment_width != 1U && assignment_width != 2U && assignment_width != 4U) || directory_offset != kHeaderBytes || directory_length != static_cast<std::uint64_t>(table_count) * 32U || assignment_offset != directory_offset + directory_length || assignment_offset > bytes || assignment_length != bytes - assignment_offset || assignment_length != static_cast<std::uint64_t>(table_count) * MULTIWAY_HOLE_COMBINATION_COUNT * assignment_width)
				throw std::invalid_argument("multiway bucket artifact has invalid section bounds");
			auto hash_range = [&](std::uint64_t offset, std::uint64_t length) {
				auto hash = texas::core::fingerprint::FNV1A_OFFSET;
				for (std::uint64_t i = 0U; i < length; ++i)
					texas::core::fingerprint::append_u8(hash, input[offset + i]);
				return hash;
			};
			if (hash_range(directory_offset, directory_length) != directory_hash || hash_range(assignment_offset, assignment_length) != assignment_hash)
				throw std::invalid_argument("multiway bucket artifact section hash mismatch");
			cursor = static_cast<std::size_t>(directory_offset);
		}
		std::vector<MultiwayBucketTable> tables;
		if (schema == MULTIWAY_BUCKET_ARTIFACT_SCHEMA_VERSION)
		{
			constexpr std::uint64_t kMappedMetadataLimit = 128ULL * 1024ULL * 1024ULL;
			const auto available = kMappedMetadataLimit - sizeof(MultiwayModelIdentity);
			if (table_count > available / sizeof(MultiwayBucketTable))
				throw std::invalid_argument("multiway bucket metadata exceeds the 128 MiB limit");
		}
		tables.reserve(table_count);
		if (schema == MULTIWAY_BUCKET_ARTIFACT_SCHEMA_VERSION && static_cast<std::uint64_t>(tables.capacity()) * sizeof(MultiwayBucketTable) + sizeof(MultiwayModelIdentity) > 128ULL * 1024ULL * 1024ULL)
			throw std::invalid_argument("multiway bucket metadata exceeds the 128 MiB limit");
		for (std::uint32_t index = 0U; index < table_count; ++index)
		{
			Street street;
			std::size_t board_size;
			std::uint32_t bucket_count;
			std::uint64_t expected_table_identity = 0U;
			if (schema == MULTIWAY_BUCKET_ARTIFACT_SCHEMA_VERSION)
			{
				street = static_cast<Street>(read_byte());
				board_size = read_byte();
				const auto reserved_a = read_byte();
				const auto reserved_b = read_byte();
				const auto board_key = read64();
				bucket_count = read32();
				const auto offset = read64();
				expected_table_identity = read64();
				const auto expected_offset = assignment_offset + static_cast<std::uint64_t>(index) * MULTIWAY_HOLE_COMBINATION_COUNT * assignment_width;
				if (board_size != bucket_artifact_detail::board_size_for(street) || reserved_a != 0U || reserved_b != 0U || (board_key >> (board_size * 6U)) != 0U || bucket_count == 0U || bucket_artifact_detail::bucket_assignment_width(bucket_count) > assignment_width || offset != expected_offset || offset > assignment_offset + assignment_length - MULTIWAY_HOLE_COMBINATION_COUNT * assignment_width)
					throw std::invalid_argument("multiway bucket artifact has invalid table directory entry");
				MultiwayBucketTable table;
				table.mapped_directory_entry_ = input + cursor - 32U;
				table.mapped_artifact_data_ = input;
				table.mapped_assignment_width_ = static_cast<std::uint8_t>(assignment_width);
				table.mapped_schema5_ = true;
				const auto board = table.canonical_board();
				if (!is_multiway_canonical_board(street, board))
					throw std::invalid_argument("multiway bucket artifact board is not canonical");
				auto table_hash = texas::core::fingerprint::FNV1A_OFFSET;
				texas::core::fingerprint::append_u64(table_hash, identity.combined_hash);
				texas::core::fingerprint::append_u64(table_hash, static_cast<std::uint8_t>(street));
				texas::core::fingerprint::append_u64(table_hash, board.size());
				for (const auto card : board)
					texas::core::fingerprint::append_u64(table_hash, card);
				texas::core::fingerprint::append_u64(table_hash, bucket_count);
				for (std::size_t assignment = 0U; assignment < MULTIWAY_HOLE_COMBINATION_COUNT; ++assignment)
					texas::core::fingerprint::append_u64(table_hash, table.assignment_at(assignment));
				if (table_hash != expected_table_identity)
					throw std::invalid_argument("multiway bucket artifact table identity mismatch");
				if (!tables.empty() && !(tables.back().street() < street || (tables.back().street() == street && tables.back().canonical_board() < table.canonical_board())))
					throw std::invalid_argument("multiway bucket artifact directory is not uniquely sorted");
				tables.push_back(std::move(table));
				continue;
			}
			const auto record_start = cursor;
			street = static_cast<Street>(read_byte());
			board_size = static_cast<std::size_t>(read_byte());
			if (board_size != bucket_artifact_detail::board_size_for(street))
			{
				throw std::invalid_argument("multiway bucket artifact has invalid board metadata");
			}
			std::vector<std::uint8_t> board(board_size);
			for (auto& card : board)
				card = read_byte();
			bucket_count = read32();
			if (mapped->size - cursor < MULTIWAY_HOLE_COMBINATION_COUNT * 4U)
				throw std::invalid_argument("multiway bucket artifact is truncated");
			cursor += MULTIWAY_HOLE_COMBINATION_COUNT * 4U;
			if (!is_multiway_canonical_board(street, board))
			{
				throw std::invalid_argument("multiway bucket artifact board is not canonical");
			}
			if (!is_multiway_canonical_board(street, board) || bucket_count == 0U)
				throw std::invalid_argument("multiway bucket artifact has invalid table");
			MultiwayBucketTable table;
			table.owned_ = std::make_unique<MultiwayBucketTable::OwnedData>();
			table.owned_->identity = identity;
			table.owned_->street = street;
			table.owned_->board = board;
			table.owned_->bucket_count = bucket_count;
			table.mapped_directory_entry_ = input + record_start;
			auto table_hash = texas::core::fingerprint::FNV1A_OFFSET;
			texas::core::fingerprint::append_u64(table_hash, identity.combined_hash);
			texas::core::fingerprint::append_u64(table_hash, static_cast<std::uint8_t>(street));
			texas::core::fingerprint::append_u64(table_hash, board.size());
			for (const auto card : board)
				texas::core::fingerprint::append_u64(table_hash, card);
			texas::core::fingerprint::append_u64(table_hash, bucket_count);
			table.owned_->table_identity = table_hash;
			tables.push_back(std::move(table));
		}
		if (process_memory_snapshots != nullptr && !process_memory_snapshots->record_current(MultiwayMemoryPhase::AfterBucketMapMetadata))
			throw std::logic_error("multiway bucket memory snapshot phase order is invalid");
		validate_mapped_assignments_parallel(tables);
		if (process_memory_snapshots != nullptr && !process_memory_snapshots->record_current(MultiwayMemoryPhase::AfterBucketAssignmentValidation))
			throw std::logic_error("multiway bucket memory snapshot phase order is invalid");
		if (schema == MULTIWAY_BUCKET_ARTIFACT_SCHEMA_VERSION)
		{
			std::uint32_t maximum_bucket_count = 0U;
			for (const auto& table : tables)
				maximum_bucket_count = std::max(maximum_bucket_count, table.bucket_count());
			if (bucket_artifact_detail::bucket_assignment_width(maximum_bucket_count) != assignment_width)
				throw std::invalid_argument("multiway bucket artifact assignment width is not minimal");
			cursor = mapped->size;
		}
		if (cursor != mapped->size)
			throw std::invalid_argument("multiway bucket artifact has trailing data");
		MultiwayBucketRegistry registry(std::move(tables), std::move(mapped), identity);
		if (schema == MULTIWAY_BUCKET_ARTIFACT_SCHEMA_VERSION)
		{
			registry.mapped_directory_ = input + directory_offset;
			registry.mapped_directory_count_ = table_count;
		}
		return registry;
	}

} // namespace texas::solver::multiway
