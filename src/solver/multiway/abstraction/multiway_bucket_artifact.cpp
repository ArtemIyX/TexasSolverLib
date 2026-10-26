#include "solver/multiway/abstraction/multiway_bucket_artifact.hpp"
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

		constexpr std::uint8_t kMagic[] = { 'M', 'W', 'B', 'K' };
		constexpr std::size_t kIdentityFieldCount = 13U;

		std::uint32_t bucket_assignment_width(std::uint32_t bucket_count) noexcept
		{
			return bucket_count <= 0xffU ? 1U : bucket_count <= 0xffffU ? 2U : 4U;
		}

		std::size_t board_size_for(Street street) noexcept
		{
			switch (street)
			{
				case Street::Flop:
					return 3U;
				case Street::Turn:
					return 4U;
				case Street::River:
					return 5U;
				default:
					return 0U;
			}
		}

		void append_u32(std::vector<std::uint8_t>& output, std::uint32_t value)
		{
			for (std::uint8_t index = 0U; index < 4U; ++index)
			{
				output.push_back(static_cast<std::uint8_t>((value >> (index * 8U)) & 0xffU));
			}
		}

		void append_u64(std::vector<std::uint8_t>& output, std::uint64_t value)
		{
			for (std::uint8_t index = 0U; index < 8U; ++index)
			{
				output.push_back(static_cast<std::uint8_t>((value >> (index * 8U)) & 0xffU));
			}
		}

		std::uint32_t read_u32(const std::vector<std::uint8_t>& input, std::size_t& cursor)
		{
			if (input.size() - cursor < 4U)
				throw std::invalid_argument("multiway bucket artifact is truncated");
			std::uint32_t value = 0U;
			for (std::uint8_t index = 0U; index < 4U; ++index)
			{
				value |= static_cast<std::uint32_t>(input[cursor++]) << (index * 8U);
			}
			return value;
		}

		std::uint64_t read_u64(const std::vector<std::uint8_t>& input, std::size_t& cursor)
		{
			if (input.size() - cursor < 8U)
				throw std::invalid_argument("multiway bucket artifact is truncated");
			std::uint64_t value = 0U;
			for (std::uint8_t index = 0U; index < 8U; ++index)
			{
				value |= static_cast<std::uint64_t>(input[cursor++]) << (index * 8U);
			}
			return value;
		}

		void append_identity(std::vector<std::uint8_t>& output, const MultiwayModelIdentity& identity)
		{
			visit_multiway_model_identity_fields(identity, [&](std::uint64_t field) {
				append_u64(output, field);
			});
		}

		MultiwayModelIdentity read_identity(const std::vector<std::uint8_t>& input, std::size_t& cursor)
		{
			MultiwayModelIdentity identity;
			visit_multiway_model_identity_fields(identity, [&](std::uint64_t& field) {
				field = read_u64(input, cursor);
			});
			identity.validate();
			return identity;
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
							if (bucket != MULTIWAY_INVALID_BUCKET && bucket >= table.bucket_count())
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
		const auto expected_size = board_size_for(street);
		if (expected_size == 0U || board.size() != expected_size || !are_valid_compact_cards(board.data(), board.size()))
		{
			return false;
		}
		return std::is_sorted(board.begin(), board.end());
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

	std::vector<std::uint8_t> serialize_multiway_bucket_registry(const MultiwayBucketRegistry& registry)
	{
		registry.identity().validate();
		const auto& tables = registry.tables();
		if (tables.size() > std::numeric_limits<std::uint32_t>::max())
			throw std::overflow_error("multiway bucket artifact table count overflows format");
		std::uint32_t maximum_bucket_count = 0U;
		for (const auto& table : tables)
			maximum_bucket_count = std::max(maximum_bucket_count, table.bucket_count());
		const std::uint32_t width = bucket_assignment_width(maximum_bucket_count);
		constexpr std::size_t kHeaderBytes = 4U + 4U + kIdentityFieldCount * 8U + 4U + 4U + 6U * 8U;
		constexpr std::size_t kDirectoryEntryBytes = 32U;
		const auto directory_bytes = tables.size() * kDirectoryEntryBytes;
		if (tables.size() != 0U && directory_bytes / kDirectoryEntryBytes != tables.size())
			throw std::overflow_error("multiway bucket directory size overflows address space");
		const auto assignment_offset = kHeaderBytes + directory_bytes;
		if (tables.size() > (std::numeric_limits<std::size_t>::max() - assignment_offset) /
			(MULTIWAY_HOLE_COMBINATION_COUNT * width))
			throw std::overflow_error("multiway bucket payload size overflows address space");
		std::vector<std::uint8_t> directory;
		directory.reserve(directory_bytes);
		std::vector<std::uint8_t> assignments;
		assignments.reserve(tables.size() * MULTIWAY_HOLE_COMBINATION_COUNT * width);
		for (const auto& table : tables)
		{
			const auto board_size = table.canonical_board().size();
			if (board_size > 5U)
				throw std::invalid_argument("multiway bucket board is too large");
			directory.push_back(static_cast<std::uint8_t>(table.street()));
			directory.push_back(static_cast<std::uint8_t>(board_size));
			directory.push_back(0U);
			directory.push_back(0U);
			std::uint64_t board_key = 0U;
			for (std::size_t card = 0U; card < board_size; ++card)
				board_key |= static_cast<std::uint64_t>(table.canonical_board()[card]) << (card * 6U);
			append_u64(directory, board_key);
			append_u32(directory, table.bucket_count());
			append_u64(directory, assignment_offset + assignments.size());
			append_u64(directory, table.table_identity());
			for (std::size_t index = 0U; index < MULTIWAY_HOLE_COMBINATION_COUNT; ++index)
			{
				auto value = table.assignment_at(index);
				if (value == MULTIWAY_INVALID_BUCKET && width < 4U)
					value = (1U << (width * 8U)) - 1U;
				for (std::uint32_t byte = 0U; byte < width; ++byte)
					assignments.push_back(static_cast<std::uint8_t>(value >> (byte * 8U)));
			}
		}
		auto section_hash = [](const std::vector<std::uint8_t>& section) {
			auto hash = texas::core::fingerprint::FNV1A_OFFSET;
			for (const auto byte : section) texas::core::fingerprint::append_u8(hash, byte);
			return hash;
		};
		std::vector<std::uint8_t> output;
		output.reserve(assignment_offset + assignments.size());
		output.insert(output.end(), std::begin(kMagic), std::end(kMagic));
		append_u32(output, MULTIWAY_BUCKET_ARTIFACT_SCHEMA_VERSION);
		append_identity(output, registry.identity());
		append_u32(output, static_cast<std::uint32_t>(tables.size()));
		append_u32(output, width);
		append_u64(output, kHeaderBytes);
		append_u64(output, directory.size());
		append_u64(output, assignment_offset);
		append_u64(output, assignments.size());
		append_u64(output, section_hash(directory));
		append_u64(output, section_hash(assignments));
		output.insert(output.end(), directory.begin(), directory.end());
		output.insert(output.end(), assignments.begin(), assignments.end());
		return output;
	}

	MultiwayBucketRegistry deserialize_multiway_bucket_registry(const std::vector<std::uint8_t>& bytes)
	{
		constexpr std::size_t kMinimumSize = 4U + 4U + kIdentityFieldCount * 8U + 4U;
		if (bytes.size() < kMinimumSize || !std::equal(std::begin(kMagic), std::end(kMagic), bytes.begin()))
		{
			throw std::invalid_argument("multiway bucket artifact has invalid header");
		}
		std::size_t cursor = 4U;
		const auto schema = read_u32(bytes, cursor);
		if (schema == MULTIWAY_BUCKET_ARTIFACT_SCHEMA_VERSION)
		{
			const auto identity = read_identity(bytes, cursor);
			const auto count = read_u32(bytes, cursor);
			const auto width = read_u32(bytes, cursor);
			const auto directory_offset = read_u64(bytes, cursor);
			const auto directory_length = read_u64(bytes, cursor);
			const auto assignment_offset = read_u64(bytes, cursor);
			const auto assignment_length = read_u64(bytes, cursor);
			const auto directory_hash = read_u64(bytes, cursor);
			const auto assignment_hash = read_u64(bytes, cursor);
			constexpr std::size_t header_size = 4U + 4U + kIdentityFieldCount * 8U + 4U + 4U + 6U * 8U;
			constexpr std::size_t entry_size = 32U;
			if (count == 0U || (width != 1U && width != 2U && width != 4U) || directory_offset != header_size ||
				directory_length != static_cast<std::uint64_t>(count) * entry_size ||
				assignment_offset != directory_offset + directory_length || assignment_offset > bytes.size() ||
				assignment_length != bytes.size() - static_cast<std::size_t>(assignment_offset) ||
				assignment_length != static_cast<std::uint64_t>(count) * MULTIWAY_HOLE_COMBINATION_COUNT * width)
				throw std::invalid_argument("multiway bucket artifact has invalid section bounds");
			auto hash_section = [](const std::uint8_t* data, std::size_t size) {
				auto hash = texas::core::fingerprint::FNV1A_OFFSET;
				for (std::size_t index = 0U; index < size; ++index) texas::core::fingerprint::append_u8(hash, data[index]);
				return hash;
			};
			if (hash_section(bytes.data() + directory_offset, static_cast<std::size_t>(directory_length)) != directory_hash ||
				hash_section(bytes.data() + assignment_offset, static_cast<std::size_t>(assignment_length)) != assignment_hash)
				throw std::invalid_argument("multiway bucket artifact section hash mismatch");
			std::vector<MultiwayBucketTable> tables;
			tables.reserve(count);
			std::size_t directory_cursor = static_cast<std::size_t>(directory_offset);
			for (std::uint32_t table_index = 0U; table_index < count; ++table_index)
			{
				const auto street = static_cast<Street>(bytes[directory_cursor++]);
				const auto board_size = bytes[directory_cursor++];
				const auto reserved_a = bytes[directory_cursor++];
				const auto reserved_b = bytes[directory_cursor++];
				const auto board_key = read_u64(bytes, directory_cursor);
				const auto bucket_count = read_u32(bytes, directory_cursor);
				const auto offset = read_u64(bytes, directory_cursor);
				const auto expected_table_identity = read_u64(bytes, directory_cursor);
				const auto expected_offset = assignment_offset + static_cast<std::uint64_t>(table_index) * MULTIWAY_HOLE_COMBINATION_COUNT * width;
				if (board_size != board_size_for(street) || reserved_a != 0U || reserved_b != 0U || (board_key >> (board_size * 6U)) != 0U ||
					bucket_count == 0U || bucket_assignment_width(bucket_count) > width || offset != expected_offset ||
					offset > assignment_offset + assignment_length - MULTIWAY_HOLE_COMBINATION_COUNT * width)
					throw std::invalid_argument("multiway bucket artifact has invalid table directory entry");
				std::vector<std::uint8_t> board(board_size);
				for (std::size_t card = 0U; card < board_size; ++card) board[card] = static_cast<std::uint8_t>((board_key >> (card * 6U)) & 0x3fU);
				if (!is_multiway_canonical_board(street, board))
					throw std::invalid_argument("multiway bucket artifact board is not canonical");
				std::vector<std::uint32_t> assignments;
				assignments.reserve(MULTIWAY_HOLE_COMBINATION_COUNT);
				std::size_t assignment_cursor = static_cast<std::size_t>(offset);
				for (std::size_t index = 0U; index < MULTIWAY_HOLE_COMBINATION_COUNT; ++index)
				{
					std::uint32_t value = 0U;
					for (std::uint32_t byte = 0U; byte < width; ++byte)
						value |= static_cast<std::uint32_t>(bytes[assignment_cursor++]) << (byte * 8U);
					if (width < 4U && value == ((1U << (width * 8U)) - 1U)) value = MULTIWAY_INVALID_BUCKET;
					assignments.push_back(value);
				}
				tables.emplace_back(identity, street, std::move(board), bucket_count, std::move(assignments));
				if (tables.back().table_identity() != expected_table_identity)
					throw std::invalid_argument("multiway bucket artifact table identity mismatch");
				if (table_index != 0U && !(tables[table_index - 1U].street() < street ||
					(tables[table_index - 1U].street() == street && tables[table_index - 1U].canonical_board() < tables.back().canonical_board())))
					throw std::invalid_argument("multiway bucket artifact directory is not uniquely sorted");
			}
			std::uint32_t maximum_bucket_count = 0U;
			for (const auto& table : tables) maximum_bucket_count = std::max(maximum_bucket_count, table.bucket_count());
			if (bucket_assignment_width(maximum_bucket_count) != width)
				throw std::invalid_argument("multiway bucket artifact assignment width is not minimal");
			return MultiwayBucketRegistry(std::move(tables));
		}
		if (schema != MULTIWAY_BUCKET_ARTIFACT_SCHEMA_V4)
			throw std::invalid_argument("multiway bucket artifact schema is unsupported");
		const auto identity = read_identity(bytes, cursor);
		const auto table_count = read_u32(bytes, cursor);
		if (table_count == 0U)
			throw std::invalid_argument("multiway bucket artifact has no tables");
		constexpr std::size_t kSmallestTableBytes = 2U + 3U + 4U + MULTIWAY_HOLE_COMBINATION_COUNT * 4U;
		if (static_cast<std::size_t>(table_count) > (bytes.size() - cursor) / kSmallestTableBytes)
		{
			throw std::invalid_argument("multiway bucket artifact table count is invalid");
		}
		std::vector<MultiwayBucketTable> tables;
		tables.reserve(table_count);
		for (std::uint32_t index = 0U; index < table_count; ++index)
		{
			if (bytes.size() - cursor < 2U)
				throw std::invalid_argument("multiway bucket artifact is truncated");
			const auto street = static_cast<Street>(bytes[cursor++]);
			const auto board_size = static_cast<std::size_t>(bytes[cursor++]);
			if (board_size != board_size_for(street) || bytes.size() - cursor < board_size)
			{
				throw std::invalid_argument("multiway bucket artifact has invalid board metadata");
			}
			std::vector<std::uint8_t> board(bytes.begin() + static_cast<std::ptrdiff_t>(cursor),
				bytes.begin() + static_cast<std::ptrdiff_t>(cursor + board_size));
			cursor += board_size;
			const auto bucket_count = read_u32(bytes, cursor);
			std::vector<std::uint32_t> assignments;
			assignments.reserve(MULTIWAY_HOLE_COMBINATION_COUNT);
			for (std::size_t assignment = 0U; assignment < MULTIWAY_HOLE_COMBINATION_COUNT; ++assignment)
			{
				assignments.push_back(read_u32(bytes, cursor));
			}
			if (!is_multiway_canonical_board(street, board))
			{
				throw std::invalid_argument("multiway bucket artifact board is not canonical");
			}
			tables.emplace_back(identity, street, std::move(board), bucket_count, std::move(assignments));
		}
		if (cursor != bytes.size())
			throw std::invalid_argument("multiway bucket artifact has trailing data");
		return MultiwayBucketRegistry(std::move(tables));
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
		constexpr std::uint64_t kV4HeaderBytes = 4U + 4U + kIdentityFieldCount * 8U + 4U;
		constexpr std::uint64_t kHeaderBytes = 4U + 4U + kIdentityFieldCount * 8U + 4U + 4U + 6U * 8U;
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
		if (std::memcmp(input, kMagic, 4U) != 0)
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
			directory_offset = read64(); directory_length = read64(); assignment_offset = read64(); assignment_length = read64();
			const auto directory_hash = read64(); const auto assignment_hash = read64();
			if ((assignment_width != 1U && assignment_width != 2U && assignment_width != 4U) || directory_offset != kHeaderBytes ||
				directory_length != static_cast<std::uint64_t>(table_count) * 32U || assignment_offset != directory_offset + directory_length ||
				assignment_offset > bytes || assignment_length != bytes - assignment_offset ||
				assignment_length != static_cast<std::uint64_t>(table_count) * MULTIWAY_HOLE_COMBINATION_COUNT * assignment_width)
				throw std::invalid_argument("multiway bucket artifact has invalid section bounds");
			auto hash_range = [&](std::uint64_t offset, std::uint64_t length) {
				auto hash = texas::core::fingerprint::FNV1A_OFFSET;
				for (std::uint64_t i = 0U; i < length; ++i) texas::core::fingerprint::append_u8(hash, input[offset + i]);
				return hash;
			};
			if (hash_range(directory_offset, directory_length) != directory_hash || hash_range(assignment_offset, assignment_length) != assignment_hash)
				throw std::invalid_argument("multiway bucket artifact section hash mismatch");
			cursor = static_cast<std::size_t>(directory_offset);
		}
		std::vector<MultiwayBucketTable> tables;
		tables.reserve(table_count);
		for (std::uint32_t index = 0U; index < table_count; ++index)
		{
			Street street;
			std::size_t board_size;
			std::uint32_t bucket_count;
			const std::uint8_t* assignments;
			std::uint64_t expected_table_identity = 0U;
			if (schema == MULTIWAY_BUCKET_ARTIFACT_SCHEMA_VERSION)
			{
				street = static_cast<Street>(read_byte()); board_size = read_byte();
				const auto reserved_a = read_byte(); const auto reserved_b = read_byte();
				const auto board_key = read64();
				bucket_count = read32();
				const auto offset = read64(); expected_table_identity = read64();
				const auto expected_offset = assignment_offset + static_cast<std::uint64_t>(index) * MULTIWAY_HOLE_COMBINATION_COUNT * assignment_width;
				if (board_size != board_size_for(street) || reserved_a != 0U || reserved_b != 0U || (board_key >> (board_size * 6U)) != 0U ||
					bucket_count == 0U || bucket_assignment_width(bucket_count) > assignment_width || offset != expected_offset ||
					offset > assignment_offset + assignment_length - MULTIWAY_HOLE_COMBINATION_COUNT * assignment_width)
					throw std::invalid_argument("multiway bucket artifact has invalid table directory entry");
				std::vector<std::uint8_t> board(board_size);
				for (std::size_t card = 0U; card < board_size; ++card) board[card] = static_cast<std::uint8_t>((board_key >> (card * 6U)) & 0x3fU);
				if (!is_multiway_canonical_board(street, board)) throw std::invalid_argument("multiway bucket artifact board is not canonical");
				assignments = input + offset;
				MultiwayBucketTable table;
				table.identity_ = identity; table.street_ = street; table.canonical_board_ = std::move(board); table.bucket_count_ = bucket_count;
				table.mapped_assignments_ = assignments; table.mapped_assignment_width_ = static_cast<std::uint8_t>(assignment_width);
				auto table_hash = texas::core::fingerprint::FNV1A_OFFSET;
				texas::core::fingerprint::append_u64(table_hash, identity.combined_hash);
				texas::core::fingerprint::append_u64(table_hash, static_cast<std::uint8_t>(street));
				texas::core::fingerprint::append_u64(table_hash, table.canonical_board_.size());
				for (const auto card : table.canonical_board_) texas::core::fingerprint::append_u64(table_hash, card);
				texas::core::fingerprint::append_u64(table_hash, bucket_count);
				for (std::size_t assignment = 0U; assignment < MULTIWAY_HOLE_COMBINATION_COUNT; ++assignment)
					texas::core::fingerprint::append_u64(table_hash, table.assignment_at(assignment));
				if (table_hash != expected_table_identity)
					throw std::invalid_argument("multiway bucket artifact table identity mismatch");
				if (!tables.empty() && !(tables.back().street() < street ||
					(tables.back().street() == street && tables.back().canonical_board() < table.canonical_board_)))
					throw std::invalid_argument("multiway bucket artifact directory is not uniquely sorted");
			table.table_identity_ = table_hash;
				tables.push_back(std::move(table));
				continue;
			}
			street = static_cast<Street>(read_byte());
			board_size = static_cast<std::size_t>(read_byte());
			if (board_size != board_size_for(street))
			{
				throw std::invalid_argument("multiway bucket artifact has invalid board metadata");
			}
			std::vector<std::uint8_t> board(board_size);
			for (auto& card : board)
				card = read_byte();
			bucket_count = read32();
			if (mapped->size - cursor < MULTIWAY_HOLE_COMBINATION_COUNT * 4U)
				throw std::invalid_argument("multiway bucket artifact is truncated");
			assignments = input + cursor;
			cursor += MULTIWAY_HOLE_COMBINATION_COUNT * 4U;
			if (!is_multiway_canonical_board(street, board))
			{
				throw std::invalid_argument("multiway bucket artifact board is not canonical");
			}
			if (!is_multiway_canonical_board(street, board) || bucket_count == 0U)
				throw std::invalid_argument("multiway bucket artifact has invalid table");
			MultiwayBucketTable table;
			table.identity_ = identity;
			table.street_ = street;
			table.canonical_board_ = std::move(board);
			table.bucket_count_ = bucket_count;
			table.mapped_assignments_ = assignments;
			auto table_hash = texas::core::fingerprint::FNV1A_OFFSET;
			texas::core::fingerprint::append_u64(table_hash, identity.combined_hash);
			texas::core::fingerprint::append_u64(table_hash, static_cast<std::uint8_t>(street));
			texas::core::fingerprint::append_u64(table_hash, table.canonical_board_.size());
			for (const auto card : table.canonical_board_)
				texas::core::fingerprint::append_u64(table_hash, card);
			texas::core::fingerprint::append_u64(table_hash, bucket_count);
			table.table_identity_ = table_hash;
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
			for (const auto& table : tables) maximum_bucket_count = std::max(maximum_bucket_count, table.bucket_count());
			if (bucket_assignment_width(maximum_bucket_count) != assignment_width)
				throw std::invalid_argument("multiway bucket artifact assignment width is not minimal");
			cursor = mapped->size;
		}
		if (cursor != mapped->size)
			throw std::invalid_argument("multiway bucket artifact has trailing data");
		return MultiwayBucketRegistry(std::move(tables), std::move(mapped));
	}

} // namespace texas::solver::multiway
