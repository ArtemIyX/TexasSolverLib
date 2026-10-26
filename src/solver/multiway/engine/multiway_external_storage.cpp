#include "solver/multiway/engine/multiway_external_storage.hpp"
#include "solver/multiway/engine/multiway_solver.hpp"
#include "solver/multiway/engine/multiway_compact_storage.hpp"
#include "solver/multiway/abstraction/multiway_action_abstraction.hpp"

#include "core/atomic_publish.hpp"
#include "core/fingerprint.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <fstream>
#include <limits>
#include <queue>
#include <stdexcept>
#include <string>
#include <system_error>

#ifdef _WIN32
	#ifndef NOMINMAX
		#define NOMINMAX
	#endif
	#include <windows.h>
#else
	#include <fcntl.h>
	#include <unistd.h>
#endif

namespace texas::solver::multiway
{
	namespace
	{
		constexpr std::array<std::uint8_t, 8U> kManifestMagic = { 'T','X','M','W','E','X','T','1' };
		constexpr std::array<std::uint8_t, 8U> kWalMagic = { 'T','X','M','W','W','A','L','1' };
		constexpr std::uint32_t kSchema = 1U;
		constexpr std::uint64_t kHashOffset = texas::core::fingerprint::FNV1A_OFFSET;
		constexpr std::uint32_t kNodeRecordMagic = 0x314e574dU;
		constexpr std::size_t kNodeRecordBytes = 225U;
		constexpr std::uint32_t kCatalogMenuMagic = 0x314d574dU;
		constexpr std::uint32_t kCatalogMenuSchema = 1U;
		constexpr std::size_t kCatalogMenuHeaderBytes = 12U;
		constexpr std::size_t kCatalogMenuActionBytes = 17U;
		constexpr std::size_t kCatalogMenuChecksumBytes = 8U;
		constexpr std::uint32_t kPageRecordMagic = 0x3150574dU;
		constexpr std::uint32_t kPageRecordSchema = 1U;
		constexpr std::size_t kPageRecordHeaderBytes = 40U;
		constexpr std::size_t kPageRecordChecksumBytes = 8U;
		constexpr std::size_t kCatalogMenuBytes = kCatalogMenuHeaderBytes +
			MULTIWAY_MAX_ABSTRACTED_ACTIONS * kCatalogMenuActionBytes + kCatalogMenuChecksumBytes;
		constexpr std::array<std::uint8_t, 8U> kTransactionWalMagic = { 'T','X','M','W','B','A','T','1' };
		constexpr std::array<const char*, 3U> kComponentNames = { "catalog", "rows", "pages" };
		constexpr std::array<const char*, 3U> kBackupNames = {
			"CATALOG_CURRENT.previous", "ROWS_CURRENT.previous", "PAGES_CURRENT.previous" };

		struct ComponentManifestInfo
		{
			std::uint64_t generation = 0U;
			std::uint64_t last_batch_number = 0U;
			bool exists = false;
		};
		struct TransactionWal
		{
			std::uint32_t previous_flags = 0U;
			std::uint64_t batch_number = 0U;
			std::uint64_t merge_fingerprint = 0U;
		};

		void append_u32(std::vector<std::uint8_t>& bytes, std::uint32_t value)
		{
			for (unsigned shift = 0U; shift < 32U; shift += 8U) bytes.push_back(static_cast<std::uint8_t>(value >> shift));
		}
		void append_u64(std::vector<std::uint8_t>& bytes, std::uint64_t value)
		{
			for (unsigned shift = 0U; shift < 64U; shift += 8U) bytes.push_back(static_cast<std::uint8_t>(value >> shift));
		}
		std::uint32_t read_u32(const std::uint8_t* bytes)
		{
			return static_cast<std::uint32_t>(bytes[0]) | (static_cast<std::uint32_t>(bytes[1]) << 8U) |
				(static_cast<std::uint32_t>(bytes[2]) << 16U) | (static_cast<std::uint32_t>(bytes[3]) << 24U);
		}
		std::uint64_t read_u64(const std::uint8_t* bytes)
		{
			std::uint64_t result = 0U;
			for (unsigned shift = 0U; shift < 64U; shift += 8U) result |= static_cast<std::uint64_t>(bytes[shift / 8U]) << shift;
			return result;
		}
		void append_i32(std::vector<std::uint8_t>& bytes, int value)
		{
			static_assert(sizeof(int) == sizeof(std::int32_t), "packed node integer width changed");
			const auto signed_value = static_cast<std::int64_t>(value);
			const auto bits = static_cast<std::uint32_t>(signed_value < 0
				? (static_cast<std::uint64_t>(signed_value + (1LL << 32U)))
				: static_cast<std::uint64_t>(signed_value));
			append_u32(bytes, bits);
		}
		int read_i32(const std::uint8_t* bytes)
		{
			const auto bits = read_u32(bytes);
			const auto value = bits <= static_cast<std::uint32_t>(std::numeric_limits<std::int32_t>::max())
				? static_cast<std::int64_t>(bits)
				: static_cast<std::int64_t>(bits) - (1LL << 32U);
			return static_cast<int>(value);
		}
		void write_u64(std::ostream& out, std::uint64_t value)
		{
			for (unsigned shift = 0U; shift < 64U; shift += 8U) out.put(static_cast<char>(value >> shift));
		}
		std::uint64_t checked_add(std::uint64_t left, std::uint64_t right)
		{
			if (right > std::numeric_limits<std::uint64_t>::max() - left)
				throw std::length_error("external storage byte count overflows uint64");
			return left + right;
		}
		std::size_t external_cache_slot(std::uint64_t key, std::size_t capacity) noexcept
		{
			auto mixed = key;
			mixed ^= mixed >> 30U;
			mixed *= 0xbf58476d1ce4e5b9ULL;
			mixed ^= mixed >> 27U;
			mixed *= 0x94d049bb133111ebULL;
			mixed ^= mixed >> 31U;
			return static_cast<std::size_t>(mixed % capacity);
		}
		std::uint64_t record_bytes(std::uint64_t count, std::size_t payload_bytes)
		{
			const auto width = checked_add(8U, payload_bytes);
			if (count != 0U && width > std::numeric_limits<std::uint64_t>::max() / count)
				throw std::length_error("external storage record bytes overflow uint64");
			return count * width;
		}
		std::filesystem::path run_path(const std::filesystem::path& directory, std::uint64_t generation, const char* suffix)
		{
			return directory / ("run_" + std::to_string(generation) + suffix);
		}
		void sync_file_path(const std::filesystem::path& path)
		{
#ifdef _WIN32
			const auto handle = CreateFileW(path.c_str(), GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ,
				nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
			if (handle == INVALID_HANDLE_VALUE) throw std::runtime_error("external transaction file cannot be opened for flush");
			const auto flushed = FlushFileBuffers(handle);
			CloseHandle(handle);
			if (!flushed) throw std::runtime_error("external transaction file flush failed");
#else
			const auto descriptor = open(path.c_str(), O_RDONLY);
			if (descriptor < 0) throw std::runtime_error("external transaction file cannot be opened for flush");
			const auto flushed = fsync(descriptor);
			close(descriptor);
			if (flushed != 0) throw std::runtime_error("external transaction file flush failed");
#endif
		}
		void sync_directory_path(const std::filesystem::path& path)
		{
#ifdef _WIN32
			(void)path;
#else
			const auto descriptor = open(path.c_str(), O_RDONLY | O_DIRECTORY);
			if (descriptor < 0) throw std::runtime_error("external transaction directory cannot be opened for flush");
			const auto flushed = fsync(descriptor);
			close(descriptor);
			if (flushed != 0) throw std::runtime_error("external transaction directory flush failed");
#endif
		}
		ComponentManifestInfo read_component_manifest(const std::filesystem::path& component_directory)
		{
			const auto path = component_directory / "CURRENT";
			std::error_code error;
			if (!std::filesystem::exists(path, error))
			{
				if (error) throw std::runtime_error("external component manifest cannot be inspected");
				return {};
			}
			const auto file_bytes = std::filesystem::file_size(path, error);
			if (error || file_bytes < 48U) throw std::invalid_argument("external component manifest size is invalid");
			std::ifstream input(path, std::ios::binary);
			std::array<std::uint8_t, 40U> header{};
			input.read(reinterpret_cast<char*>(header.data()), static_cast<std::streamsize>(header.size()));
			if (!input || !std::equal(kManifestMagic.begin(), kManifestMagic.end(), header.begin()) ||
				read_u32(header.data() + 8U) != kSchema)
				throw std::invalid_argument("external component manifest header is invalid");
			const auto generation = read_u64(header.data() + 16U);
			const auto run_count = read_u64(header.data() + 24U);
			const auto last_batch_number = read_u64(header.data() + 32U);
			if (run_count > (std::numeric_limits<std::uint64_t>::max() - 48U) / 24U ||
				48U + run_count * 24U != file_bytes)
				throw std::invalid_argument("external component manifest run table size is invalid");
			auto checksum = kHashOffset;
			std::array<char, 64U * 1024U> buffer{};
			std::uint64_t remaining = file_bytes - 8U;
			input.clear();
			input.seekg(0);
			while (remaining != 0U)
			{
				const auto request = static_cast<std::streamsize>(std::min<std::uint64_t>(remaining, buffer.size()));
				input.read(buffer.data(), request);
				if (!input) throw std::invalid_argument("external component manifest is truncated");
				for (std::streamsize index = 0; index < request; ++index)
					texas::core::fingerprint::append_u8(checksum, static_cast<std::uint8_t>(buffer[static_cast<std::size_t>(index)]));
				remaining -= static_cast<std::uint64_t>(request);
			}
		std::array<std::uint8_t, 8U> checksum_bytes{};
		input.read(reinterpret_cast<char*>(checksum_bytes.data()), checksum_bytes.size());
		if (!input || texas::core::fingerprint::finish(checksum) != read_u64(checksum_bytes.data()))
			throw std::invalid_argument("external component manifest checksum is invalid");
		return { generation, last_batch_number, true };
	}
	void write_transaction_wal(const std::filesystem::path& directory,
		const TransactionWal& transaction, bool synchronous_durability,
		bool replace_existing = false)
	{
		std::vector<std::uint8_t> bytes(kTransactionWalMagic.begin(), kTransactionWalMagic.end());
		append_u32(bytes, 1U);
		append_u32(bytes, transaction.previous_flags);
		append_u64(bytes, transaction.batch_number);
		append_u64(bytes, transaction.merge_fingerprint);
		auto checksum = kHashOffset;
		for (const auto byte : bytes) texas::core::fingerprint::append_u8(checksum, byte);
		append_u64(bytes, texas::core::fingerprint::finish(checksum));
		const auto temp = directory / "TRAINING_BATCH.tmp";
		const auto wal = directory / "TRAINING_BATCH.wal";
		if (std::filesystem::exists(temp) ||
			(!replace_existing && std::filesystem::exists(wal)) ||
			(replace_existing && !std::filesystem::exists(wal)))
			throw std::runtime_error("external transaction already has a pending journal");
		std::ofstream output(temp, std::ios::binary | std::ios::trunc);
		if (!output) throw std::runtime_error("external transaction journal cannot be opened");
		output.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
		output.flush();
		if (!output) throw std::runtime_error("external transaction journal write failed");
		output.close();
		if (synchronous_durability) sync_file_path(temp);
		texas::core::publish_atomic_replace(temp, wal, "external transaction journal publish failed",
			texas::core::AtomicPublishFallbackPolicy::RejectNonAtomicFallback);
		if (synchronous_durability) sync_directory_path(directory);
	}
	TransactionWal read_transaction_wal(const std::filesystem::path& path)
	{
		if (std::filesystem::file_size(path) != 40U)
			throw std::invalid_argument("external transaction journal size is invalid");
		std::array<std::uint8_t, 40U> bytes{};
		std::ifstream input(path, std::ios::binary);
		input.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
		if (!input || !std::equal(kTransactionWalMagic.begin(), kTransactionWalMagic.end(), bytes.begin()) ||
			read_u32(bytes.data() + 8U) != 1U)
			throw std::invalid_argument("external transaction journal header is invalid");
		auto checksum = kHashOffset;
		for (std::size_t index = 0U; index < bytes.size() - 8U; ++index)
			texas::core::fingerprint::append_u8(checksum, bytes[index]);
		if (texas::core::fingerprint::finish(checksum) != read_u64(bytes.data() + 32U))
			throw std::invalid_argument("external transaction journal checksum is invalid");
		TransactionWal result{ read_u32(bytes.data() + 12U), read_u64(bytes.data() + 16U), read_u64(bytes.data() + 24U) };
		if ((result.previous_flags & ~7U) != 0U || result.batch_number == 0U)
			throw std::invalid_argument("external transaction journal fields are invalid");
		return result;
	}
		void append_file_hash(std::uint64_t& hash, const std::filesystem::path& path)
		{
			std::ifstream input(path, std::ios::binary);
			if (!input) throw std::runtime_error("external storage run cannot be read");
			std::array<char, 64U * 1024U> buffer{};
			while (input)
			{
				input.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
				const auto count = input.gcount();
				for (std::streamsize index = 0; index < count; ++index)
					texas::core::fingerprint::append_u8(hash, static_cast<std::uint8_t>(buffer[static_cast<std::size_t>(index)]));
			}
			if (!input.eof()) throw std::runtime_error("external storage run read failed");
		}
	}

	void encode_external_public_node(const MultiwayPackedPublicNode& node,
		std::vector<std::uint8_t>& output)
	{
		if (node.id.value == 0U || node.betting.seat_count > node.betting.stacks.size() ||
			node.board_count > node.board.size() || node.dealt_card_count > node.dealt_cards.size() ||
			node.transition_board_count > node.transition_board.size() ||
			static_cast<std::uint8_t>(node.betting.street) > static_cast<std::uint8_t>(core::Street::Showdown) ||
			static_cast<std::uint8_t>(node.incoming_kind) > static_cast<std::uint8_t>(MultiwayPublicParentEdgeKind::StreetTransition) ||
			static_cast<std::uint8_t>(node.incoming_action.action) > static_cast<std::uint8_t>(MultiwayAction::AllIn))
			throw std::invalid_argument("packed public node exceeds its fixed record bounds");
		for (const auto value : node.betting.folded) if (value > 1U) throw std::invalid_argument("packed public node has invalid folded flags");
		for (const auto value : node.betting.all_in) if (value > 1U) throw std::invalid_argument("packed public node has invalid all-in flags");
		for (const auto value : node.betting.may_raise) if (value > 1U) throw std::invalid_argument("packed public node has invalid raise flags");
		for (const auto value : node.betting.pending) if (value > 1U) throw std::invalid_argument("packed public node has invalid pending flags");
		for (const auto value : node.betting.has_acted) if (value > 1U) throw std::invalid_argument("packed public node has invalid acted flags");
		output.clear();
		output.reserve(240U);
		append_u32(output, kNodeRecordMagic);
		append_u32(output, MULTIWAY_EXTERNAL_NODE_RECORD_SCHEMA);
		append_u64(output, node.id.value);
		append_u64(output, node.parent_id.value);
		append_u64(output, node.canonical_history_id);
		for (const auto value : node.betting.stacks) append_i32(output, value);
		for (const auto value : node.betting.contributions) append_i32(output, value);
		for (const auto value : node.betting.street_contributions) append_i32(output, value);
		for (const auto value : node.betting.folded) output.push_back(value);
		for (const auto value : node.betting.all_in) output.push_back(value);
		for (const auto value : node.betting.may_raise) output.push_back(value);
		for (const auto value : node.betting.pending) output.push_back(value);
		for (const auto value : node.betting.has_acted) output.push_back(value);
		for (const auto value : node.betting.bet_faced_when_acted) append_i32(output, value);
		output.push_back(node.betting.seat_count);
		append_i32(output, node.betting.current_player);
		append_i32(output, node.betting.last_aggressor);
		append_i32(output, node.betting.current_bet);
		append_i32(output, node.betting.last_full_raise_size);
		append_i32(output, node.betting.big_blind);
		output.push_back(static_cast<std::uint8_t>(node.betting.street));
		output.insert(output.end(), node.board.begin(), node.board.end());
		output.push_back(node.board_count);
		output.push_back(node.board_runout.remaining_board_cards);
		output.push_back(node.board_runout.chance_only_runout ? 1U : 0U);
		output.push_back(static_cast<std::uint8_t>(node.incoming_kind));
		output.push_back(static_cast<std::uint8_t>(node.incoming_action.action));
		append_u32(output, node.incoming_action.action_index);
		append_i32(output, node.incoming_action.target_street_contribution);
		append_u64(output, node.incoming_action.action_menu_id);
		append_i32(output, node.incoming_actor);
		output.push_back(node.dealt_card);
		output.insert(output.end(), node.dealt_cards.begin(), node.dealt_cards.end());
		output.push_back(node.dealt_card_count);
		output.insert(output.end(), node.transition_board.begin(), node.transition_board.end());
		output.push_back(node.transition_board_count);
		append_u32(output, node.legal_menu_handle);
		if (output.size() != kNodeRecordBytes)
			throw std::logic_error("packed public node codec width changed without a schema update");
	}

	std::size_t external_public_node_record_size() noexcept
	{
		return kNodeRecordBytes;
	}

	std::size_t external_public_catalog_record_size() noexcept
	{
		return kNodeRecordBytes + kCatalogMenuBytes;
	}

	void publish_multiway_external_generation(const std::filesystem::path& directory,
		const MultiwayExternalGenerationManifest& manifest, bool synchronous_durability)
	{
		if (directory.empty() || manifest.generation == 0U || manifest.committed_batch_number == 0U ||
			manifest.merge_fingerprint == 0U)
			throw std::invalid_argument("external generation manifest identity is invalid");
		std::vector<std::uint8_t> bytes{ 'T','X','M','W','G','E','N','1' };
		append_u32(bytes, 1U);
		append_u32(bytes, 0U);
		append_u64(bytes, manifest.generation);
		append_u64(bytes, manifest.committed_batch_number);
		append_u64(bytes, manifest.merge_fingerprint);
		append_u64(bytes, manifest.catalog_generation);
		append_u64(bytes, manifest.public_state_count);
		append_u64(bytes, manifest.row_generation);
		append_u64(bytes, manifest.row_count);
		append_u64(bytes, manifest.page_generation);
		append_u64(bytes, manifest.page_count);
		append_u64(bytes, manifest.content_hash);
		auto checksum = kHashOffset;
		for (const auto byte : bytes) texas::core::fingerprint::append_u8(checksum, byte);
		append_u64(bytes, texas::core::fingerprint::finish(checksum));
		const auto temp = directory / "TRAINING_CURRENT.tmp";
		const auto current = directory / "TRAINING_CURRENT";
		if (std::filesystem::exists(temp))
			throw std::runtime_error("external training manifest temporary path already exists");
		std::ofstream output(temp, std::ios::binary | std::ios::trunc);
		if (!output) throw std::runtime_error("external training manifest cannot be opened");
		output.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
		output.flush();
		if (!output) throw std::runtime_error("external training manifest write failed");
		output.close();
		if (synchronous_durability)
		{
#ifdef _WIN32
			const auto handle = CreateFileW(temp.c_str(), GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ, nullptr,
				OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
			if (handle == INVALID_HANDLE_VALUE) throw std::runtime_error("external training manifest cannot be opened for flush");
			const auto flushed = FlushFileBuffers(handle);
			CloseHandle(handle);
			if (!flushed) throw std::runtime_error("external training manifest flush failed");
#else
			const auto descriptor = open(temp.c_str(), O_RDONLY);
			if (descriptor < 0) throw std::runtime_error("external training manifest cannot be opened for flush");
			const auto flushed = fsync(descriptor);
			close(descriptor);
			if (flushed != 0) throw std::runtime_error("external training manifest flush failed");
#endif
		}
		texas::core::publish_atomic_replace(temp, current, "external training manifest publish failed",
			texas::core::AtomicPublishFallbackPolicy::RejectNonAtomicFallback);
		if (synchronous_durability)
		{
#ifndef _WIN32
			const auto descriptor = open(directory.c_str(), O_RDONLY | O_DIRECTORY);
			if (descriptor < 0) throw std::runtime_error("external training directory cannot be opened for flush");
			const auto flushed = fsync(descriptor);
			close(descriptor);
			if (flushed != 0) throw std::runtime_error("external training directory flush failed");
#endif
		}
	}

	bool load_multiway_external_generation(const std::filesystem::path& directory,
		MultiwayExternalGenerationManifest& manifest)
	{
		const auto path = directory / "TRAINING_CURRENT";
		std::error_code error;
		if (!std::filesystem::exists(path, error))
		{
			if (error) throw std::runtime_error("external training manifest cannot be inspected");
			return false;
		}
		constexpr std::size_t manifest_size = 104U;
		if (std::filesystem::file_size(path, error) != manifest_size || error)
			throw std::invalid_argument("external training manifest size is invalid");
		std::array<std::uint8_t, manifest_size> bytes{};
		std::ifstream input(path, std::ios::binary);
		input.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
		constexpr std::array<std::uint8_t, 8U> magic = { 'T','X','M','W','G','E','N','1' };
		if (!input || !std::equal(magic.begin(), magic.end(), bytes.begin()) ||
			read_u32(bytes.data() + 8U) != 1U || read_u32(bytes.data() + 12U) != 0U)
			throw std::invalid_argument("external training manifest header is invalid");
		auto checksum = kHashOffset;
		for (std::size_t index = 0U; index < bytes.size() - 8U; ++index)
			texas::core::fingerprint::append_u8(checksum, bytes[index]);
		if (texas::core::fingerprint::finish(checksum) != read_u64(bytes.data() + bytes.size() - 8U))
			throw std::invalid_argument("external training manifest checksum is invalid");
		MultiwayExternalGenerationManifest decoded{};
		std::size_t cursor = 16U;
		const auto read_field = [&]() { const auto value = read_u64(bytes.data() + cursor); cursor += 8U; return value; };
		decoded.generation = read_field();
		decoded.committed_batch_number = read_field();
		decoded.merge_fingerprint = read_field();
		decoded.catalog_generation = read_field();
		decoded.public_state_count = read_field();
		decoded.row_generation = read_field();
		decoded.row_count = read_field();
		decoded.page_generation = read_field();
		decoded.page_count = read_field();
		decoded.content_hash = read_field();
		if (cursor != bytes.size() - 8U || decoded.generation == 0U ||
			decoded.committed_batch_number == 0U || decoded.merge_fingerprint == 0U)
			throw std::invalid_argument("external training manifest identity is invalid");
		manifest = decoded;
		return true;
	}

	MultiwayExternalBatchTransaction::MultiwayExternalBatchTransaction(
		std::filesystem::path directory, bool synchronous_durability)
		: directory_(std::move(directory)), synchronous_durability_(synchronous_durability)
	{
		if (directory_.empty()) throw std::invalid_argument("external batch transaction directory is empty");
		std::error_code error;
		std::filesystem::create_directories(directory_, error);
		if (error || !std::filesystem::is_directory(directory_, error) || error)
			throw std::runtime_error("external batch transaction directory is unavailable");
		if (!load_multiway_external_generation(directory_, current_))
			current_ = {};
		recover_pending_batch();
	}

	void MultiwayExternalBatchTransaction::begin_batch(
		std::uint64_t batch_number, std::uint64_t merge_fingerprint)
	{
		if (batch_open_ || batch_number == 0U ||
			batch_number <= current_.committed_batch_number ||
			current_.generation == std::numeric_limits<std::uint64_t>::max())
			throw std::invalid_argument("external batch transaction identity is invalid");
		previous_manifest_flags_ = 0U;
		previous_component_generations_ = {};
		const auto wal = directory_ / "TRAINING_BATCH.wal";
		const auto temp = directory_ / "TRAINING_BATCH.tmp";
		if (std::filesystem::exists(wal) || std::filesystem::exists(temp))
			throw std::runtime_error("external batch transaction has an unrecovered journal");
		for (std::size_t index = 0U; index < kComponentNames.size(); ++index)
		{
			const auto component = directory_ / kComponentNames[index];
			std::error_code error;
			std::filesystem::create_directories(component, error);
			if (error) throw std::runtime_error("external batch component directory cannot be created");
			const auto info = read_component_manifest(component);
			previous_component_generations_[index] = info.generation;
			const auto backup = directory_ / kBackupNames[index];
			if (std::filesystem::exists(backup))
			{
				std::filesystem::remove(backup, error);
				if (error) throw std::runtime_error("stale external component backup cannot be removed");
			}
			const auto current = component / "CURRENT";
			if (info.exists)
			{
				std::filesystem::copy_file(current, backup);
				if (synchronous_durability_) sync_file_path(backup);
				previous_manifest_flags_ |= (1U << index);
			}
		}
		if (synchronous_durability_) sync_directory_path(directory_);
		try
		{
			write_transaction_wal(directory_, { previous_manifest_flags_, batch_number, merge_fingerprint },
				synchronous_durability_);
		}
		catch (...)
		{
			std::error_code ignored;
			for (const auto* name : kBackupNames) std::filesystem::remove(directory_ / name, ignored);
			std::filesystem::remove(temp, ignored);
			std::filesystem::remove(wal, ignored);
			previous_manifest_flags_ = 0U;
			throw;
		}
		batch_number_ = batch_number;
		merge_fingerprint_ = merge_fingerprint;
		batch_open_ = true;
	}

	void MultiwayExternalBatchTransaction::set_merge_fingerprint(
		std::uint64_t merge_fingerprint)
	{
		if (!batch_open_ || merge_fingerprint == 0U ||
			(merge_fingerprint_ != 0U && merge_fingerprint_ != merge_fingerprint))
			throw std::invalid_argument("external batch merge fingerprint cannot be finalized");
		if (merge_fingerprint_ == merge_fingerprint) return;
		write_transaction_wal(directory_,
			{ previous_manifest_flags_, batch_number_, merge_fingerprint },
			synchronous_durability_, true);
		merge_fingerprint_ = merge_fingerprint;
	}

	void MultiwayExternalBatchTransaction::commit_batch(
		const MultiwayExternalGenerationManifest& manifest)
	{
		if (!batch_open_ || manifest.generation != current_.generation + 1U ||
			manifest.committed_batch_number != batch_number_ || manifest.merge_fingerprint != merge_fingerprint_)
			throw std::invalid_argument("external batch commit does not match its begin record");
		const std::array<std::uint64_t, 3U> expected_generations = {
			manifest.catalog_generation, manifest.row_generation, manifest.page_generation };
		for (std::size_t index = 0U; index < kComponentNames.size(); ++index)
		{
			const auto info = read_component_manifest(directory_ / kComponentNames[index]);
			if (info.generation != expected_generations[index] ||
				(info.exists && info.last_batch_number > batch_number_) ||
				(info.generation != previous_component_generations_[index] && info.last_batch_number != batch_number_))
				throw std::invalid_argument("external component generation does not match the batch commit");
		}
		publish_multiway_external_generation(directory_, manifest, synchronous_durability_);
		current_ = manifest;
		batch_open_ = false;
		clear_pending_batch();
	}

	void MultiwayExternalBatchTransaction::abort_batch()
	{
		if (!batch_open_) return;
		rollback_pending_batch(previous_manifest_flags_);
		batch_open_ = false;
		batch_number_ = 0U;
		merge_fingerprint_ = 0U;
		previous_manifest_flags_ = 0U;
		if (!load_multiway_external_generation(directory_, current_)) current_ = {};
	}

	void MultiwayExternalBatchTransaction::recover_pending_batch()
	{
		const auto wal = directory_ / "TRAINING_BATCH.wal";
		if (!std::filesystem::exists(wal))
		{
			const auto temp = directory_ / "TRAINING_BATCH.tmp";
			if (std::filesystem::exists(temp))
			{
				std::error_code error;
				std::filesystem::remove(temp, error);
				if (error) throw std::runtime_error("incomplete external transaction journal cannot be removed");
			}
			return;
		}
		const auto pending = read_transaction_wal(wal);
		if (pending.batch_number <= current_.committed_batch_number)
		{
			if (pending.batch_number == current_.committed_batch_number &&
				pending.merge_fingerprint != current_.merge_fingerprint)
				throw std::invalid_argument("external transaction journal conflicts with the committed manifest");
			clear_pending_batch();
			return;
		}
		batch_number_ = pending.batch_number;
		merge_fingerprint_ = pending.merge_fingerprint;
		previous_manifest_flags_ = pending.previous_flags;
		rollback_pending_batch(pending.previous_flags);
	}

	void MultiwayExternalBatchTransaction::rollback_pending_batch(std::uint32_t previous_manifest_flags)
	{
		MultiwayExternalGenerationManifest committed{};
		const auto has_manifest = load_multiway_external_generation(directory_, committed);
		if (has_manifest && committed.committed_batch_number >= batch_number_ &&
			committed.committed_batch_number != 0U)
		{
			if (committed.committed_batch_number == batch_number_ && committed.merge_fingerprint != merge_fingerprint_)
				throw std::invalid_argument("external batch journal conflicts with the published generation");
			current_ = committed;
			clear_pending_batch();
			return;
		}
		for (std::size_t index = 0U; index < kComponentNames.size(); ++index)
		{
			const auto component = directory_ / kComponentNames[index];
			std::error_code error;
			const auto current = component / "CURRENT";
			const auto backup = directory_ / kBackupNames[index];
			if ((previous_manifest_flags & (1U << index)) != 0U)
			{
				if (!std::filesystem::exists(backup))
					throw std::runtime_error("external transaction rollback manifest backup is missing");
				const auto restore_temp = component / "CURRENT.restore.tmp";
				if (std::filesystem::exists(restore_temp))
				{
					std::filesystem::remove(restore_temp, error);
					if (error) throw std::runtime_error("external transaction restore temporary cannot be cleared");
				}
				std::filesystem::copy_file(backup, restore_temp);
				if (synchronous_durability_) sync_file_path(restore_temp);
				texas::core::publish_atomic_replace(restore_temp, current,
					"external component manifest rollback failed",
					texas::core::AtomicPublishFallbackPolicy::RejectNonAtomicFallback);
			}
			else
			{
				std::filesystem::remove(current, error);
				if (error) throw std::runtime_error("new external component manifest cannot be rolled back");
			}
			for (const auto* name : { "BATCH.wal", "CURRENT.tmp", "CURRENT.restore.tmp" })
			{
				std::filesystem::remove(component / name, error);
				if (error) throw std::runtime_error("external component transaction temporary cannot be removed");
			}
			if (synchronous_durability_) sync_directory_path(component);
		}
		std::error_code error;
		std::filesystem::remove(directory_ / "TRAINING_CURRENT.tmp", error);
		if (error) throw std::runtime_error("external training manifest temporary cannot be removed");
		current_ = {};
		(void)load_multiway_external_generation(directory_, current_);
		clear_pending_batch();
	}

	void MultiwayExternalBatchTransaction::clear_pending_batch()
	{
		std::error_code error;
		for (const auto* name : { "TRAINING_BATCH.wal", "TRAINING_BATCH.tmp" })
		{
			std::filesystem::remove(directory_ / name, error);
			if (error) throw std::runtime_error("external transaction journal cannot be removed");
		}
		for (const auto* name : kBackupNames)
		{
			std::filesystem::remove(directory_ / name, error);
			if (error) throw std::runtime_error("external transaction backup cannot be removed");
		}
		if (synchronous_durability_) sync_directory_path(directory_);
		batch_number_ = 0U;
		merge_fingerprint_ = 0U;
		previous_manifest_flags_ = 0U;
	}

	bool decode_external_public_node(const std::uint8_t* bytes, std::size_t size,
		MultiwayPackedPublicNode& output) noexcept
	{
		if (bytes == nullptr || size < 8U || read_u32(bytes) != kNodeRecordMagic ||
			read_u32(bytes + 4U) != MULTIWAY_EXTERNAL_NODE_RECORD_SCHEMA)
			return false;
		if (size != kNodeRecordBytes) return false;
		MultiwayPackedPublicNode node{};
		std::size_t cursor = 8U;
		const auto get_u32 = [&]() { const auto value = read_u32(bytes + cursor); cursor += 4U; return value; };
		const auto get_i32 = [&]() { const auto value = read_i32(bytes + cursor); cursor += 4U; return value; };
		const auto get_u64 = [&]() { const auto value = read_u64(bytes + cursor); cursor += 8U; return value; };
		node.id.value = get_u64(); node.parent_id.value = get_u64(); node.canonical_history_id = get_u64();
		for (auto& value : node.betting.stacks) value = get_i32();
		for (auto& value : node.betting.contributions) value = get_i32();
		for (auto& value : node.betting.street_contributions) value = get_i32();
		for (auto& value : node.betting.folded) value = bytes[cursor++];
		for (auto& value : node.betting.all_in) value = bytes[cursor++];
		for (auto& value : node.betting.may_raise) value = bytes[cursor++];
		for (auto& value : node.betting.pending) value = bytes[cursor++];
		for (auto& value : node.betting.has_acted) value = bytes[cursor++];
		for (auto& value : node.betting.bet_faced_when_acted) value = get_i32();
		node.betting.seat_count = bytes[cursor++];
		node.betting.current_player = get_i32(); node.betting.last_aggressor = get_i32();
		node.betting.current_bet = get_i32(); node.betting.last_full_raise_size = get_i32(); node.betting.big_blind = get_i32();
		node.betting.street = static_cast<core::Street>(bytes[cursor++]);
		std::copy_n(bytes + cursor, node.board.size(), node.board.begin()); cursor += node.board.size();
		node.board_count = bytes[cursor++];
		node.board_runout.remaining_board_cards = bytes[cursor++];
		if (bytes[cursor] > 1U) return false;
		node.board_runout.chance_only_runout = bytes[cursor++] != 0U;
		node.incoming_kind = static_cast<MultiwayPublicParentEdgeKind>(bytes[cursor++]);
		node.incoming_action.action = static_cast<MultiwayAction>(bytes[cursor++]);
		node.incoming_action.action_index = get_u32();
		node.incoming_action.target_street_contribution = get_i32();
		node.incoming_action.action_menu_id = get_u64();
		node.incoming_actor = get_i32(); node.dealt_card = bytes[cursor++];
		std::copy_n(bytes + cursor, node.dealt_cards.size(), node.dealt_cards.begin()); cursor += node.dealt_cards.size();
		node.dealt_card_count = bytes[cursor++];
		std::copy_n(bytes + cursor, node.transition_board.size(), node.transition_board.begin()); cursor += node.transition_board.size();
		node.transition_board_count = bytes[cursor++]; node.legal_menu_handle = get_u32();
		if (cursor != size || node.id.value == 0U || node.betting.seat_count > node.betting.stacks.size() ||
			node.board_count > node.board.size() || node.dealt_card_count > node.dealt_cards.size() ||
			node.transition_board_count > node.transition_board.size() ||
			static_cast<std::uint8_t>(node.betting.street) > static_cast<std::uint8_t>(core::Street::Showdown) ||
			static_cast<std::uint8_t>(node.incoming_kind) > static_cast<std::uint8_t>(MultiwayPublicParentEdgeKind::StreetTransition) ||
			static_cast<std::uint8_t>(node.incoming_action.action) > static_cast<std::uint8_t>(MultiwayAction::AllIn))
			return false;
		for (const auto value : node.betting.folded) if (value > 1U) return false;
		for (const auto value : node.betting.all_in) if (value > 1U) return false;
		for (const auto value : node.betting.may_raise) if (value > 1U) return false;
		for (const auto value : node.betting.pending) if (value > 1U) return false;
		for (const auto value : node.betting.has_acted) if (value > 1U) return false;
		output = node;
		return true;
	}

	void MultiwayExternalStorageConfig::validate(std::size_t payload_bytes) const
	{
		if (directory.empty() || payload_bytes == 0U || payload_bytes > std::numeric_limits<std::uint32_t>::max() ||
			cache_bytes < payload_bytes + 8U || maximum_wal_bytes < payload_bytes + 56U ||
			segment_target_bytes < payload_bytes + 8U || compaction_fan_in < 2U || compaction_scratch_bytes == 0U || disk_reserve_bytes == 0U)
			throw std::invalid_argument("external storage requires a directory and positive bounded resources");
		constexpr std::uint64_t kCompactionCursorBytes = 2048U;
		const auto per_cursor = checked_add(payload_bytes, kCompactionCursorBytes);
		if (compaction_fan_in != 0U && per_cursor > std::numeric_limits<std::uint64_t>::max() / compaction_fan_in)
			throw std::invalid_argument("external storage compaction scratch bound overflows");
		const auto minimum_scratch = static_cast<std::uint64_t>(compaction_fan_in) * per_cursor;
		if (compaction_scratch_bytes < minimum_scratch)
			throw std::invalid_argument("external storage compaction scratch is below its bounded fan-in requirement");
	}

	MultiwayExternalRecordStore::MultiwayExternalRecordStore(MultiwayExternalStorageConfig config, std::size_t payload_bytes)
		: config_(std::move(config)), payload_bytes_(payload_bytes)
	{
		config_.validate(payload_bytes_);
		cache_capacity_ = static_cast<std::size_t>(config_.cache_bytes / (payload_bytes_ + sizeof(CacheEntry) + 8U));
		if (cache_capacity_ == 0U) throw std::invalid_argument("external storage cache cannot hold one record");
		cache_.resize(cache_capacity_);
		for (auto& entry : cache_) entry.payload.resize(payload_bytes_);
		std::error_code error;
		if (std::filesystem::exists(config_.directory, error))
		{
			if (error || !std::filesystem::is_directory(config_.directory, error) || error)
				throw std::invalid_argument("external storage path is not a directory");
			if (!std::filesystem::exists(config_.directory / "CURRENT", error))
			{
				if (error) throw std::runtime_error("external storage manifest cannot be inspected");
				for (const auto& entry : std::filesystem::directory_iterator(config_.directory))
				{
					const auto name = entry.path().filename().string();
					if (name != "BATCH.wal" && name != "CURRENT.tmp" && name.rfind("run_", 0U) != 0U)
						throw std::invalid_argument("external storage directory contains an unrecognized file without a manifest");
				}
			}
		}
		else
		{
			if (error) throw std::runtime_error("external storage directory cannot be inspected");
			std::filesystem::create_directories(config_.directory, error);
			if (error) throw std::runtime_error("external storage directory cannot be created");
		}
		if (std::filesystem::exists(config_.directory / "CURRENT")) load_manifest();
		cleanup_orphans();
		recover_wal();
	}

	MultiwayExternalRecordStore::~MultiwayExternalRecordStore() = default;

	void MultiwayExternalRecordStore::check_disk_reserve(std::uint64_t additional_bytes) const
	{
		std::error_code error;
		const auto space = std::filesystem::space(config_.directory, error);
		if (error || checked_add(additional_bytes, config_.disk_reserve_bytes) > space.available)
			throw std::runtime_error("external storage disk reserve would be violated");
	}

	void MultiwayExternalRecordStore::flush_file(const std::filesystem::path& path) const
	{
		if (!config_.synchronous_durability) return;
#ifdef _WIN32
		const auto handle = CreateFileW(path.c_str(), GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
		if (handle == INVALID_HANDLE_VALUE) throw std::runtime_error("external storage file cannot be opened for durable flush");
		const auto ok = FlushFileBuffers(handle);
		CloseHandle(handle);
		if (!ok) throw std::runtime_error("external storage durable file flush failed");
#else
		const auto descriptor = open(path.c_str(), O_RDONLY);
		if (descriptor < 0) throw std::runtime_error("external storage file cannot be opened for durable flush");
		const auto result = fsync(descriptor);
		close(descriptor);
		if (result != 0) throw std::runtime_error("external storage durable file flush failed");
#endif
	}

	void MultiwayExternalRecordStore::flush_directory() const
	{
		if (!config_.synchronous_durability) return;
#ifdef _WIN32
		// Atomic replacement uses Windows write-through semantics in the shared helper.
#else
		const auto descriptor = open(config_.directory.c_str(), O_RDONLY | O_DIRECTORY);
		if (descriptor < 0) throw std::runtime_error("external storage directory cannot be opened for durable flush");
		const auto result = fsync(descriptor);
		close(descriptor);
		if (result != 0) throw std::runtime_error("external storage durable directory flush failed");
#endif
	}

	void MultiwayExternalRecordStore::write_wal(
		std::uint64_t batch_number, std::uint64_t merge_fingerprint,
		const std::vector<MultiwayExternalRecord>& records) const
	{
		const auto wal = config_.directory / "BATCH.wal";
		const auto bytes = checked_add(48U, record_bytes(records.size(), payload_bytes_));
		if (bytes > config_.maximum_wal_bytes) throw std::length_error("external storage batch exceeds the WAL limit");
		check_disk_reserve(bytes);
		if (std::filesystem::exists(wal)) throw std::runtime_error("external storage has an unrecovered WAL");
		std::ofstream output(wal, std::ios::binary | std::ios::trunc);
		if (!output) throw std::runtime_error("external storage WAL cannot be opened");
		output.write(reinterpret_cast<const char*>(kWalMagic.data()), static_cast<std::streamsize>(kWalMagic.size()));
		auto wal_hash = kHashOffset;
		for (const auto byte : kWalMagic) texas::core::fingerprint::append_u8(wal_hash, byte);
		const auto hash_u64 = [&wal_hash](std::uint64_t value) {
			for (unsigned shift = 0U; shift < 64U; shift += 8U) texas::core::fingerprint::append_u8(wal_hash, static_cast<std::uint8_t>(value >> shift));
		};
		hash_u64(batch_number); hash_u64(merge_fingerprint); hash_u64(records.size());
		write_u64(output, batch_number);
		write_u64(output, merge_fingerprint);
		write_u64(output, records.size());
		for (const auto& record : records)
		{
			write_u64(output, record.key);
			output.write(reinterpret_cast<const char*>(record.payload.data()), static_cast<std::streamsize>(record.payload.size()));
			hash_u64(record.key);
			for (const auto byte : record.payload) texas::core::fingerprint::append_u8(wal_hash, byte);
		}
		hash_u64(0x434f4d4d49545445ULL);
		write_u64(output, 0x434f4d4d49545445ULL);
		write_u64(output, texas::core::fingerprint::finish(wal_hash));
		output.flush();
		if (!output) throw std::runtime_error("external storage WAL write failed");
		output.close();
		flush_file(wal);
	}

	void MultiwayExternalRecordStore::append_batch(
		std::uint64_t batch_number, std::uint64_t merge_fingerprint,
		const std::vector<MultiwayExternalRecord>& records)
	{
		std::unique_lock<std::mutex> lock(mutex_);
		if (failed_) throw std::runtime_error("external storage is unavailable after a failed batch");
		if (batch_number <= last_batch_number_ || records.empty())
			throw std::invalid_argument("external storage batch number must increase and contain records");
		for (std::size_t index = 0U; index < records.size(); ++index)
		{
			if (records[index].payload.size() != payload_bytes_ || (index != 0U && records[index - 1U].key >= records[index].key))
				throw std::invalid_argument("external storage batch records must be fixed-width and strictly sorted");
		}
		if (generation_ == std::numeric_limits<std::uint64_t>::max()) throw std::overflow_error("external storage generation overflows");
		const auto next_generation = generation_ + 1U;
		const auto data_path = run_path(config_.directory, next_generation, ".dat");
		auto data_temp = data_path; data_temp += ".tmp";
		const auto index_path = run_path(config_.directory, next_generation, ".idx");
		auto index_temp = index_path; index_temp += ".tmp";
		const auto batch_bytes = record_bytes(records.size(), payload_bytes_);
		if (batch_bytes > config_.segment_target_bytes)
			throw std::length_error("external storage batch exceeds the segment target");
		if (records.size() > std::numeric_limits<std::uint64_t>::max() / 8U)
			throw std::length_error("external storage index bytes overflow uint64");
		const auto index_bytes = checked_add(0U, static_cast<std::uint64_t>(records.size()) * 8U);
		check_disk_reserve(checked_add(checked_add(batch_bytes, index_bytes), config_.maximum_wal_bytes));
		try
		{
			write_wal(batch_number, merge_fingerprint, records);
			std::ofstream data(data_temp, std::ios::binary | std::ios::trunc);
			std::ofstream index(index_temp, std::ios::binary | std::ios::trunc);
			if (!data || !index) throw std::runtime_error("external storage segment cannot be opened");
			for (const auto& record : records)
			{
				write_u64(data, record.key);
				data.write(reinterpret_cast<const char*>(record.payload.data()), static_cast<std::streamsize>(record.payload.size()));
				write_u64(index, record.key);
			}
			data.flush(); index.flush();
			if (!data || !index) throw std::runtime_error("external storage segment write failed");
			data.close(); index.close();
			flush_file(data_temp); flush_file(index_temp);
			if (std::filesystem::exists(data_path) || std::filesystem::exists(index_path))
				throw std::runtime_error("external storage generation file already exists");
			texas::core::publish_atomic_replace(data_temp, data_path, "external data segment publish failed",
				texas::core::AtomicPublishFallbackPolicy::RejectNonAtomicFallback);
			texas::core::publish_atomic_replace(index_temp, index_path, "external index run publish failed",
				texas::core::AtomicPublishFallbackPolicy::RejectNonAtomicFallback);
			flush_directory();
			Run run{ next_generation, records.size(), 0U, data_path, index_path };
			run.content_hash = run_hash(run);
			auto next_runs = runs_;
			next_runs.push_back(run);
			publish_manifest(next_runs, next_generation, batch_number);
			runs_.swap(next_runs);
			generation_ = next_generation;
			record_count_ = checked_add(record_count_, records.size());
			last_batch_number_ = batch_number;
			for (const auto& record : records)
			{
				auto& cached = cache_[external_cache_slot(record.key, cache_.size())];
				if (cached.valid && cached.key == record.key) cached.valid = false;
			}
			flush_directory();
			std::error_code ignored;
			std::filesystem::remove(config_.directory / "BATCH.wal", ignored);
		}
		catch (...)
		{
			failed_ = true;
			std::error_code ignored;
			std::filesystem::remove(data_temp, ignored);
			std::filesystem::remove(index_temp, ignored);
			throw;
		}
		lock.unlock();
	}

	bool MultiwayExternalRecordStore::lookup(std::uint64_t key, std::vector<std::uint8_t>& payload) const
	{
		payload.resize(payload_bytes_);
		return lookup(key, payload.data(), payload.size());
	}

	bool MultiwayExternalRecordStore::lookup(std::uint64_t key, std::uint8_t* payload,
		std::size_t payload_size) const
	{
		if (payload == nullptr || payload_size != payload_bytes_)
			throw std::invalid_argument("external lookup output buffer has the wrong size");
		std::lock_guard<std::mutex> lock(mutex_);
		if (failed_) throw std::runtime_error("external storage is unavailable after a failed batch");
		return lookup_unlocked(key, payload, payload_size);
	}

	bool MultiwayExternalRecordStore::lookup_unlocked(std::uint64_t key,
		std::uint8_t* payload, std::size_t payload_size) const
	{
		++stats_.lookups;
		auto& entry = cache_[external_cache_slot(key, cache_.size())];
		if (entry.valid && entry.key == key)
		{
			++stats_.cache_hits;
			std::copy_n(entry.payload.data(), payload_size, payload);
			return true;
		}
		++stats_.cache_misses;
		const auto started = std::chrono::steady_clock::now();
		for (auto run = runs_.rbegin(); run != runs_.rend(); ++run)
		{
			++stats_.disk_reads;
			if (!lookup_run(*run, key, payload, payload_size)) continue;
			auto& entry = cache_[external_cache_slot(key, cache_.size())];
			if (entry.valid) ++stats_.cache_evictions;
			entry.key = key; entry.valid = true;
			std::copy_n(payload, payload_size, entry.payload.data());
			stats_.read_nanoseconds += static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - started).count());
			return true;
		}
		stats_.read_nanoseconds += static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - started).count());
		return false;
	}

	bool MultiwayExternalRecordStore::lookup_run(const Run& run, std::uint64_t key,
		std::uint8_t* payload, std::size_t payload_size) const
	{
		std::ifstream index(run.index_path, std::ios::binary);
		if (!index) throw std::runtime_error("external index run cannot be opened");
		std::uint64_t first = 0U, last = run.record_count;
		while (first < last)
		{
			const auto middle = first + (last - first) / 2U;
			index.seekg(static_cast<std::streamoff>(middle * 8U));
			std::array<std::uint8_t, 8U> bytes{};
			index.read(reinterpret_cast<char*>(bytes.data()), bytes.size());
			if (!index) throw std::runtime_error("external index run is truncated");
			const auto candidate = read_u64(bytes.data());
			if (candidate < key) first = middle + 1U;
			else last = middle;
		}
		if (first == run.record_count) return false;
		index.clear(); index.seekg(static_cast<std::streamoff>(first * 8U));
		std::array<std::uint8_t, 8U> key_bytes{};
		index.read(reinterpret_cast<char*>(key_bytes.data()), key_bytes.size());
		if (!index) throw std::runtime_error("external index run is truncated");
		if (read_u64(key_bytes.data()) != key) return false;
		std::ifstream data(run.data_path, std::ios::binary);
		if (!data) throw std::runtime_error("external data segment cannot be opened");
		const auto width = payload_bytes_ + 8U;
		data.seekg(static_cast<std::streamoff>(first * width + 8U));
		data.read(reinterpret_cast<char*>(payload), static_cast<std::streamsize>(payload_size));
		if (!data) throw std::runtime_error("external data segment is truncated");
		return true;
	}

	void MultiwayExternalRecordStore::publish_manifest(const std::vector<Run>& runs, std::uint64_t next_generation, std::uint64_t last_batch_number)
	{
		std::vector<std::uint8_t> bytes(kManifestMagic.begin(), kManifestMagic.end());
		append_u32(bytes, kSchema); append_u32(bytes, static_cast<std::uint32_t>(payload_bytes_));
		append_u64(bytes, next_generation); append_u64(bytes, runs.size());
		append_u64(bytes, last_batch_number);
		for (const auto& run : runs)
		{
			append_u64(bytes, run.generation); append_u64(bytes, run.record_count); append_u64(bytes, run.content_hash);
		}
		auto hash = kHashOffset;
		for (const auto byte : bytes) texas::core::fingerprint::append_u8(hash, byte);
		append_u64(bytes, hash);
		const auto temp = config_.directory / "CURRENT.tmp";
		const auto destination = config_.directory / "CURRENT";
		if (std::filesystem::exists(temp)) throw std::runtime_error("external manifest temporary path already exists");
		std::ofstream output(temp, std::ios::binary | std::ios::trunc);
		if (!output) throw std::runtime_error("external manifest cannot be opened");
		output.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
		output.flush();
		if (!output) throw std::runtime_error("external manifest write failed");
		output.close();
		flush_file(temp);
		texas::core::publish_atomic_replace(temp, destination, "external manifest publish failed",
			texas::core::AtomicPublishFallbackPolicy::RejectNonAtomicFallback);
		flush_directory();
	}

	void MultiwayExternalRecordStore::load_manifest()
	{
		const auto path = config_.directory / "CURRENT";
		std::ifstream input(path, std::ios::binary);
		if (!input) throw std::runtime_error("external storage manifest cannot be opened");
		std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
		constexpr std::size_t fixed = 8U + 4U + 4U + 8U + 8U + 8U + 8U;
		if (bytes.size() < fixed || !std::equal(kManifestMagic.begin(), kManifestMagic.end(), bytes.begin()) || read_u32(bytes.data() + 8U) != kSchema || read_u32(bytes.data() + 12U) != payload_bytes_)
			throw std::invalid_argument("external storage manifest has invalid schema or payload width");
		const auto next_generation = read_u64(bytes.data() + 16U);
		const auto run_count = read_u64(bytes.data() + 24U);
		last_batch_number_ = read_u64(bytes.data() + 32U);
		if (run_count > (bytes.size() - fixed) / 24U || bytes.size() != fixed + run_count * 24U)
			throw std::invalid_argument("external storage manifest has invalid run count");
		auto hash = kHashOffset;
		for (std::size_t index = 0U; index + 8U < bytes.size(); ++index) texas::core::fingerprint::append_u8(hash, bytes[index]);
		if (hash != read_u64(bytes.data() + bytes.size() - 8U)) throw std::invalid_argument("external storage manifest checksum mismatch");
		std::vector<Run> runs;
		runs.reserve(static_cast<std::size_t>(run_count));
		std::size_t cursor = 40U;
		std::uint64_t total_records = 0U;
		std::uint64_t max_run_generation = 0U;
		for (std::uint64_t index = 0U; index < run_count; ++index)
		{
			Run run;
			run.generation = read_u64(bytes.data() + cursor); cursor += 8U;
			run.record_count = read_u64(bytes.data() + cursor); cursor += 8U;
			run.content_hash = read_u64(bytes.data() + cursor); cursor += 8U;
			if (run.generation <= max_run_generation || run.record_count == 0U)
				throw std::invalid_argument("external storage manifest run order is invalid");
			max_run_generation = run.generation;
			run.data_path = run_path(config_.directory, run.generation, ".dat");
			run.index_path = run_path(config_.directory, run.generation, ".idx");
			if (std::filesystem::file_size(run.data_path) != record_bytes(run.record_count, payload_bytes_) ||
				std::filesystem::file_size(run.index_path) != run.record_count * 8U)
				throw std::invalid_argument("external storage run length is invalid");
			if (run_hash(run) != run.content_hash) throw std::invalid_argument("external storage run checksum mismatch");
			total_records = checked_add(total_records, run.record_count);
			runs.push_back(std::move(run));
		}
		if (next_generation < max_run_generation) throw std::invalid_argument("external storage generation regressed");
		generation_ = next_generation;
		record_count_ = total_records;
		runs_.swap(runs);
	}

	void MultiwayExternalRecordStore::cleanup_orphans()
	{
		bool removed = false;
		for (const auto& entry : std::filesystem::directory_iterator(config_.directory))
		{
			if (!entry.is_regular_file()) throw std::invalid_argument("external storage directory contains a non-file entry");
			const auto name = entry.path().filename().string();
			if (name == "CURRENT" || name == "BATCH.wal") continue;
			bool keep = false;
			if (name.rfind("run_", 0U) == 0U)
			{
				const auto separator = name.find('.', 4U);
				if (separator == std::string::npos) throw std::invalid_argument("external storage run filename is invalid");
				const auto generation_text = name.substr(4U, separator - 4U);
				std::size_t consumed = 0U;
				std::uint64_t run_generation = 0U;
				try { run_generation = std::stoull(generation_text, &consumed, 10); }
				catch (...) { throw std::invalid_argument("external storage run generation is invalid"); }
				if (consumed != generation_text.size()) throw std::invalid_argument("external storage run generation is invalid");
				const auto suffix = name.substr(separator);
				if (suffix != ".dat" && suffix != ".idx" && suffix != ".dat.tmp" && suffix != ".idx.tmp")
					throw std::invalid_argument("external storage run filename has an invalid suffix");
				if ((suffix == ".dat" || suffix == ".idx") && run_generation <= generation_)
					for (const auto& run : runs_)
						if ((suffix == ".dat" && entry.path() == run.data_path) || (suffix == ".idx" && entry.path() == run.index_path)) keep = true;
			}
			else if (name != "CURRENT.tmp")
				throw std::invalid_argument("external storage directory contains an unrecognized file");
			if (!keep)
			{
				std::error_code error;
				std::filesystem::remove(entry.path(), error);
				if (error) throw std::runtime_error("external storage orphan cannot be removed");
				removed = true;
			}
		}
		if (removed) flush_directory();
	}

	void MultiwayExternalRecordStore::recover_wal()
	{
		const auto path = config_.directory / "BATCH.wal";
		if (!std::filesystem::exists(path)) return;
		const auto file_bytes = std::filesystem::file_size(path);
		if (file_bytes > config_.maximum_wal_bytes)
			throw std::invalid_argument("external storage WAL length is invalid");
		const auto discard_uncommitted = [&]() {
			std::error_code error;
			std::filesystem::remove(path, error);
			if (error) throw std::runtime_error("external storage uncommitted WAL cannot be removed");
			if (generation_ != std::numeric_limits<std::uint64_t>::max())
			{
				const auto next_generation = generation_ + 1U;
				for (const auto* suffix : { ".dat", ".idx", ".dat.tmp", ".idx.tmp" })
				{
					std::filesystem::remove(run_path(config_.directory, next_generation, suffix), error);
					if (error) throw std::runtime_error("external storage orphan generation cannot be removed");
				}
			}
			std::filesystem::remove(config_.directory / "CURRENT.tmp", error);
			if (error) throw std::runtime_error("external storage orphan manifest cannot be removed");
			flush_directory();
		};
		if (file_bytes < 32U)
		{
			discard_uncommitted();
			return;
		}
		std::ifstream input(path, std::ios::binary);
		if (!input) throw std::runtime_error("external storage WAL cannot be opened for recovery");
		std::vector<std::uint8_t> bytes(static_cast<std::size_t>(file_bytes));
		input.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
		if (!input || !std::equal(kWalMagic.begin(), kWalMagic.end(), bytes.begin()))
		{
			input.close(); discard_uncommitted(); return;
		}
		const auto batch_number = read_u64(bytes.data() + 8U);
		const auto merge_fingerprint = read_u64(bytes.data() + 16U);
		const auto count = read_u64(bytes.data() + 24U);
		input.close();
		if (batch_number <= last_batch_number_)
		{
			std::error_code error;
			std::filesystem::remove(path, error);
			if (error) throw std::runtime_error("external storage already committed WAL cannot be removed");
			flush_directory();
			return;
		}
		if (file_bytes < 48U)
		{
			discard_uncommitted();
			return;
		}
		const auto max_records = (config_.maximum_wal_bytes - 48U) / (payload_bytes_ + 8U);
		if (count == 0U || count > max_records)
		{
			discard_uncommitted();
			return;
		}
		const auto data_bytes = record_bytes(count, payload_bytes_);
		if (data_bytes > std::numeric_limits<std::size_t>::max() || data_bytes > file_bytes - 48U || file_bytes != 48U + data_bytes)
		{
			discard_uncommitted();
			return;
		}
		constexpr std::uint64_t commit_marker = 0x434f4d4d49545445ULL;
		const auto marker_offset = 32U + static_cast<std::size_t>(data_bytes);
		if (read_u64(bytes.data() + marker_offset) != commit_marker)
		{
			discard_uncommitted();
			return;
		}
		auto hash = kHashOffset;
		for (std::size_t index = 0U; index + 8U < bytes.size(); ++index)
			texas::core::fingerprint::append_u8(hash, bytes[index]);
		if (texas::core::fingerprint::finish(hash) != read_u64(bytes.data() + bytes.size() - 8U))
		{
			discard_uncommitted();
			return;
		}
		std::vector<MultiwayExternalRecord> records;
		records.reserve(static_cast<std::size_t>(count));
		std::size_t cursor = 32U;
		for (std::uint64_t index = 0U; index < count; ++index)
		{
			MultiwayExternalRecord record;
			record.key = read_u64(bytes.data() + cursor); cursor += 8U;
			record.payload.assign(bytes.begin() + static_cast<std::ptrdiff_t>(cursor),
				bytes.begin() + static_cast<std::ptrdiff_t>(cursor + payload_bytes_));
			cursor += payload_bytes_;
			if (!records.empty() && records.back().key >= record.key)
			{
				discard_uncommitted();
				return;
			}
			records.push_back(std::move(record));
		}
		if (records.empty()) throw std::invalid_argument("external storage WAL contains no records");
		if (generation_ == std::numeric_limits<std::uint64_t>::max()) throw std::overflow_error("external storage generation overflows");
		const auto next_generation = generation_ + 1U;
		for (const auto* suffix : { ".dat", ".idx", ".dat.tmp", ".idx.tmp" })
		{
			std::error_code error;
			std::filesystem::remove(run_path(config_.directory, next_generation, suffix), error);
			if (error) throw std::runtime_error("external storage orphan generation cannot be removed");
		}
		{
			std::error_code error;
			std::filesystem::remove(config_.directory / "CURRENT.tmp", error);
			if (error) throw std::runtime_error("external storage orphan manifest cannot be removed");
		}
		std::error_code remove_error;
		std::filesystem::remove(path, remove_error);
		if (remove_error) throw std::runtime_error("external storage recovered WAL cannot be removed");
		flush_directory();
		append_batch(batch_number, merge_fingerprint, records);
	}

	std::uint64_t MultiwayExternalRecordStore::run_hash(const Run& run) const
	{
		auto hash = kHashOffset;
		append_file_hash(hash, run.index_path);
		append_file_hash(hash, run.data_path);
		return texas::core::fingerprint::finish(hash);
	}

	void MultiwayExternalRecordStore::compact(bool retain_input_runs)
	{
		std::lock_guard<std::mutex> lock(mutex_);
		if (runs_.size() < config_.compaction_fan_in) return;
		if (generation_ == std::numeric_limits<std::uint64_t>::max()) throw std::overflow_error("external storage generation overflows");
		cleanup_orphans();
		const auto input_count = static_cast<std::size_t>(config_.compaction_fan_in);
		const auto input_start = runs_.size() - input_count;
		std::uint64_t total_records = 0U;
		std::uint64_t output_bytes = 0U;
		for (std::size_t index = 0U; index < input_count; ++index)
		{
			total_records = checked_add(total_records, runs_[input_start + index].record_count);
			output_bytes = checked_add(output_bytes, record_bytes(runs_[input_start + index].record_count, payload_bytes_));
		}
		check_disk_reserve(checked_add(output_bytes, record_bytes(total_records, 0U)));
		const auto next_generation = generation_ + 1U;
		const auto data_path = run_path(config_.directory, next_generation, ".dat");
		const auto index_path = run_path(config_.directory, next_generation, ".idx");
		auto data_temp = data_path; data_temp += ".tmp";
		auto index_temp = index_path; index_temp += ".tmp";
		struct Cursor { std::ifstream index; std::ifstream data; std::uint64_t generation = 0U; std::uint64_t count = 0U; std::uint64_t position = 0U; std::uint64_t key = 0U; std::vector<std::uint8_t> payload; };
		std::vector<Cursor> cursors(input_count);
		struct Item { std::uint64_t key; std::uint64_t generation; std::size_t cursor; };
		const auto greater = [](const Item& left, const Item& right) {
			return left.key > right.key || (left.key == right.key && (left.generation < right.generation ||
				(left.generation == right.generation && left.cursor > right.cursor)));
		};
		std::priority_queue<Item, std::vector<Item>, decltype(greater)> queue(greater);
		std::vector<std::size_t> consumed;
		consumed.reserve(input_count);
		const auto read_next = [&](std::size_t index) {
			auto& cursor = cursors[index];
			std::array<std::uint8_t, 8U> bytes{};
			cursor.index.read(reinterpret_cast<char*>(bytes.data()), bytes.size());
			if (!cursor.index) return false;
			cursor.key = read_u64(bytes.data());
			cursor.data.seekg(static_cast<std::streamoff>(cursor.position * (payload_bytes_ + 8U)));
			std::array<std::uint8_t, 8U> data_key_bytes{};
			cursor.data.read(reinterpret_cast<char*>(data_key_bytes.data()), data_key_bytes.size());
			if (!cursor.data || read_u64(data_key_bytes.data()) != cursor.key)
				throw std::invalid_argument("external compaction index and data keys disagree");
			cursor.data.read(reinterpret_cast<char*>(cursor.payload.data()), static_cast<std::streamsize>(payload_bytes_));
			if (!cursor.data) throw std::runtime_error("external compaction input is truncated");
			return true;
		};
		for (std::size_t index = 0U; index < input_count; ++index)
		{
			cursors[index].index.open(runs_[input_start + index].index_path, std::ios::binary);
			cursors[index].data.open(runs_[input_start + index].data_path, std::ios::binary);
			cursors[index].generation = runs_[input_start + index].generation;
			cursors[index].count = runs_[input_start + index].record_count;
			cursors[index].payload.resize(payload_bytes_);
			if (!cursors[index].index || !cursors[index].data || !read_next(index))
				throw std::runtime_error("external compaction input cannot be read");
			queue.push({ cursors[index].key, cursors[index].generation, index });
		}
		check_disk_reserve(checked_add(output_bytes, record_bytes(total_records, 0U)));
		std::ofstream data(data_temp, std::ios::binary | std::ios::trunc);
		std::ofstream index(index_temp, std::ios::binary | std::ios::trunc);
		if (!data || !index) throw std::runtime_error("external compaction output cannot be opened");
		std::uint64_t compacted_count = 0U;
		while (!queue.empty())
		{
			const auto item = queue.top(); queue.pop();
			consumed.clear();
			consumed.push_back(item.cursor);
			while (!queue.empty() && queue.top().key == item.key)
			{
				consumed.push_back(queue.top().cursor);
				queue.pop();
			}
			const auto& newest = cursors[item.cursor];
			write_u64(data, item.key); data.write(reinterpret_cast<const char*>(newest.payload.data()), static_cast<std::streamsize>(payload_bytes_));
			write_u64(index, item.key);
			++compacted_count;
			for (const auto cursor_index : consumed)
			{
				auto& cursor = cursors[cursor_index];
				++cursor.position;
				if (cursor.position < cursor.count)
				{
					if (!read_next(cursor_index)) throw std::runtime_error("external compaction input ended early");
					queue.push({ cursor.key, cursor.generation, cursor_index });
				}
			}
		}
		data.flush(); index.flush();
		if (!data || !index) throw std::runtime_error("external compaction write failed");
		data.close(); index.close(); flush_file(data_temp); flush_file(index_temp);
		texas::core::publish_atomic_replace(data_temp, data_path, "external compacted segment publish failed", texas::core::AtomicPublishFallbackPolicy::RejectNonAtomicFallback);
		texas::core::publish_atomic_replace(index_temp, index_path, "external compacted index publish failed", texas::core::AtomicPublishFallbackPolicy::RejectNonAtomicFallback);
		Run merged{ next_generation, compacted_count, 0U, data_path, index_path };
		merged.content_hash = run_hash(merged);
		auto next_runs = runs_;
		next_runs.erase(next_runs.begin() + static_cast<std::ptrdiff_t>(input_start), next_runs.end());
		next_runs.push_back(merged);
		publish_manifest(next_runs, next_generation, last_batch_number_);
		flush_directory();
		if (!retain_input_runs)
			for (std::size_t index = 0U; index < input_count; ++index)
			{
				std::error_code ignored;
				std::filesystem::remove(runs_[input_start + index].data_path, ignored);
				std::filesystem::remove(runs_[input_start + index].index_path, ignored);
			}
		runs_.swap(next_runs);
		generation_ = next_generation;
		record_count_ = record_count_ - total_records + compacted_count;
		flush_directory();
	}

	MultiwayExternalPublicCatalog::MultiwayExternalPublicCatalog(
		MultiwayExternalStorageConfig config, std::size_t maximum_ancestry_depth)
		: store_(std::move(config), external_public_catalog_record_size()),
		  maximum_ancestry_depth_(maximum_ancestry_depth)
	{
		if (maximum_ancestry_depth_ == 0U)
			throw std::invalid_argument("external public catalog requires a positive ancestry depth bound");
	}

	void MultiwayExternalPublicCatalog::append_batch(std::uint64_t batch_number,
		std::uint64_t merge_fingerprint, const std::vector<MultiwayPackedPublicNode>& nodes)
	{
		if (nodes.empty()) return;
		std::vector<MultiwayExternalRecord> records;
		records.reserve(nodes.size());
		std::vector<std::uint8_t> node_bytes;
		std::vector<std::uint8_t> payload;
		for (std::size_t index = 0U; index < nodes.size(); ++index)
		{
			if (nodes[index].id.value == 0U ||
				nodes[index].legal_menu_handle != 0U ||
				(index != 0U && nodes[index - 1U].id.value >= nodes[index].id.value))
				throw std::invalid_argument("external menu-less catalog batch requires sorted unique states with menu handle zero");
			if (contains(nodes[index].id))
				throw std::invalid_argument("external public catalog cannot append an existing state id");
			encode_external_public_node(nodes[index], node_bytes);
			payload = node_bytes;
			append_u32(payload, kCatalogMenuMagic);
			append_u32(payload, kCatalogMenuSchema);
			append_u32(payload, 0U);
			payload.resize(external_public_catalog_record_size() - kCatalogMenuChecksumBytes, 0U);
			auto checksum = kHashOffset;
			for (std::size_t byte = node_bytes.size(); byte < payload.size(); ++byte)
				texas::core::fingerprint::append_u8(checksum, payload[byte]);
			append_u64(payload, texas::core::fingerprint::finish(checksum));
			records.push_back({ nodes[index].id.value, payload });
		}
		store_.append_batch(batch_number, merge_fingerprint, records);
	}

	void MultiwayExternalPublicCatalog::append_batch(std::uint64_t batch_number,
		std::uint64_t merge_fingerprint, const std::vector<MultiwayPackedPublicNode>& nodes,
		const std::vector<MultiwayPublicStateDescriptor>& descriptors)
	{
		if (nodes.size() != descriptors.size())
			throw std::invalid_argument("external public catalog descriptors do not match packed nodes");
		if (nodes.empty()) return;
		std::vector<MultiwayExternalRecord> records;
		records.reserve(nodes.size());
		std::vector<std::uint8_t> node_bytes;
		std::vector<std::uint8_t> payload;
		for (std::size_t index = 0U; index < nodes.size(); ++index)
		{
			const auto& node = nodes[index];
			const auto& descriptor = descriptors[index];
			if (node.id.value == 0U || descriptor.id != node.id ||
				(index != 0U && nodes[index - 1U].id.value >= node.id.value) ||
				descriptor.legal_actions.size() > MULTIWAY_MAX_ABSTRACTED_ACTIONS)
				throw std::invalid_argument("external public catalog batch has invalid identity, order, or menu size");
			if (contains(node.id))
				throw std::invalid_argument("external public catalog cannot append an existing state id");
			for (std::size_t action = 0U; action < descriptor.legal_actions.size(); ++action)
			{
				const auto& item = descriptor.legal_actions[action];
				if (static_cast<std::uint8_t>(item.action) > static_cast<std::uint8_t>(MultiwayAction::AllIn) ||
					item.action_index != action || item.target_street_contribution < 0 || item.action_menu_id == 0U ||
					(action != 0U && (item.action_menu_id != descriptor.legal_actions.front().action_menu_id ||
						static_cast<std::uint8_t>(item.action) < static_cast<std::uint8_t>(descriptor.legal_actions[action - 1U].action) ||
						(item.action == descriptor.legal_actions[action - 1U].action &&
							item.target_street_contribution <= descriptor.legal_actions[action - 1U].target_street_contribution))))
					throw std::invalid_argument("external public catalog action menu is not canonical");
			}
			auto external_node = node;
			external_node.legal_menu_handle = 0U;
			encode_external_public_node(external_node, node_bytes);
			payload = node_bytes;
			const auto menu_start = payload.size();
			append_u32(payload, kCatalogMenuMagic);
			append_u32(payload, kCatalogMenuSchema);
			append_u32(payload, static_cast<std::uint32_t>(descriptor.legal_actions.size()));
			for (std::size_t action = 0U; action < MULTIWAY_MAX_ABSTRACTED_ACTIONS; ++action)
			{
				if (action < descriptor.legal_actions.size())
				{
					const auto& item = descriptor.legal_actions[action];
					payload.push_back(static_cast<std::uint8_t>(item.action));
					append_u32(payload, item.action_index);
					append_i32(payload, item.target_street_contribution);
					append_u64(payload, item.action_menu_id);
				}
				else
					payload.insert(payload.end(), kCatalogMenuActionBytes, 0U);
			}
			auto checksum = kHashOffset;
			for (std::size_t byte = menu_start; byte < payload.size(); ++byte)
				texas::core::fingerprint::append_u8(checksum, payload[byte]);
			append_u64(payload, texas::core::fingerprint::finish(checksum));
			if (payload.size() != external_public_catalog_record_size())
				throw std::logic_error("external catalog record width changed without a schema update");
			records.push_back({ node.id.value, payload });
		}
		store_.append_batch(batch_number, merge_fingerprint, records);
	}

	bool MultiwayExternalPublicCatalog::lookup(MultiwayPublicStateId id,
		MultiwayPackedPublicNode& output) const
	{
		if (id.value == 0U) return false;
		std::vector<std::uint8_t> payload;
		if (!store_.lookup(id.value, payload)) return false;
		return payload.size() == external_public_catalog_record_size() &&
			decode_external_public_node(payload.data(), external_public_node_record_size(), output) && output.id == id;
	}

	bool MultiwayExternalPublicCatalog::lookup_menu(MultiwayPublicStateId id,
		MultiwayActionDescriptor* output, std::size_t output_capacity, std::size_t& action_count) const
	{
		action_count = 0U;
		if (id.value == 0U) return false;
		std::vector<std::uint8_t> payload;
		if (!store_.lookup(id.value, payload) || payload.size() != external_public_catalog_record_size()) return false;
		const auto* menu = payload.data() + external_public_node_record_size();
		if (read_u32(menu) != kCatalogMenuMagic || read_u32(menu + 4U) != kCatalogMenuSchema)
			throw std::invalid_argument("external public catalog menu header is invalid");
		const auto count = read_u32(menu + 8U);
		if (count > MULTIWAY_MAX_ABSTRACTED_ACTIONS || count > output_capacity || (count != 0U && output == nullptr))
			throw std::invalid_argument("external public catalog menu output or count is invalid");
		auto checksum = kHashOffset;
		for (std::size_t byte = 0U; byte < kCatalogMenuBytes - kCatalogMenuChecksumBytes; ++byte)
			texas::core::fingerprint::append_u8(checksum, menu[byte]);
		if (texas::core::fingerprint::finish(checksum) !=
			read_u64(menu + kCatalogMenuBytes - kCatalogMenuChecksumBytes))
			throw std::invalid_argument("external public catalog menu checksum is invalid");
		const auto* action_bytes = menu + kCatalogMenuHeaderBytes;
		std::uint64_t menu_id = 0U;
		for (std::size_t index = 0U; index < MULTIWAY_MAX_ABSTRACTED_ACTIONS; ++index)
		{
			const auto* item = action_bytes + index * kCatalogMenuActionBytes;
			if (index >= count)
			{
				for (std::size_t byte = 0U; byte < kCatalogMenuActionBytes; ++byte)
					if (item[byte] != 0U) throw std::invalid_argument("external public catalog menu padding is invalid");
				continue;
			}
			const auto action_kind = item[0];
			const auto action_index = read_u32(item + 1U);
			const auto target = read_i32(item + 5U);
			const auto current_menu_id = read_u64(item + 9U);
			if (action_kind > static_cast<std::uint8_t>(MultiwayAction::AllIn) || action_index != index ||
				target < 0 || current_menu_id == 0U || (index != 0U && current_menu_id != menu_id))
				throw std::invalid_argument("external public catalog action record is invalid");
			menu_id = current_menu_id;
			output[index] = { static_cast<MultiwayAction>(action_kind), action_index, target, current_menu_id };
		}
		action_count = count;
		return true;
	}

	bool MultiwayExternalPublicCatalog::contains(MultiwayPublicStateId id) const
	{
		MultiwayPackedPublicNode node{};
		return lookup(id, node);
	}

	bool MultiwayExternalPublicCatalog::reconstruct_chain(MultiwayPublicStateId id,
		std::vector<MultiwayPackedPublicNode>& parent_first_output) const
	{
		parent_first_output.clear();
		parent_first_output.reserve(maximum_ancestry_depth_);
		MultiwayPublicStateId current = id;
		while (current.value != 0U)
		{
			if (parent_first_output.size() == maximum_ancestry_depth_)
			{
				parent_first_output.clear();
				return false;
			}
			MultiwayPackedPublicNode node{};
			if (!lookup(current, node))
			{
				parent_first_output.clear();
				return false;
			}
			current = node.parent_id;
			parent_first_output.push_back(node);
		}
		std::reverse(parent_first_output.begin(), parent_first_output.end());
		return !parent_first_output.empty();
	}

	bool MultiwayExternalPublicCatalog::reconstruct(MultiwayPublicStateId id,
		MultiwayPublicStateDescriptor& output) const
	{
		std::vector<MultiwayPackedPublicNode> packed_chain;
		if (!reconstruct_chain(id, packed_chain)) return false;
		const auto& packed = packed_chain.back();
		MultiwayPublicStateDescriptor descriptor{};
		descriptor.id = packed.id;
		descriptor.parent_id = packed.parent_id;
		descriptor.canonical_history_id = packed.canonical_history_id;
		descriptor.board_runout = packed.board_runout;
		descriptor.board.assign(packed.board.begin(), packed.board.begin() + packed.board_count);
		descriptor.incoming_edge.kind = packed.incoming_kind;
		descriptor.incoming_edge.action = packed.incoming_action;
		descriptor.incoming_edge.dealt_card = packed.dealt_card;
		descriptor.incoming_edge.dealt_cards.assign(
			packed.dealt_cards.begin(), packed.dealt_cards.begin() + packed.dealt_card_count);
		descriptor.incoming_edge.transition_board.assign(
			packed.transition_board.begin(), packed.transition_board.begin() + packed.transition_board_count);
		auto& betting = descriptor.betting;
		betting.current_player = packed.betting.current_player;
		betting.last_aggressor = packed.betting.last_aggressor;
		betting.current_bet = packed.betting.current_bet;
		betting.last_full_raise_size = packed.betting.last_full_raise_size;
		betting.big_blind = packed.betting.big_blind;
		betting.street = packed.betting.street;
		const auto seat_count = packed.betting.seat_count;
		betting.stacks.assign(packed.betting.stacks.begin(), packed.betting.stacks.begin() + seat_count);
		betting.contributions.assign(packed.betting.contributions.begin(), packed.betting.contributions.begin() + seat_count);
		betting.street_contributions.assign(packed.betting.street_contributions.begin(), packed.betting.street_contributions.begin() + seat_count);
		betting.bet_faced_when_acted.assign(
			packed.betting.bet_faced_when_acted.begin(), packed.betting.bet_faced_when_acted.begin() + seat_count);
		betting.folded.resize(seat_count);
		betting.all_in.resize(seat_count);
		betting.may_raise.resize(seat_count);
		betting.pending.resize(seat_count);
		betting.has_acted.resize(seat_count);
		for (std::size_t seat = 0U; seat < seat_count; ++seat)
		{
			betting.folded[seat] = packed.betting.folded[seat] != 0U;
			betting.all_in[seat] = packed.betting.all_in[seat] != 0U;
			betting.may_raise[seat] = packed.betting.may_raise[seat] != 0U;
			betting.pending[seat] = packed.betting.pending[seat] != 0U;
			betting.has_acted[seat] = packed.betting.has_acted[seat] != 0U;
		}
		descriptor.legal_actions.resize(MULTIWAY_MAX_ABSTRACTED_ACTIONS);
		std::size_t action_count = 0U;
		if (!lookup_menu(packed.id, descriptor.legal_actions.data(), descriptor.legal_actions.size(), action_count))
			return false;
		descriptor.legal_actions.resize(action_count);
		for (std::size_t index = 1U; index < packed_chain.size(); ++index)
		{
			const auto& ancestor = packed_chain[index];
			if (ancestor.incoming_kind == MultiwayPublicParentEdgeKind::BettingAction)
			{
				auto action = ancestor.incoming_action;
				action.action_index = 0U;
				descriptor.history.push_back({ ancestor.incoming_actor, action });
			}
		}
		output = std::move(descriptor);
		return true;
	}

	MultiwayExternalRowIndex::MultiwayExternalRowIndex(
		MultiwayExternalStorageConfig config, std::uint32_t page_width)
		: store_(std::move(config), 8U + 6U * 24U), page_width_(page_width)
	{
		if (page_width_ == 0U)
			throw std::invalid_argument("external row index requires a positive page width");
		std::vector<std::uint8_t> allocator;
		if (store_.lookup(0U, allocator))
		{
			if (allocator.size() != 8U + 6U * 24U || read_u32(allocator.data()) != 0x3152574dU ||
				read_u32(allocator.data() + 4U) != 1U || read_u64(allocator.data() + 8U) == 0U)
				throw std::invalid_argument("external row page-id allocator record is invalid");
			next_page_id_ = read_u64(allocator.data() + 8U);
		}
		else if (store_.record_count() != 0U)
			throw std::invalid_argument("external row index is missing its page-id allocator record");
	}

	void MultiwayExternalRowIndex::append_batch(std::uint64_t batch_number,
		std::uint64_t merge_fingerprint, const std::vector<MultiwaySparseRowShape>& rows,
		std::uint64_t& next_page_id)
	{
		if (rows.empty()) return;
		constexpr std::uint64_t row_payload_bytes = 8U + 6U * 24U;
		const auto wal_limit = store_.maximum_batch_bytes();
		if (wal_limit < 48U + 2U * (8U + row_payload_bytes) ||
			rows.size() > (wal_limit - 48U) / (8U + row_payload_bytes) - 1U)
			throw std::length_error("external row admission exceeds its bounded WAL batch");
		constexpr std::uint32_t row_magic = 0x3152574dU;
		constexpr std::uint32_t row_schema = 1U;
		std::vector<std::size_t> order(rows.size());
		for (std::size_t index = 0U; index < order.size(); ++index) order[index] = index;
		std::sort(order.begin(), order.end(), [&rows](std::size_t left, std::size_t right) {
			const auto& a = rows[left].infoset;
			const auto& b = rows[right].infoset;
			return a.public_state.value != b.public_state.value
				? a.public_state.value < b.public_state.value
				: a.seat < b.seat;
		});
		std::vector<MultiwayExternalRecord> records;
		records.reserve(rows.size() + 1U);
		if (next_page_id != next_page_id_)
			throw std::invalid_argument("external row page-id allocator does not match the durable generation");
		auto candidate_next_page_id = next_page_id;
		std::size_t cursor = 0U;
		while (cursor < order.size())
		{
			const auto& first_row = rows[order[cursor]];
			const auto state_id = first_row.infoset.public_state.value;
			if (state_id == 0U) throw std::invalid_argument("external row state id is invalid");
			std::vector<std::uint8_t> payload(8U + 6U * 24U, 0U);
			if (!store_.lookup(state_id, payload))
		{
				for (unsigned shift = 0U; shift < 32U; shift += 8U)
				{
					payload[shift / 8U] = static_cast<std::uint8_t>(row_magic >> shift);
					payload[4U + shift / 8U] = static_cast<std::uint8_t>(row_schema >> shift);
				}
			}
			else if (read_u32(payload.data()) != row_magic || read_u32(payload.data() + 4U) != row_schema)
				throw std::invalid_argument("external row metadata record schema is invalid");
			std::int32_t prior_seat = -1;
			while (cursor < order.size() && rows[order[cursor]].infoset.public_state.value == state_id)
			{
				const auto& row = rows[order[cursor]];
				const auto seat = row.infoset.seat;
				if (seat < 0 || seat >= 6 || seat <= prior_seat || row.bucket_count == 0U ||
					row.action_count == 0U || row.action_count > MULTIWAY_MAX_ABSTRACTED_ACTIONS)
					throw std::invalid_argument("external row metadata batch has invalid seat, order, or shape");
				prior_seat = seat;
				const auto slot = 8U + static_cast<std::size_t>(seat) * 24U;
				if (payload[slot] != 0U)
					throw std::invalid_argument("external row index cannot append an existing infoset");
				const auto page_count64 = (static_cast<std::uint64_t>(row.bucket_count) + page_width_ - 1U) / page_width_;
				if (page_count64 > std::numeric_limits<std::uint32_t>::max() || candidate_next_page_id == 0U ||
					page_count64 > std::numeric_limits<std::uint64_t>::max() - candidate_next_page_id)
					throw std::overflow_error("external row page identity overflows");
				payload[slot] = 1U;
				payload[slot + 1U] = row.action_count;
				for (unsigned shift = 0U; shift < 32U; shift += 8U)
					payload[slot + 4U + shift / 8U] = static_cast<std::uint8_t>(row.bucket_count >> shift);
			for (unsigned shift = 0U; shift < 64U; shift += 8U)
				payload[slot + 8U + shift / 8U] = static_cast<std::uint8_t>(candidate_next_page_id >> shift);
			const auto page_count = static_cast<std::uint32_t>(page_count64);
			for (unsigned shift = 0U; shift < 32U; shift += 8U)
				payload[slot + 16U + shift / 8U] = static_cast<std::uint8_t>(page_count >> shift);
			candidate_next_page_id = checked_add(candidate_next_page_id, page_count64);
			++cursor;
			}
			records.push_back({ state_id, std::move(payload) });
		}
		std::vector<std::uint8_t> allocator_payload(8U + 6U * 24U, 0U);
		for (unsigned shift = 0U; shift < 32U; shift += 8U)
		{
			allocator_payload[shift / 8U] = static_cast<std::uint8_t>(row_magic >> shift);
			allocator_payload[4U + shift / 8U] = static_cast<std::uint8_t>(row_schema >> shift);
		}
		for (unsigned shift = 0U; shift < 64U; shift += 8U)
			allocator_payload[8U + shift / 8U] = static_cast<std::uint8_t>(candidate_next_page_id >> shift);
		records.insert(records.begin(), { 0U, std::move(allocator_payload) });
		store_.append_batch(batch_number, merge_fingerprint, records);
		next_page_id = candidate_next_page_id;
		next_page_id_ = candidate_next_page_id;
	}

	bool MultiwayExternalRowIndex::lookup(MultiwayPublicStateId state,
		std::int32_t seat, MultiwayExternalRowMetadata& output) const
	{
		if (state.value == 0U || seat < 0 || seat >= 6) return false;
		std::array<std::uint8_t, 8U + 6U * 24U> payload{};
		if (!store_.lookup(state.value, payload.data(), payload.size())) return false;
		if (payload.size() != 8U + 6U * 24U || read_u32(payload.data()) != 0x3152574dU ||
			read_u32(payload.data() + 4U) != 1U)
			throw std::invalid_argument("external row metadata record schema is invalid");
		const auto slot = 8U + static_cast<std::size_t>(seat) * 24U;
		if (payload[slot] == 0U) return false;
		if (payload[slot] != 1U || payload[slot + 2U] != 0U || payload[slot + 3U] != 0U ||
			read_u32(payload.data() + slot + 20U) != 0U)
			throw std::invalid_argument("external row metadata flags or reserved fields are invalid");
		output.action_count = payload[slot + 1U];
		output.bucket_count = read_u32(payload.data() + slot + 4U);
		output.first_page_id = read_u64(payload.data() + slot + 8U);
		output.page_count = read_u32(payload.data() + slot + 16U);
		if (output.action_count == 0U || output.bucket_count == 0U || output.first_page_id == 0U || output.page_count == 0U)
			throw std::invalid_argument("external row metadata contains an invalid shape");
		const auto expected_pages = (static_cast<std::uint64_t>(output.bucket_count) + page_width_ - 1U) / page_width_;
		if (output.page_count != expected_pages ||
			output.page_count > std::numeric_limits<std::uint64_t>::max() - output.first_page_id)
			throw std::invalid_argument("external row metadata page range is inconsistent");
		return true;
	}

	bool MultiwayExternalRowIndex::locate_bucket(MultiwayPublicStateId state,
		std::int32_t seat, std::uint32_t bucket, MultiwayExternalRowPage& output) const
	{
		MultiwayExternalRowMetadata metadata{};
		if (!lookup(state, seat, metadata)) return false;
		MultiwayExternalRowBinding binding;
		binding.owner_ = this;
		binding.state_id_ = state.value;
		binding.seat_ = seat;
		binding.row_ = metadata;
		return locate_bucket(binding, bucket, output);
	}

	bool MultiwayExternalRowIndex::locate_bucket(const MultiwayExternalRowBinding& binding,
		std::uint32_t bucket, MultiwayExternalRowPage& output) const
	{
		const auto& row = binding.row_;
		if (binding.owner_ != this || binding.state_id_ == 0U || binding.seat_ < 0 || binding.seat_ >= 6 ||
			row.bucket_count == 0U || row.action_count == 0U || row.first_page_id == 0U ||
			row.page_count == 0U || bucket >= row.bucket_count)
			return false;
		const auto page_index = bucket / page_width_;
		if (page_index >= row.page_count || page_index > std::numeric_limits<std::uint64_t>::max() - row.first_page_id)
			throw std::invalid_argument("external row metadata does not cover its bucket range");
		const auto first_bucket = static_cast<std::uint64_t>(page_index) * page_width_;
		const auto page_bucket_count = std::min<std::uint64_t>(page_width_,
			static_cast<std::uint64_t>(row.bucket_count) - first_bucket);
		output = { row.first_page_id + page_index, page_index,
			static_cast<std::uint32_t>(page_bucket_count), row.action_count };
		return true;
	}

	std::size_t MultiwayExternalCompactPages::page_payload_size() const noexcept
	{
		return kPageRecordHeaderBytes +
			MULTIWAY_MAX_ABSTRACTED_ACTIONS * static_cast<std::size_t>(page_width_) * 12U +
			kPageRecordChecksumBytes;
	}

	MultiwayExternalCompactPages::MultiwayExternalCompactPages(
		MultiwayExternalStorageConfig config, std::uint32_t page_width,
		std::uint64_t cache_bytes)
		: page_width_(page_width)
	{
		if (page_width_ == 0U || cache_bytes > std::numeric_limits<std::size_t>::max())
			throw std::invalid_argument("external compact pages require bounded positive dimensions");
		const auto payload = kPageRecordHeaderBytes +
			MULTIWAY_MAX_ABSTRACTED_ACTIONS * static_cast<std::uint64_t>(page_width_) * 12U +
			kPageRecordChecksumBytes;
		if (payload > std::numeric_limits<std::size_t>::max() || payload > std::numeric_limits<std::uint32_t>::max())
			throw std::length_error("external compact page payload width overflows");
		const auto store_cache_bytes = payload + 1024U;
		if (cache_bytes <= store_cache_bytes + 2U * payload)
			throw std::invalid_argument("external compact page cache cannot hold one working page");
		config.cache_bytes = store_cache_bytes;
		store_cache_budget_bytes_ = store_cache_bytes;
		store_ = std::make_unique<MultiwayExternalRecordStore>(config, static_cast<std::size_t>(payload));
		lookup_scratch_.resize(static_cast<std::size_t>(payload));
		encode_scratch_.reserve(static_cast<std::size_t>(payload));
		const auto cells = MULTIWAY_MAX_ABSTRACTED_ACTIONS * static_cast<std::size_t>(page_width_);
		constexpr auto bytes_per_cell = 2U * sizeof(std::int32_t) + 2U * sizeof(std::uint64_t);
		if (cells > (std::numeric_limits<std::size_t>::max() - sizeof(Page)) / bytes_per_cell)
			throw std::length_error("external compact page cache cell arena overflows size_t");
		const auto page_memory = sizeof(Page) + cells * bytes_per_cell;
		const auto scratch_bytes = lookup_scratch_.capacity() + encode_scratch_.capacity();
		if (cache_bytes <= store_cache_bytes + scratch_bytes)
			throw std::invalid_argument("external compact page cache cannot hold fixed scratch");
		const auto page_budget = cache_bytes - store_cache_bytes - scratch_bytes;
		auto capacity = page_budget / page_memory;
		const auto record_width = payload + 8U;
		capacity = std::min<std::uint64_t>(capacity, (config.maximum_wal_bytes - 48U) / record_width);
		capacity = std::min<std::uint64_t>(capacity, config.segment_target_bytes / record_width);
		if (capacity == 0U || capacity > std::numeric_limits<std::size_t>::max())
			throw std::invalid_argument("external compact page cache cannot hold a committable page");
		pages_.resize(static_cast<std::size_t>(capacity));
		for (auto& page : pages_)
		{
			page.regrets.resize(cells);
			page.strategy_masses.resize(cells);
			page.before_regrets.resize(cells);
			page.before_strategy_masses.resize(cells);
		}
		if (memory_bytes() > cache_bytes)
			throw std::length_error("external compact page allocation exceeds its configured cache bound");
	}

	MultiwayExternalCompactPages::Page* MultiwayExternalCompactPages::find(std::uint64_t page_id) noexcept
	{
		for (auto& page : pages_)
			if (page.valid && page.page_id == page_id) return &page;
		return nullptr;
	}

	void MultiwayExternalCompactPages::decode_page(const std::uint8_t* bytes, Page& page) const
	{
		if (read_u32(bytes) != kPageRecordMagic || read_u32(bytes + 4U) != kPageRecordSchema)
			throw std::invalid_argument("external compact page schema is invalid");
		auto hash = kHashOffset;
		for (std::size_t index = 0U; index + kPageRecordChecksumBytes < page_payload_size(); ++index)
			texas::core::fingerprint::append_u8(hash, bytes[index]);
		if (texas::core::fingerprint::finish(hash) != read_u64(bytes + page_payload_size() - kPageRecordChecksumBytes))
			throw std::invalid_argument("external compact page checksum is invalid");
		std::size_t cursor = 8U;
		page.page_id = read_u64(bytes + cursor); cursor += 8U;
		page.state_id = read_u64(bytes + cursor); cursor += 8U;
		page.seat = read_i32(bytes + cursor); cursor += 4U;
		page.page_index = read_u32(bytes + cursor); cursor += 4U;
		page.bucket_count = read_u32(bytes + cursor); cursor += 4U;
		page.action_count = bytes[cursor++];
		if (bytes[cursor] != 0U || bytes[cursor + 1U] != 0U || bytes[cursor + 2U] != 0U)
			throw std::invalid_argument("external compact page reserved bytes are invalid");
		cursor += 3U;
		for (auto& value : page.regrets) { value = read_i32(bytes + cursor); cursor += 4U; }
		for (auto& value : page.strategy_masses) { value = read_u64(bytes + cursor); cursor += 8U; }
		if (cursor + kPageRecordChecksumBytes != page_payload_size() || page.page_id == 0U ||
			page.state_id == 0U || page.seat < 0 || page.seat >= 6 || page.bucket_count == 0U ||
			page.bucket_count > page_width_ || page.action_count == 0U ||
			page.action_count > MULTIWAY_MAX_ABSTRACTED_ACTIONS)
			throw std::invalid_argument("external compact page identity or shape is invalid");
	}

	void MultiwayExternalCompactPages::encode_page(const Page& page,
		std::vector<std::uint8_t>& bytes) const
	{
		bytes.clear();
		bytes.reserve(page_payload_size());
		append_u32(bytes, kPageRecordMagic);
		append_u32(bytes, kPageRecordSchema);
		append_u64(bytes, page.page_id);
		append_u64(bytes, page.state_id);
		append_i32(bytes, page.seat);
		append_u32(bytes, page.page_index);
		append_u32(bytes, page.bucket_count);
		bytes.push_back(page.action_count);
		bytes.insert(bytes.end(), 3U, 0U);
		for (const auto value : page.regrets) append_i32(bytes, value);
		for (const auto value : page.strategy_masses) append_u64(bytes, value);
		auto hash = kHashOffset;
		for (const auto byte : bytes) texas::core::fingerprint::append_u8(hash, byte);
		append_u64(bytes, texas::core::fingerprint::finish(hash));
		if (bytes.size() != page_payload_size())
			throw std::logic_error("external compact page codec width changed without a schema update");
	}

	void MultiwayExternalCompactPages::begin_batch(std::uint64_t batch_number,
		std::uint64_t merge_fingerprint)
	{
		std::lock_guard<std::mutex> lock(mutex_);
		if (batch_open_ || batch_number == 0U)
			throw std::logic_error("external compact page batch state is invalid");
		for (const auto& page : pages_)
			if (page.dirty || page.pin_count != 0U)
				throw std::logic_error("external compact page cache is not at a clean batch boundary");
		batch_number_ = batch_number;
		merge_fingerprint_ = merge_fingerprint;
		batch_open_ = true;
	}

	MultiwayExternalCompactPages::PageView MultiwayExternalCompactPages::pin(
		std::uint64_t page_id, MultiwayPublicStateId state, std::int32_t seat,
		std::uint32_t page_index, std::uint32_t bucket_count, std::uint8_t action_count)
	{
		std::lock_guard<std::mutex> lock(mutex_);
		if (page_id == 0U || state.value == 0U || seat < 0 || seat >= 6 || bucket_count == 0U ||
			bucket_count > page_width_ || action_count == 0U || action_count > MULTIWAY_MAX_ABSTRACTED_ACTIONS)
			throw std::invalid_argument("external compact page lookup identity or shape is invalid");
		if (auto* cached = find(page_id))
		{
			++page_cache_hits_;
			if (cached->state_id != state.value || cached->seat != seat || cached->page_index != page_index ||
				cached->bucket_count != bucket_count || cached->action_count != action_count)
				throw std::invalid_argument("external compact page identity disagrees with row metadata");
			if (cached->pin_count == std::numeric_limits<std::uint32_t>::max())
				throw std::overflow_error("external compact page pin count overflows");
			++cached->pin_count;
			return { page_id, cached->regrets.data(), cached->strategy_masses.data(), bucket_count,
				action_count, page_width_ };
		}
		++page_cache_misses_;
		Page* selected = nullptr;
		for (std::size_t offset = 0U; offset < pages_.size(); ++offset)
		{
			const auto index = (next_victim_ + offset) % pages_.size();
			auto& candidate = pages_[index];
			if (!candidate.valid || (candidate.pin_count == 0U && !candidate.dirty))
			{
				selected = &candidate;
				next_victim_ = (index + 1U) % pages_.size();
				break;
			}
		}
		if (selected == nullptr) throw std::length_error("external compact page cache has no clean unpinned page");
		if (selected->valid) ++page_cache_evictions_;
		selected->valid = false;
		const auto found = store_->lookup(page_id, lookup_scratch_);
		if (found)
		{
			decode_page(lookup_scratch_.data(), *selected);
			if (selected->page_id != page_id || selected->state_id != state.value || selected->seat != seat ||
				selected->page_index != page_index || selected->bucket_count != bucket_count ||
				selected->action_count != action_count)
				throw std::invalid_argument("external compact page identity disagrees with requested row");
		}
		else
		{
			std::fill(selected->regrets.begin(), selected->regrets.end(), 0);
			std::fill(selected->strategy_masses.begin(), selected->strategy_masses.end(), 0U);
			selected->page_id = page_id;
			selected->state_id = state.value;
			selected->seat = seat;
			selected->page_index = page_index;
			selected->bucket_count = bucket_count;
			selected->action_count = action_count;
		}
		selected->valid = true;
		selected->dirty = false;
		selected->pin_count = 1U;
		return { page_id, selected->regrets.data(), selected->strategy_masses.data(), bucket_count,
			action_count, page_width_ };
	}

	void MultiwayExternalCompactPages::unpin(std::uint64_t page_id)
	{
		std::lock_guard<std::mutex> lock(mutex_);
		auto* page = find(page_id);
		if (page == nullptr || page->pin_count == 0U)
			throw std::invalid_argument("external compact page is not pinned");
		--page->pin_count;
	}

	void MultiwayExternalCompactPages::mark_dirty(std::uint64_t page_id)
	{
		std::lock_guard<std::mutex> lock(mutex_);
		if (!batch_open_) throw std::logic_error("external compact page mutation requires an open batch");
		auto* page = find(page_id);
		if (page == nullptr || page->pin_count == 0U)
			throw std::invalid_argument("external compact page mutation requires a pinned page");
		if (!page->dirty)
		{
			std::copy(page->regrets.begin(), page->regrets.end(), page->before_regrets.begin());
			std::copy(page->strategy_masses.begin(), page->strategy_masses.end(), page->before_strategy_masses.begin());
			page->dirty = true;
		}
	}

	void MultiwayExternalCompactPages::commit_batch()
	{
		std::lock_guard<std::mutex> lock(mutex_);
		if (!batch_open_) throw std::logic_error("external compact page batch is not open");
		std::vector<Page*> dirty;
		dirty.reserve(pages_.size());
		for (auto& page : pages_)
		{
			if (page.pin_count != 0U) throw std::logic_error("external compact page batch cannot commit pinned pages");
			if (page.dirty) dirty.push_back(&page);
		}
		if (dirty.empty())
		{
			batch_open_ = false;
			return;
		}
		std::sort(dirty.begin(), dirty.end(), [](const Page* left, const Page* right) { return left->page_id < right->page_id; });
		const auto prior_generation = store_->generation();
		try
		{
			std::vector<MultiwayExternalRecord> records;
			records.reserve(dirty.size());
			for (const auto* page : dirty)
			{
				encode_page(*page, encode_scratch_);
				records.push_back({ page->page_id, encode_scratch_ });
			}
			store_->append_batch(batch_number_, merge_fingerprint_, records);
		}
		catch (...)
		{
			if (store_->generation() > prior_generation)
			{
				for (auto* page : dirty) page->dirty = false;
				batch_open_ = false;
			}
			else
				abort_batch();
			throw;
		}
		for (auto* page : dirty) page->dirty = false;
		batch_open_ = false;
	}

	void MultiwayExternalCompactPages::set_merge_fingerprint(std::uint64_t merge_fingerprint)
	{
		std::lock_guard<std::mutex> lock(mutex_);
		if (!batch_open_ || merge_fingerprint == 0U)
			throw std::invalid_argument("external compact page merge fingerprint is invalid");
		merge_fingerprint_ = merge_fingerprint;
	}

	void MultiwayExternalCompactPages::abort_batch()
	{
		std::lock_guard<std::mutex> lock(mutex_);
		if (!batch_open_) return;
		for (auto& page : pages_)
		{
			if (page.dirty)
			{
				std::copy(page.before_regrets.begin(), page.before_regrets.end(), page.regrets.begin());
				std::copy(page.before_strategy_masses.begin(), page.before_strategy_masses.end(), page.strategy_masses.begin());
				page.dirty = false;
			}
		}
		batch_open_ = false;
	}

	std::size_t MultiwayExternalCompactPages::memory_bytes() const noexcept
	{
		std::size_t result = static_cast<std::size_t>(store_cache_budget_bytes_) +
			lookup_scratch_.capacity() + encode_scratch_.capacity() + pages_.capacity() * sizeof(Page);
		for (const auto& page : pages_)
			result += page.regrets.capacity() * sizeof(std::int32_t) +
				page.strategy_masses.capacity() * sizeof(std::uint64_t) +
				page.before_regrets.capacity() * sizeof(std::int32_t) +
				page.before_strategy_masses.capacity() * sizeof(std::uint64_t);
		return result;
	}

	MultiwayExternalStorageStats MultiwayExternalCompactPages::stats() const
	{
		std::lock_guard<std::mutex> lock(mutex_);
		auto result = store_->stats();
		result.page_cache_hits = page_cache_hits_;
		result.page_cache_misses = page_cache_misses_;
		result.page_cache_evictions = page_cache_evictions_;
		return result;
	}

	void MultiwayExternalCompactPages::compact(bool retain_input_runs)
	{
		std::lock_guard<std::mutex> lock(mutex_);
		for (const auto& page : pages_)
			if (page.pin_count != 0U || page.dirty)
				throw std::logic_error("external compact page compaction requires a clean cache");
		store_->compact(retain_input_runs);
	}

	MultiwayExternalCompactStorage::MultiwayExternalCompactStorage(
		MultiwayExternalStorageConfig row_config, MultiwayExternalStorageConfig page_config,
		std::uint32_t page_width, std::uint64_t page_cache_bytes)
		: rows_(std::move(row_config), page_width),
		  pages_(std::move(page_config), page_width, page_cache_bytes),
		  next_page_id_(rows_.next_page_id()), page_width_(page_width)
	{
		if (page_width_ == 0U) throw std::invalid_argument("external compact storage page width is zero");
	}

	void MultiwayExternalCompactStorage::admit_rows(std::uint64_t batch_number,
		std::uint64_t merge_fingerprint, const std::vector<MultiwaySparseRowShape>& rows)
	{
		rows_.append_batch(batch_number, merge_fingerprint, rows, next_page_id_);
	}

	void MultiwayExternalCompactStorage::begin_batch(std::uint64_t batch_number,
		std::uint64_t merge_fingerprint)
	{
		pages_.begin_batch(batch_number, merge_fingerprint);
	}

	bool MultiwayExternalCompactStorage::lookup_row(MultiwayInfosetId infoset,
		MultiwayExternalRowBinding& output) const
	{
		MultiwayExternalRowMetadata row{};
		if (!rows_.lookup(infoset.public_state, infoset.seat, row)) return false;
		output.state_id_ = infoset.public_state.value;
		output.owner_ = &rows_;
		output.seat_ = infoset.seat;
		output.row_ = row;
		return true;
	}

	MultiwayExternalCompactStorage::PinnedBucket::PinnedBucket(PinnedBucket&& other) noexcept
		: owner(other.owner), page(other.page), local_bucket(other.local_bucket), writable(other.writable)
	{
		other.owner = nullptr;
		other.page = {};
		other.local_bucket = 0U;
		other.writable = false;
	}

	MultiwayExternalCompactStorage::PinnedBucket MultiwayExternalCompactStorage::pin_bucket(
		const MultiwayExternalRowBinding& binding, std::uint32_t bucket, bool writable) const
	{
		MultiwayExternalRowPage row_page{};
		if (!rows_.locate_bucket(binding, bucket, row_page))
			throw std::out_of_range("external compact row or bucket is unavailable");
		PinnedBucket result;
		result.owner = this;
		result.local_bucket = bucket % page_width_;
		result.writable = writable;
		result.page = pages_.pin(row_page.page_id, { binding.state_id_ }, binding.seat_,
			row_page.page_index, row_page.bucket_count, row_page.action_count);
		if (writable)
		{
			try
			{
				pages_.mark_dirty(row_page.page_id);
			}
			catch (...)
			{
				pages_.unpin(row_page.page_id);
				throw;
			}
		}
		return result;
	}

	void MultiwayExternalCompactStorage::apply_delta(PinnedBucket& bucket,
		std::uint8_t action, double regret, double strategy_sum)
	{
		if (bucket.owner != this || !bucket.writable || bucket.page.page_id == 0U ||
			bucket.local_bucket >= bucket.page.bucket_count || action >= bucket.page.action_count)
			throw std::invalid_argument("external compact pinned bucket is invalid");
		const auto cell = static_cast<std::size_t>(action) * bucket.page.action_stride + bucket.local_bucket;
		MultiwayCompactStorage::apply_delta_to_cells(
			bucket.page.regrets[cell], bucket.page.strategy_masses[cell], regret, strategy_sum);
	}

	void MultiwayExternalCompactStorage::regret_matched_strategy_into(
		const PinnedBucket& bucket, double* output, std::size_t output_size) const
	{
		if (bucket.owner != this || bucket.page.page_id == 0U || output == nullptr ||
			output_size != bucket.page.action_count || bucket.local_bucket >= bucket.page.bucket_count)
			throw std::invalid_argument("external compact pinned bucket policy output is invalid");
		Probability total = 0.0;
		for (std::size_t action = 0U; action < output_size; ++action)
		{
			const auto cell = action * bucket.page.action_stride + bucket.local_bucket;
			output[action] = std::max(0.0, static_cast<double>(bucket.page.regrets[cell]) / 1024.0);
			total += output[action];
		}
		if (total == 0.0)
			std::fill(output, output + output_size, 1.0 / static_cast<double>(output_size));
		else
			for (std::size_t action = 0U; action < output_size; ++action) output[action] /= total;
	}

	void MultiwayExternalCompactStorage::average_strategy_into(
		const PinnedBucket& bucket, double* output, std::size_t output_size) const
	{
		if (bucket.owner != this || bucket.page.page_id == 0U || output == nullptr ||
			output_size != bucket.page.action_count || bucket.local_bucket >= bucket.page.bucket_count)
			throw std::invalid_argument("external compact pinned bucket policy output is invalid");
		std::uint64_t total = 0U;
		for (std::size_t action = 0U; action < output_size; ++action)
		{
			const auto cell = action * bucket.page.action_stride + bucket.local_bucket;
			output[action] = static_cast<double>(bucket.page.strategy_masses[cell]) /
				static_cast<double>(MultiwayCompactStorage::kMassScale);
			total += bucket.page.strategy_masses[cell];
		}
		if (total == 0U)
			std::fill(output, output + output_size, 1.0 / static_cast<double>(output_size));
		else
			for (std::size_t action = 0U; action < output_size; ++action) output[action] /= static_cast<double>(total) /
				static_cast<double>(MultiwayCompactStorage::kMassScale);
	}

	bool MultiwayExternalCompactStorage::action_below_regret(const PinnedBucket& bucket,
		std::uint8_t action, double threshold) const noexcept
	{
		if (bucket.owner != this || bucket.page.page_id == 0U || !std::isfinite(threshold) ||
			bucket.local_bucket >= bucket.page.bucket_count || action >= bucket.page.action_count)
			return false;
		const auto cell = static_cast<std::size_t>(action) * bucket.page.action_stride + bucket.local_bucket;
		return static_cast<double>(bucket.page.regrets[cell]) / 1024.0 < threshold;
	}

	void MultiwayExternalCompactStorage::unpin_bucket(PinnedBucket& bucket) const
	{
		if (bucket.owner != this || bucket.page.page_id == 0U)
			throw std::invalid_argument("external compact pinned bucket is invalid");
		pages_.unpin(bucket.page.page_id);
		bucket.owner = nullptr;
		bucket.page = {};
		bucket.local_bucket = 0U;
		bucket.writable = false;
	}

	void MultiwayExternalCompactStorage::apply_delta(const MultiwayExternalRowBinding& binding,
		std::uint32_t bucket, std::uint8_t action, double regret, double strategy_sum)
	{
		auto pinned = pin_bucket(binding, bucket, true);
		try
		{
			apply_delta(pinned, action, regret, strategy_sum);
		}
		catch (...)
		{
			unpin_bucket(pinned);
			throw;
		}
		unpin_bucket(pinned);
	}

	void MultiwayExternalCompactStorage::regret_matched_strategy_into(
		const MultiwayExternalRowBinding& binding, std::uint32_t bucket,
		double* output, std::size_t output_size) const
	{
		MultiwayExternalRowPage row_page{};
		if (binding.owner_ != &rows_)
			throw std::invalid_argument("external compact row binding belongs to another storage object");
		if (!rows_.locate_bucket(binding, bucket, row_page))
			throw std::out_of_range("external compact regret strategy row or bucket is unavailable");
		if (output == nullptr || output_size != row_page.action_count)
			throw std::invalid_argument("external compact regret strategy output size mismatch");
		auto page = pages_.pin(row_page.page_id, { binding.state_id_ }, binding.seat_,
			row_page.page_index, row_page.bucket_count, row_page.action_count);
		Probability total = 0.0;
		for (std::size_t action = 0U; action < output_size; ++action)
		{
			const auto cell = action * page.action_stride + bucket % page_width_;
			output[action] = std::max(0.0, static_cast<double>(page.regrets[cell]) / 1024.0);
			total += output[action];
		}
		if (total == 0.0)
			std::fill(output, output + output_size, 1.0 / static_cast<double>(output_size));
		else
			for (std::size_t action = 0U; action < output_size; ++action) output[action] /= total;
		pages_.unpin(row_page.page_id);
	}

	void MultiwayExternalCompactStorage::average_strategy_into(
		const MultiwayExternalRowBinding& binding, std::uint32_t bucket,
		double* output, std::size_t output_size) const
	{
		MultiwayExternalRowPage row_page{};
		if (binding.owner_ != &rows_)
			throw std::invalid_argument("external compact row binding belongs to another storage object");
		if (!rows_.locate_bucket(binding, bucket, row_page))
			throw std::out_of_range("external compact average strategy row or bucket is unavailable");
		if (output == nullptr || output_size != row_page.action_count)
			throw std::invalid_argument("external compact average strategy output size mismatch");
		auto page = pages_.pin(row_page.page_id, { binding.state_id_ }, binding.seat_,
			row_page.page_index, row_page.bucket_count, row_page.action_count);
		std::uint64_t total = 0U;
		const auto local_bucket = bucket % page_width_;
		for (std::size_t action = 0U; action < output_size; ++action)
		{
			const auto cell = action * page.action_stride + local_bucket;
			output[action] = static_cast<double>(page.strategy_masses[cell]) /
				static_cast<double>(MultiwayCompactStorage::kMassScale);
			total += page.strategy_masses[cell];
		}
		if (total == 0U)
			std::fill(output, output + output_size, 1.0 / static_cast<double>(output_size));
		else
			for (std::size_t action = 0U; action < output_size; ++action) output[action] /= static_cast<double>(total) /
				static_cast<double>(MultiwayCompactStorage::kMassScale);
		pages_.unpin(row_page.page_id);
	}

	bool MultiwayExternalCompactStorage::action_below_regret(const MultiwayExternalRowBinding& binding,
		std::uint32_t bucket, std::uint8_t action, double threshold) const
	{
		MultiwayExternalRowPage row_page{};
		if (!std::isfinite(threshold) || !rows_.locate_bucket(binding, bucket, row_page) ||
			action >= row_page.action_count)
			return false;
		auto page = pages_.pin(row_page.page_id, { binding.state_id_ }, binding.seat_,
			row_page.page_index, row_page.bucket_count, row_page.action_count);
		const auto cell = static_cast<std::size_t>(action) * page.action_stride + bucket % page_width_;
		const auto below = static_cast<double>(page.regrets[cell]) / 1024.0 < threshold;
		pages_.unpin(row_page.page_id);
		return below;
	}

	void MultiwayExternalCompactStorage::commit_batch()
	{
		pages_.commit_batch();
	}

	void MultiwayExternalCompactStorage::set_merge_fingerprint(std::uint64_t merge_fingerprint)
	{
		pages_.set_merge_fingerprint(merge_fingerprint);
	}

	void MultiwayExternalCompactStorage::abort_batch()
	{
		pages_.abort_batch();
	}

	void MultiwayExternalCompactStorage::compact(bool retain_input_runs)
	{
		rows_.compact(retain_input_runs);
		pages_.compact(retain_input_runs);
	}

	std::uint64_t MultiwayExternalCompactStorage::content_hash() const
	{
		auto hash = kHashOffset;
		texas::core::fingerprint::append_u64(hash, rows_.content_hash());
		texas::core::fingerprint::append_u64(hash, pages_.content_hash());
		return texas::core::fingerprint::finish(hash);
	}

	MultiwayExternalTrainingStorage::MultiwayExternalTrainingStorage(
		std::filesystem::path directory, MultiwayExternalStorageConfig catalog_config,
		MultiwayExternalStorageConfig row_config, MultiwayExternalStorageConfig page_config,
		std::uint32_t page_width, std::uint64_t row_page_cache_bytes,
		std::size_t maximum_ancestry_depth, bool synchronous_durability)
		: directory_(std::move(directory)), catalog_config_(std::move(catalog_config)),
		  row_config_(std::move(row_config)), page_config_(std::move(page_config)),
		  page_width_(page_width), row_page_cache_bytes_(row_page_cache_bytes),
		  maximum_ancestry_depth_(maximum_ancestry_depth),
		  synchronous_durability_(synchronous_durability),
		  transaction_(directory_, synchronous_durability)
	{
		if (directory_.empty() || page_width_ == 0U || row_page_cache_bytes_ == 0U ||
			maximum_ancestry_depth_ == 0U ||
			catalog_config_.directory.lexically_normal() != (directory_ / "catalog").lexically_normal() ||
			row_config_.directory.lexically_normal() != (directory_ / "rows").lexically_normal() ||
			page_config_.directory.lexically_normal() != (directory_ / "pages").lexically_normal() ||
			catalog_config_.synchronous_durability != synchronous_durability_ ||
			row_config_.synchronous_durability != synchronous_durability_ ||
			page_config_.synchronous_durability != synchronous_durability_)
			throw std::invalid_argument("external training storage component paths or bounds are invalid");
		open_components();
	}

	void MultiwayExternalTrainingStorage::open_components()
	{
		catalog_ = std::make_unique<MultiwayExternalPublicCatalog>(
			catalog_config_, maximum_ancestry_depth_);
		compact_ = std::make_unique<MultiwayExternalCompactStorage>(
			row_config_, page_config_, page_width_, row_page_cache_bytes_);
	}

	void MultiwayExternalTrainingStorage::begin_batch(std::uint64_t batch_number)
	{
		if (batch_number_ != 0U || batch_number == 0U)
			throw std::logic_error("external training storage batch state is invalid");
		transaction_.begin_batch(batch_number, 0U);
		batch_number_ = batch_number;
	}

	void MultiwayExternalTrainingStorage::append_catalog(std::uint64_t component_fingerprint,
		const std::vector<MultiwayPackedPublicNode>& nodes,
		const std::vector<MultiwayPublicStateDescriptor>& descriptors)
	{
		if (batch_number_ == 0U || component_fingerprint == 0U || nodes.size() != descriptors.size())
			throw std::invalid_argument("external training catalog batch is invalid");
		if (!nodes.empty())
			catalog_->append_batch(batch_number_, component_fingerprint, nodes, descriptors);
	}

	void MultiwayExternalTrainingStorage::admit_rows(std::uint64_t component_fingerprint,
		const std::vector<MultiwaySparseRowShape>& rows)
	{
		if (batch_number_ == 0U || component_fingerprint == 0U)
			throw std::invalid_argument("external training row batch is invalid");
		compact_->admit_rows(batch_number_, component_fingerprint, rows);
	}

	void MultiwayExternalTrainingStorage::begin_page_batch()
	{
		if (batch_number_ == 0U || page_batch_open_)
			throw std::logic_error("external training page batch state is invalid");
		compact_->begin_batch(batch_number_, 0U);
		page_batch_open_ = true;
	}

	void MultiwayExternalTrainingStorage::compact_components()
	{
		if (batch_number_ == 0U || page_batch_open_)
			throw std::logic_error("external training compaction requires a clean pre-page batch boundary");
		catalog_->compact(true);
		compact_->compact(true);
	}

	void MultiwayExternalTrainingStorage::commit_batch(
		std::uint64_t merge_fingerprint, std::uint64_t row_count)
	{
		if (batch_number_ == 0U || !page_batch_open_ || merge_fingerprint == 0U)
			throw std::logic_error("external training batch is not ready to commit");
		compact_->set_merge_fingerprint(merge_fingerprint);
		compact_->commit_batch();
		transaction_.set_merge_fingerprint(merge_fingerprint);
		const auto& current = transaction_.current_manifest();
		if (current.generation == std::numeric_limits<std::uint64_t>::max())
			throw std::overflow_error("external training generation overflows");
		MultiwayExternalGenerationManifest next{};
		next.generation = current.generation + 1U;
		next.committed_batch_number = batch_number_;
		next.merge_fingerprint = merge_fingerprint;
		next.catalog_generation = catalog_->generation();
		next.public_state_count = catalog_->record_count();
		next.row_generation = compact_->row_generation();
		next.row_count = row_count;
		next.page_generation = compact_->page_generation();
		next.page_count = compact_->next_page_id() - 1U;
		next.content_hash = content_hash();
		transaction_.commit_batch(next);
		batch_number_ = 0U;
		page_batch_open_ = false;
	}

	std::uint64_t MultiwayExternalTrainingStorage::content_hash() const
	{
		auto hash = kHashOffset;
		texas::core::fingerprint::append_u64(hash, catalog_->content_hash());
		texas::core::fingerprint::append_u64(hash, compact_->content_hash());
		return texas::core::fingerprint::finish(hash);
	}

	void MultiwayExternalTrainingStorage::abort_batch()
	{
		if (batch_number_ == 0U) return;
		if (compact_ != nullptr) compact_->abort_batch();
		catalog_.reset();
		compact_.reset();
		transaction_.abort_batch();
		batch_number_ = 0U;
		page_batch_open_ = false;
		open_components();
	}

	MultiwayExternalPageCache::MultiwayExternalPageCache(MultiwayExternalRecordStore& store, std::uint64_t cache_bytes)
		: store_(&store), payload_bytes_(store.payload_bytes()), lookup_scratch_(payload_bytes_, 0U)
	{
		const auto per_page = static_cast<std::uint64_t>(sizeof(Page)) + 2U * payload_bytes_;
		auto count = per_page == 0U || cache_bytes <= payload_bytes_ ? 0U : (cache_bytes - payload_bytes_) / per_page;
		const auto record_width = payload_bytes_ + 8U;
		const auto wal_pages = (store.maximum_batch_bytes() - 48U) / record_width;
		const auto segment_pages = store.segment_target_bytes() / record_width;
		count = std::min(count, std::min(wal_pages, segment_pages));
		if (count == 0U || count > std::numeric_limits<std::size_t>::max())
			throw std::invalid_argument("external page cache cannot hold one bounded page");
		pages_.resize(static_cast<std::size_t>(count));
		for (auto& page : pages_)
		{
			page.bytes.resize(payload_bytes_);
			page.before_batch.resize(payload_bytes_);
		}
	}

	MultiwayExternalPageCache::Page* MultiwayExternalPageCache::find(std::uint64_t key) noexcept
	{
		for (auto& page : pages_)
			if (page.valid && page.key == key) return &page;
		return nullptr;
	}

	MultiwayExternalPageCache::PageView MultiwayExternalPageCache::pin(std::uint64_t key)
	{
		if (auto* cached = find(key))
		{
			if (cached->pin_count == std::numeric_limits<std::uint32_t>::max())
				throw std::overflow_error("external page pin count overflows");
			++cached->pin_count;
			return { key, cached->bytes.data(), cached->bytes.size() };
		}
		Page* selected = nullptr;
		for (std::size_t offset = 0U; offset < pages_.size(); ++offset)
		{
			const auto index = (next_victim_ + offset) % pages_.size();
			auto& candidate = pages_[index];
			if (!candidate.valid || (candidate.pin_count == 0U && !candidate.dirty))
			{
				selected = &candidate;
				next_victim_ = (index + 1U) % pages_.size();
				break;
			}
		}
		if (selected == nullptr) throw std::length_error("external page cache has no clean unpinned page");
		const auto found = store_->lookup(key, lookup_scratch_);
		if (!found) std::fill(lookup_scratch_.begin(), lookup_scratch_.end(), 0U);
		std::copy(lookup_scratch_.begin(), lookup_scratch_.end(), selected->bytes.begin());
		selected->key = key;
		selected->valid = true;
		selected->dirty = false;
		selected->pin_count = 1U;
		return { key, selected->bytes.data(), selected->bytes.size() };
	}

	void MultiwayExternalPageCache::unpin(std::uint64_t key)
	{
		auto* page = find(key);
		if (page == nullptr || page->pin_count == 0U) throw std::invalid_argument("external page is not pinned");
		--page->pin_count;
	}

	void MultiwayExternalPageCache::mark_dirty(std::uint64_t key)
	{
		if (!batch_open_) throw std::logic_error("external page mutation requires an open batch");
		auto* page = find(key);
		if (page == nullptr || page->pin_count == 0U) throw std::invalid_argument("external page mutation requires a pinned page");
		if (!page->dirty)
		{
			std::copy(page->bytes.begin(), page->bytes.end(), page->before_batch.begin());
			page->dirty = true;
		}
	}

	void MultiwayExternalPageCache::begin_batch(std::uint64_t batch_number, std::uint64_t merge_fingerprint)
	{
		if (batch_open_ || batch_number == 0U) throw std::logic_error("external page batch state is invalid");
		for (const auto& page : pages_)
			if (page.dirty || page.pin_count != 0U) throw std::logic_error("external page cache is not at a clean batch boundary");
		batch_number_ = batch_number;
		merge_fingerprint_ = merge_fingerprint;
		batch_open_ = true;
	}

	void MultiwayExternalPageCache::commit_batch()
	{
		if (!batch_open_) throw std::logic_error("external page batch is not open");
		std::vector<Page*> dirty;
		dirty.reserve(pages_.size());
		for (auto& page : pages_)
		{
			if (page.pin_count != 0U) throw std::logic_error("external page batch cannot commit pinned pages");
			if (page.dirty) dirty.push_back(&page);
		}
		if (dirty.empty())
		{
			batch_open_ = false;
			return;
		}
		std::sort(dirty.begin(), dirty.end(), [](const Page* left, const Page* right) { return left->key < right->key; });
		std::vector<MultiwayExternalRecord> records;
		records.reserve(dirty.size());
		for (const auto* page : dirty) records.push_back({ page->key, page->bytes });
		const auto prior_generation = store_->generation();
		try
		{
			store_->append_batch(batch_number_, merge_fingerprint_, records);
			store_->compact();
		}
		catch (...)
		{
			if (store_->generation() > prior_generation)
			{
				for (auto* page : dirty) page->dirty = false;
				batch_open_ = false;
			}
			else
				abort_batch();
			throw;
		}
		for (auto* page : dirty) page->dirty = false;
		batch_open_ = false;
	}

	void MultiwayExternalPageCache::abort_batch() noexcept
	{
		for (auto& page : pages_)
			if (page.dirty)
			{
				std::copy(page.before_batch.begin(), page.before_batch.end(), page.bytes.begin());
				page.dirty = false;
			}
		batch_open_ = false;
	}

	std::size_t MultiwayExternalPageCache::dirty_page_count() const noexcept
	{
		return static_cast<std::size_t>(std::count_if(pages_.begin(), pages_.end(), [](const Page& page) { return page.dirty; }));
	}

	std::size_t MultiwayExternalPageCache::memory_bytes() const noexcept
	{
		return pages_.capacity() * sizeof(Page) + pages_.size() * payload_bytes_ * 2U + lookup_scratch_.capacity();
	}

	std::uint64_t MultiwayExternalRecordStore::generation() const { std::lock_guard<std::mutex> lock(mutex_); return generation_; }
	std::uint64_t MultiwayExternalRecordStore::record_count() const { std::lock_guard<std::mutex> lock(mutex_); return record_count_; }
	std::uint64_t MultiwayExternalRecordStore::content_hash() const
	{
		std::lock_guard<std::mutex> lock(mutex_);
		auto hash = kHashOffset;
		texas::core::fingerprint::append_u64(hash, generation_);
		texas::core::fingerprint::append_u64(hash, record_count_);
		for (const auto& run : runs_)
		{
			texas::core::fingerprint::append_u64(hash, run.generation);
			texas::core::fingerprint::append_u64(hash, run.record_count);
			texas::core::fingerprint::append_u64(hash, run.content_hash);
		}
		return texas::core::fingerprint::finish(hash);
	}
	std::size_t MultiwayExternalRecordStore::cache_capacity_records() const noexcept { return cache_capacity_; }
	std::size_t MultiwayExternalRecordStore::cache_bytes() const noexcept { return cache_capacity_ * (payload_bytes_ + sizeof(CacheEntry) + 8U); }
	MultiwayExternalStorageStats MultiwayExternalRecordStore::stats() const
	{
		std::lock_guard<std::mutex> lock(mutex_);
		return stats_;
	}

} // namespace texas::solver::multiway
