#pragma once

#include "solver/multiway/engine/multiway_external_storage.hpp"
#include "solver/multiway/engine/multiway_solver.hpp"
#include "solver/multiway/engine/multiway_compact_storage.hpp"
#include "solver/multiway/engine/multiway_memory.hpp"
#include "solver/multiway/abstraction/multiway_action_abstraction.hpp"

#include "core/atomic_publish.hpp"
#include "core/fingerprint.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstring>
#include <exception>
#include <fstream>
#include <limits>
#include <queue>
#include <stdexcept>
#include <string>
#include <system_error>
#include <thread>

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
		[[maybe_unused]] std::uint64_t export_filter_hash(std::uint64_t key) noexcept
		{
			key ^= key >> 30U;
			key *= 0xbf58476d1ce4e5b9ULL;
			key ^= key >> 27U;
			key *= 0x94d049bb133111ebULL;
			return key ^ (key >> 31U);
		}

		[[maybe_unused]] bool export_filter_maybe_contains(const std::uint64_t* filter, std::uint64_t key) noexcept
		{
			constexpr auto mask = MultiwayExternalRecordStore::export_filter_bytes * 8U - 1U;
			const auto hash = export_filter_hash(key);
			const auto a = hash & mask;
			const auto b = (hash >> 23U) & mask;
			return (filter[a / 64U] & (1ULL << (a % 64U))) != 0U && (filter[b / 64U] & (1ULL << (b % 64U))) != 0U;
		}
		class OptionalExternalTimer
		{
		public:
			explicit OptionalExternalTimer(std::uint64_t* target) noexcept
				: target_(target), started_(target != nullptr ? std::chrono::steady_clock::now() : std::chrono::steady_clock::time_point{}) {}
			~OptionalExternalTimer() noexcept
			{
				if (target_ == nullptr)
					return;
				const auto elapsed = static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
					std::chrono::steady_clock::now() - started_)
						.count());
				*target_ = elapsed > std::numeric_limits<std::uint64_t>::max() - *target_
					? std::numeric_limits<std::uint64_t>::max()
					: *target_ + elapsed;
			}

		private:
			std::uint64_t* target_;
			std::chrono::steady_clock::time_point started_;
		};

		constexpr std::array<std::uint8_t, 8U> kManifestMagic = { 'T', 'X', 'M', 'W', 'E', 'X', 'T', '1' };
		constexpr std::array<std::uint8_t, 8U> kWalMagic = { 'T', 'X', 'M', 'W', 'W', 'A', 'L', '1' };
		constexpr std::uint32_t kSchema = 1U;
		constexpr std::uint64_t kGenerationManifestBytes = 8U + 2U * 4U + 11U * 8U + 8U;
		constexpr std::uint64_t kHashOffset = texas::core::fingerprint::FNV1A_OFFSET;
		constexpr std::uint32_t kCheckpointTrainingExtensionMagic = 0x31544d57U;
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
		constexpr std::size_t kCatalogMenuBytes = kCatalogMenuHeaderBytes + MULTIWAY_MAX_ABSTRACTED_ACTIONS * kCatalogMenuActionBytes + kCatalogMenuChecksumBytes;
		constexpr std::array<std::uint8_t, 8U> kTransactionWalMagic = { 'T', 'X', 'M', 'W', 'B', 'A', 'T', '1' };

		template <typename Transform, typename Flush>
		[[maybe_unused]] std::size_t transform_external_regrets(
			MultiwayExternalCompactPages& pages,
			std::uint64_t next_page_id,
			Transform&& transform,
			Flush&& flush_page_batch)
		{
			std::size_t changed = 0U;
			std::size_t dirty_pages = 0U;
			for (std::uint64_t page_id = 1U; page_id < next_page_id; ++page_id)
			{
				auto page = pages.pin_existing(page_id);
				if (page.page_id == 0U)
					continue;
				bool pinned = true;
				try
				{
					bool page_changed = false;
					for (std::size_t action = 0U; action < page.action_count; ++action)
					{
						for (std::size_t bucket = 0U; bucket < page.bucket_count; ++bucket)
						{
							const auto regret = page.regrets[action * page.action_stride + bucket];
							auto candidate = regret;
							(void)transform(candidate);
							page_changed = page_changed || candidate != regret;
						}
					}
					if (!page_changed)
					{
						pages.unpin(page_id);
						pinned = false;
						continue;
					}
					pages.mark_dirty(page_id);
					for (std::size_t action = 0U; action < page.action_count; ++action)
					{
						for (std::size_t bucket = 0U; bucket < page.bucket_count; ++bucket)
						{
							auto& regret = page.regrets[action * page.action_stride + bucket];
							changed += transform(regret) ? 1U : 0U;
						}
					}
					pages.unpin(page_id);
					pinned = false;
					++dirty_pages;
					if (dirty_pages == pages.page_capacity())
					{
						flush_page_batch();
						dirty_pages = 0U;
					}
				}
				catch (...)
				{
					if (pinned)
						pages.unpin(page_id);
					throw;
				}
			}
			return changed;
		}
		constexpr std::array<const char*, 3U> kComponentNames = { "catalog", "rows", "pages" };
		constexpr std::array<const char*, 3U> kBackupNames = {
			"CATALOG_CURRENT.previous", "ROWS_CURRENT.previous", "PAGES_CURRENT.previous"
		};
		constexpr std::array<const char*, 4U> kCheckpointRestoreBackupNames = {
			"CHECKPOINT_CATALOG.previous", "CHECKPOINT_ROWS.previous",
			"CHECKPOINT_PAGES.previous", "CHECKPOINT_TRAINING.previous"
		};

#pragma pack(push, 8)
		struct ComponentManifestInfo
		{
			std::uint64_t generation = 0U;
			std::uint64_t last_batch_number = 0U;
			bool exists = false;
		};
#pragma pack(pop)
#pragma pack(push, 8)
		struct TransactionWal
		{
			std::uint32_t previous_flags = 0U;
			std::uint64_t batch_number = 0U;
			std::uint64_t merge_fingerprint = 0U;
		};
#pragma pack(pop)

		[[maybe_unused]] void append_u32(std::vector<std::uint8_t>& bytes, std::uint32_t value)
		{
			for (unsigned shift = 0U; shift < 32U; shift += 8U)
				bytes.push_back(static_cast<std::uint8_t>(value >> shift));
		}
		[[maybe_unused]] void append_u64(std::vector<std::uint8_t>& bytes, std::uint64_t value)
		{
			for (unsigned shift = 0U; shift < 64U; shift += 8U)
				bytes.push_back(static_cast<std::uint8_t>(value >> shift));
		}
		[[maybe_unused]] std::uint32_t read_u32(const std::uint8_t* bytes)
		{
			return static_cast<std::uint32_t>(bytes[0]) | (static_cast<std::uint32_t>(bytes[1]) << 8U) | (static_cast<std::uint32_t>(bytes[2]) << 16U) | (static_cast<std::uint32_t>(bytes[3]) << 24U);
		}
		[[maybe_unused]] std::uint64_t read_u64(const std::uint8_t* bytes)
		{
			std::uint64_t result = 0U;
			for (unsigned shift = 0U; shift < 64U; shift += 8U)
				result |= static_cast<std::uint64_t>(bytes[shift / 8U]) << shift;
			return result;
		}
		[[maybe_unused]] void append_f64(std::vector<std::uint8_t>& bytes, double value)
		{
			static_assert(sizeof(double) == sizeof(std::uint64_t), "checkpoint double width changed");
			std::uint64_t bits = 0U;
			std::memcpy(&bits, &value, sizeof(bits));
			append_u64(bytes, bits);
		}
		[[maybe_unused]] double read_f64(const std::uint8_t* bytes)
		{
			static_assert(sizeof(double) == sizeof(std::uint64_t), "checkpoint double width changed");
			const auto bits = read_u64(bytes);
			double value = 0.0;
			std::memcpy(&value, &bits, sizeof(value));
			return value;
		}
		[[maybe_unused]] void append_i32(std::vector<std::uint8_t>& bytes, int value)
		{
			static_assert(sizeof(int) == sizeof(std::int32_t), "packed node integer width changed");
			const auto signed_value = static_cast<std::int64_t>(value);
			const auto bits = static_cast<std::uint32_t>(signed_value < 0
					? (static_cast<std::uint64_t>(signed_value + (1LL << 32U)))
					: static_cast<std::uint64_t>(signed_value));
			append_u32(bytes, bits);
		}
		[[maybe_unused]] int read_i32(const std::uint8_t* bytes)
		{
			const auto bits = read_u32(bytes);
			const auto value = bits <= static_cast<std::uint32_t>(std::numeric_limits<std::int32_t>::max())
				? static_cast<std::int64_t>(bits)
				: static_cast<std::int64_t>(bits) - (1LL << 32U);
			return static_cast<int>(value);
		}
		[[maybe_unused]] void write_u64(std::ostream& out, std::uint64_t value)
		{
			for (unsigned shift = 0U; shift < 64U; shift += 8U)
				out.put(static_cast<char>(value >> shift));
		}
		[[maybe_unused]] std::uint64_t checked_add(std::uint64_t left, std::uint64_t right)
		{
			if (right > std::numeric_limits<std::uint64_t>::max() - left)
				throw std::length_error("external storage byte count overflows uint64");
			return left + right;
		}
		[[maybe_unused]] void add_storage_stats(MultiwayExternalStorageStats& target,
			const MultiwayExternalStorageStats& source)
		{
			target.lookups = checked_add(target.lookups, source.lookups);
			target.cache_hits = checked_add(target.cache_hits, source.cache_hits);
			target.cache_misses = checked_add(target.cache_misses, source.cache_misses);
			target.disk_reads = checked_add(target.disk_reads, source.disk_reads);
			target.cache_evictions = checked_add(target.cache_evictions, source.cache_evictions);
			target.read_nanoseconds = checked_add(target.read_nanoseconds, source.read_nanoseconds);
			target.page_cache_hits = checked_add(target.page_cache_hits, source.page_cache_hits);
			target.page_cache_misses = checked_add(target.page_cache_misses, source.page_cache_misses);
			target.page_cache_evictions = checked_add(target.page_cache_evictions, source.page_cache_evictions);
			target.compaction_read_bytes = checked_add(target.compaction_read_bytes,
				source.compaction_read_bytes);
			target.compaction_written_bytes = checked_add(target.compaction_written_bytes,
				source.compaction_written_bytes);
			target.run_checks = multiway_memory_saturating_add(target.run_checks, source.run_checks);
			target.run_count = multiway_memory_saturating_add(target.run_count, source.run_count);
			target.publications = multiway_memory_saturating_add(target.publications, source.publications);
			target.published_records = multiway_memory_saturating_add(target.published_records, source.published_records);
			target.manifest_bytes_written = multiway_memory_saturating_add(target.manifest_bytes_written, source.manifest_bytes_written);
			target.commit_nanoseconds = multiway_memory_saturating_add(target.commit_nanoseconds, source.commit_nanoseconds);
			target.compactions = multiway_memory_saturating_add(target.compactions, source.compactions);
			target.compaction_nanoseconds = multiway_memory_saturating_add(target.compaction_nanoseconds, source.compaction_nanoseconds);
		}
		[[maybe_unused]] std::size_t external_cache_slot(std::uint64_t key, std::size_t capacity) noexcept
		{
			auto mixed = key;
			mixed ^= mixed >> 30U;
			mixed *= 0xbf58476d1ce4e5b9ULL;
			mixed ^= mixed >> 27U;
			mixed *= 0x94d049bb133111ebULL;
			mixed ^= mixed >> 31U;
			return static_cast<std::size_t>(mixed % capacity);
		}
		[[maybe_unused]] void require_external_records_absent(
			const MultiwayExternalRecordStore& store,
			const std::vector<std::uint64_t>& sorted_keys,
			const char* error_message)
		{
#pragma pack(push, 8)
			struct Context
			{
				const char* error_message;
			};
#pragma pack(pop)
			Context context{ error_message };
			const auto reject_existing = [](void* raw_context, std::uint64_t,
											 const std::uint8_t*, std::size_t) -> bool {
				const auto& current = *static_cast<Context*>(raw_context);
				throw std::invalid_argument(current.error_message);
			};
			static_cast<void>(store.visit_matching_records(sorted_keys,
				reject_existing, &context));
		}
		[[maybe_unused]] std::uint64_t record_bytes(std::uint64_t count, std::size_t payload_bytes)
		{
			const auto width = checked_add(8U, payload_bytes);
			if (count != 0U && width > std::numeric_limits<std::uint64_t>::max() / count)
				throw std::length_error("external storage record bytes overflow uint64");
			return count * width;
		}
		[[maybe_unused]] std::filesystem::path run_path(const std::filesystem::path& directory, std::uint64_t generation, const char* suffix)
		{
			return directory / ("run_" + std::to_string(generation) + suffix);
		}
		[[maybe_unused]] void sync_file_path(const std::filesystem::path& path)
		{
#ifdef _WIN32
			const auto handle = CreateFileW(path.c_str(), GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ,
				nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
			if (handle == INVALID_HANDLE_VALUE)
				throw std::runtime_error("external transaction file cannot be opened for flush");
			const auto flushed = FlushFileBuffers(handle);
			CloseHandle(handle);
			if (!flushed)
				throw std::runtime_error("external transaction file flush failed");
#else
			const auto descriptor = open(path.c_str(), O_RDONLY);
			if (descriptor < 0)
				throw std::runtime_error("external transaction file cannot be opened for flush");
			const auto flushed = fsync(descriptor);
			close(descriptor);
			if (flushed != 0)
				throw std::runtime_error("external transaction file flush failed");
#endif
		}
		[[maybe_unused]] void sync_directory_path(const std::filesystem::path& path)
		{
#ifdef _WIN32
			(void)path;
#else
			const auto descriptor = open(path.c_str(), O_RDONLY | O_DIRECTORY);
			if (descriptor < 0)
				throw std::runtime_error("external transaction directory cannot be opened for flush");
			const auto flushed = fsync(descriptor);
			close(descriptor);
			if (flushed != 0)
				throw std::runtime_error("external transaction directory flush failed");
#endif
		}
		[[maybe_unused]] std::filesystem::path checkpoint_pin_path(const std::filesystem::path& directory,
			std::uint64_t pin_id)
		{
			return directory / ("CHECKPOINT_" + std::to_string(pin_id) + ".PIN");
		}
		[[maybe_unused]] std::uint64_t checkpoint_pin_id(const std::filesystem::path& checkpoint_path,
			std::uint64_t storage_identity_hash)
		{
			auto hash = kHashOffset;
			for (const auto character : checkpoint_path.lexically_normal().string())
				texas::core::fingerprint::append_u8(hash, static_cast<std::uint8_t>(character));
			texas::core::fingerprint::append_u64(hash, storage_identity_hash);
			auto result = texas::core::fingerprint::finish(hash);
			return result == 0U ? 1U : result;
		}
		[[maybe_unused]] std::vector<MultiwayExternalCheckpointManifest::SegmentReference> read_checkpoint_pin(
			const std::filesystem::path& path)
		{
			std::ifstream input(path, std::ios::binary);
			if (!input)
				throw std::invalid_argument("external checkpoint pin cannot be opened");
			std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(input)),
				std::istreambuf_iterator<char>());
			constexpr std::array<std::uint8_t, 8U> magic = { 'T', 'X', 'M', 'W', 'P', 'I', 'N', '1' };
			constexpr std::size_t header_bytes = 20U;
			if (bytes.size() < header_bytes || !std::equal(magic.begin(), magic.end(), bytes.begin()) || read_u32(bytes.data() + 8U) != 1U)
				throw std::invalid_argument("external checkpoint pin header is invalid");
			const auto count = read_u64(bytes.data() + 12U);
			if (bytes.size() < header_bytes + 8U || count > (bytes.size() - header_bytes - 8U) / 24U || bytes.size() != header_bytes + count * 24U + 8U)
				throw std::invalid_argument("external checkpoint pin segment count is invalid");
			auto checksum = kHashOffset;
			for (std::size_t index = 0U; index < bytes.size() - 8U; ++index)
				texas::core::fingerprint::append_u8(checksum, bytes[index]);
			if (texas::core::fingerprint::finish(checksum) != read_u64(bytes.data() + bytes.size() - 8U))
				throw std::invalid_argument("external checkpoint pin checksum is invalid");
			std::vector<MultiwayExternalCheckpointManifest::SegmentReference> references;
			references.reserve(static_cast<std::size_t>(count));
			std::size_t cursor = 20U;
			std::uint64_t previous_generation = 0U;
			for (std::uint64_t index = 0U; index < count; ++index)
			{
				MultiwayExternalCheckpointManifest::SegmentReference reference{};
				reference.generation = read_u64(bytes.data() + cursor);
				cursor += 8U;
				reference.record_count = read_u64(bytes.data() + cursor);
				cursor += 8U;
				reference.content_hash = read_u64(bytes.data() + cursor);
				cursor += 8U;
				if (reference.generation <= previous_generation || reference.record_count == 0U)
					throw std::invalid_argument("external checkpoint pin runs are not ordered");
				previous_generation = reference.generation;
				references.push_back(reference);
			}
			return references;
		}
		[[maybe_unused]] std::vector<std::uint64_t> load_pinned_run_generations(
			const std::filesystem::path& directory)
		{
			std::vector<std::uint64_t> generations;
			for (const auto& entry : std::filesystem::directory_iterator(directory))
			{
				if (!entry.is_regular_file())
					throw std::invalid_argument("external storage directory contains a non-file entry");
				const auto name = entry.path().filename().string();
				if (name.rfind("CHECKPOINT_", 0U) != 0U || name.size() < 14U || name.substr(name.size() - 4U) != ".PIN")
					continue;
				const auto references = read_checkpoint_pin(entry.path());
				for (const auto& reference : references)
					generations.push_back(reference.generation);
			}
			std::sort(generations.begin(), generations.end());
			generations.erase(std::unique(generations.begin(), generations.end()), generations.end());
			return generations;
		}
		[[maybe_unused]] void write_checkpoint_pin(const std::filesystem::path& directory, std::uint64_t pin_id,
			const std::vector<MultiwayExternalCheckpointManifest::SegmentReference>& references,
			bool synchronous_durability)
		{
			std::vector<std::uint8_t> bytes{ 'T', 'X', 'M', 'W', 'P', 'I', 'N', '1' };
			append_u32(bytes, 1U);
			append_u64(bytes, references.size());
			for (const auto& reference : references)
			{
				append_u64(bytes, reference.generation);
				append_u64(bytes, reference.record_count);
				append_u64(bytes, reference.content_hash);
			}
			auto hash = kHashOffset;
			for (const auto byte : bytes)
				texas::core::fingerprint::append_u8(hash, byte);
			append_u64(bytes, texas::core::fingerprint::finish(hash));
			const auto destination = checkpoint_pin_path(directory, pin_id);
			const auto temporary = std::filesystem::path(destination.string() + ".tmp");
			if (std::filesystem::exists(temporary))
				throw std::runtime_error("external checkpoint pin temporary path already exists");
#pragma pack(push, 8)
			struct TemporaryFileCleanup
			{
				std::filesystem::path path;
				~TemporaryFileCleanup() noexcept
				{
					std::error_code ignored;
					std::filesystem::remove(path, ignored);
				}
			} cleanup{ temporary };
#pragma pack(pop)
			std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
			if (!output)
				throw std::runtime_error("external checkpoint pin cannot be opened");
			output.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
			output.flush();
			if (!output)
				throw std::runtime_error("external checkpoint pin write failed");
			output.close();
			if (synchronous_durability)
				sync_file_path(temporary);
			texas::core::publish_atomic_replace(temporary, destination,
				"external checkpoint pin publish failed",
				texas::core::AtomicPublishFallbackPolicy::RejectNonAtomicFallback);
			if (synchronous_durability)
				sync_directory_path(directory);
		}
		[[maybe_unused]] ComponentManifestInfo read_component_manifest(const std::filesystem::path& component_directory)
		{
			const auto path = component_directory / "CURRENT";
			std::error_code error;
			if (!std::filesystem::exists(path, error))
			{
				if (error)
					throw std::runtime_error("external component manifest cannot be inspected");
				return {};
			}
			const auto file_bytes = std::filesystem::file_size(path, error);
			if (error || file_bytes < 48U)
				throw std::invalid_argument("external component manifest size is invalid");
			std::ifstream input(path, std::ios::binary);
			std::array<std::uint8_t, 40U> header{};
			input.read(reinterpret_cast<char*>(header.data()), static_cast<std::streamsize>(header.size()));
			if (!input || !std::equal(kManifestMagic.begin(), kManifestMagic.end(), header.begin()) || read_u32(header.data() + 8U) != kSchema)
				throw std::invalid_argument("external component manifest header is invalid");
			const auto generation = read_u64(header.data() + 16U);
			const auto run_count = read_u64(header.data() + 24U);
			const auto last_batch_number = read_u64(header.data() + 32U);
			if (run_count > (std::numeric_limits<std::uint64_t>::max() - 48U) / 24U || 48U + run_count * 24U != file_bytes)
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
				if (!input)
					throw std::invalid_argument("external component manifest is truncated");
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
#pragma pack(push, 8)
		struct ExternalDiskBudgetSnapshot
		{
			std::uint64_t stored_bytes = 0U;
			std::uint64_t live_segment_bytes = 0U;
		};
#pragma pack(pop)
		[[maybe_unused]] ExternalDiskBudgetSnapshot read_external_disk_budget_snapshot(
			const std::filesystem::path& root)
		{
			ExternalDiskBudgetSnapshot result{};
			std::error_code error;
			for (std::filesystem::recursive_directory_iterator it(root, error), end;
				!error && it != end; it.increment(error))
			{
				if (it->is_directory(error))
					continue;
				if (error || !it->is_regular_file(error) || error)
					throw std::runtime_error("external disk budget encountered a non-file entry");
				const auto bytes = it->file_size(error);
				if (error)
					throw std::runtime_error("external disk budget file size is unavailable");
				result.stored_bytes = checked_add(result.stored_bytes, bytes);
			}
			if (error)
				throw std::runtime_error("external disk budget directory scan failed");

			for (const auto* component : kComponentNames)
			{
				const auto component_directory = root / component;
				const auto info = read_component_manifest(component_directory);
				if (!info.exists)
					continue;
				const auto manifest_path = component_directory / "CURRENT";
				std::ifstream input(manifest_path, std::ios::binary);
				std::array<std::uint8_t, 40U> header{};
				input.read(reinterpret_cast<char*>(header.data()), static_cast<std::streamsize>(header.size()));
				if (!input || !std::equal(kManifestMagic.begin(), kManifestMagic.end(), header.begin()))
					throw std::invalid_argument("external disk budget manifest header is invalid");
				const auto payload_bytes = static_cast<std::size_t>(read_u32(header.data() + 12U));
				const auto run_count = read_u64(header.data() + 24U);
				if (run_count > 1000000U)
					throw std::length_error("external disk budget manifest run count exceeds its bound");
				for (std::uint64_t index = 0U; index < run_count; ++index)
				{
					std::array<std::uint8_t, 24U> reference{};
					input.read(reinterpret_cast<char*>(reference.data()), static_cast<std::streamsize>(reference.size()));
					if (!input)
						throw std::invalid_argument("external disk budget manifest reference is truncated");
					const auto generation = read_u64(reference.data());
					const auto records = read_u64(reference.data() + 8U);
					const auto expected_data = record_bytes(records, payload_bytes);
					const auto expected_index = record_bytes(records, 0U);
					std::error_code file_error;
					if (std::filesystem::file_size(run_path(component_directory, generation, ".dat"), file_error) != expected_data || file_error || std::filesystem::file_size(run_path(component_directory, generation, ".idx"), file_error) != expected_index || file_error)
						throw std::invalid_argument("external disk budget referenced run length is invalid");
					result.live_segment_bytes = checked_add(result.live_segment_bytes,
						checked_add(expected_data, expected_index));
				}
			}
			return result;
		}
		[[maybe_unused]] std::uint64_t disk_amplification_limit_bytes(
			const ExternalDiskBudgetSnapshot& snapshot, std::uint32_t amplification_limit)
		{
			if (amplification_limit == 0U)
				return std::numeric_limits<std::uint64_t>::max();
			if (snapshot.live_segment_bytes == 0U || snapshot.live_segment_bytes > std::numeric_limits<std::uint64_t>::max() / amplification_limit)
				throw std::length_error("external disk amplification bound is unavailable or overflows");
			return snapshot.live_segment_bytes * amplification_limit;
		}
		[[maybe_unused]] void check_aggregate_commit_disk_budget(const std::filesystem::path& root,
			std::uint32_t amplification_limit, std::uint64_t disk_reserve_bytes,
			std::uint64_t additional_bytes)
		{
			const auto snapshot = read_external_disk_budget_snapshot(root);
			const auto disk_limit = disk_amplification_limit_bytes(snapshot, amplification_limit);
			if (snapshot.stored_bytes > disk_limit || additional_bytes > disk_limit - snapshot.stored_bytes)
				throw std::length_error("external training commit exceeds the disk amplification bound while staging");
			std::error_code error;
			const auto space = std::filesystem::space(root, error);
			if (error || checked_add(additional_bytes, disk_reserve_bytes) > space.available)
				throw std::runtime_error("external training commit would violate the disk reserve");
		}
		[[maybe_unused]] void write_transaction_wal(const std::filesystem::path& directory,
			const TransactionWal& transaction, bool synchronous_durability,
			bool replace_existing = false)
		{
			std::vector<std::uint8_t> bytes(kTransactionWalMagic.begin(), kTransactionWalMagic.end());
			append_u32(bytes, 1U);
			append_u32(bytes, transaction.previous_flags);
			append_u64(bytes, transaction.batch_number);
			append_u64(bytes, transaction.merge_fingerprint);
			auto checksum = kHashOffset;
			for (const auto byte : bytes)
				texas::core::fingerprint::append_u8(checksum, byte);
			append_u64(bytes, texas::core::fingerprint::finish(checksum));
			const auto temp = directory / "TRAINING_BATCH.tmp";
			const auto wal = directory / "TRAINING_BATCH.wal";
			if (std::filesystem::exists(temp) || (!replace_existing && std::filesystem::exists(wal)) || (replace_existing && !std::filesystem::exists(wal)))
				throw std::runtime_error("external transaction already has a pending journal");
			std::ofstream output(temp, std::ios::binary | std::ios::trunc);
			if (!output)
				throw std::runtime_error("external transaction journal cannot be opened");
			output.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
			output.flush();
			if (!output)
				throw std::runtime_error("external transaction journal write failed");
			output.close();
			if (synchronous_durability)
				sync_file_path(temp);
			texas::core::publish_atomic_replace(temp, wal, "external transaction journal publish failed",
				texas::core::AtomicPublishFallbackPolicy::RejectNonAtomicFallback);
			if (synchronous_durability)
				sync_directory_path(directory);
		}
		[[maybe_unused]] TransactionWal read_transaction_wal(const std::filesystem::path& path)
		{
			if (std::filesystem::file_size(path) != 40U)
				throw std::invalid_argument("external transaction journal size is invalid");
			std::array<std::uint8_t, 40U> bytes{};
			std::ifstream input(path, std::ios::binary);
			input.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
			if (!input || !std::equal(kTransactionWalMagic.begin(), kTransactionWalMagic.end(), bytes.begin()) || read_u32(bytes.data() + 8U) != 1U)
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
#pragma pack(push, 8)
		struct CheckpointRestoreWal
		{
			std::uint32_t previous_flags = 0U;
			MultiwayExternalGenerationManifest target{};
			std::array<std::uint64_t, 3U> component_manifest_hashes{};
		};
#pragma pack(pop)
		constexpr std::array<std::uint8_t, 8U> kCheckpointRestoreWalMagic = {
			'T', 'X', 'M', 'W', 'R', 'S', 'T', '1'
		};
		[[maybe_unused]] void write_checkpoint_restore_wal(const std::filesystem::path& directory,
			const CheckpointRestoreWal& wal, bool synchronous_durability)
		{
			std::vector<std::uint8_t> bytes(kCheckpointRestoreWalMagic.begin(), kCheckpointRestoreWalMagic.end());
			append_u32(bytes, 1U);
			append_u32(bytes, wal.previous_flags);
			append_u64(bytes, wal.target.generation);
			append_u64(bytes, wal.target.content_hash);
			append_u64(bytes, wal.target.catalog_generation);
			append_u64(bytes, wal.target.row_generation);
			append_u64(bytes, wal.target.page_generation);
			for (const auto hash : wal.component_manifest_hashes)
				append_u64(bytes, hash);
			auto checksum = kHashOffset;
			for (const auto byte : bytes)
				texas::core::fingerprint::append_u8(checksum, byte);
			append_u64(bytes, texas::core::fingerprint::finish(checksum));
			const auto temporary = directory / "CHECKPOINT_RESTORE.wal.tmp";
			const auto destination = directory / "CHECKPOINT_RESTORE.wal";
			if (std::filesystem::exists(temporary) || std::filesystem::exists(destination))
				throw std::runtime_error("external checkpoint restore already has a pending journal");
			std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
			if (!output)
				throw std::runtime_error("external checkpoint restore journal cannot be opened");
			output.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
			output.flush();
			if (!output)
				throw std::runtime_error("external checkpoint restore journal write failed");
			output.close();
			if (synchronous_durability)
				sync_file_path(temporary);
			texas::core::publish_atomic_replace(temporary, destination,
				"external checkpoint restore journal publish failed",
				texas::core::AtomicPublishFallbackPolicy::RejectNonAtomicFallback);
			if (synchronous_durability)
				sync_directory_path(directory);
		}
		[[maybe_unused]] CheckpointRestoreWal read_checkpoint_restore_wal(const std::filesystem::path& path)
		{
			constexpr std::size_t bytes_size = 88U;
			if (std::filesystem::file_size(path) != bytes_size)
				throw std::invalid_argument("external checkpoint restore journal size is invalid");
			std::array<std::uint8_t, bytes_size> bytes{};
			std::ifstream input(path, std::ios::binary);
			input.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
			if (!input || !std::equal(kCheckpointRestoreWalMagic.begin(), kCheckpointRestoreWalMagic.end(), bytes.begin()) || read_u32(bytes.data() + 8U) != 1U)
				throw std::invalid_argument("external checkpoint restore journal header is invalid");
			auto checksum = kHashOffset;
			for (std::size_t index = 0U; index < bytes.size() - 8U; ++index)
				texas::core::fingerprint::append_u8(checksum, bytes[index]);
			if (texas::core::fingerprint::finish(checksum) != read_u64(bytes.data() + bytes.size() - 8U))
				throw std::invalid_argument("external checkpoint restore journal checksum is invalid");
			CheckpointRestoreWal result{};
			result.previous_flags = read_u32(bytes.data() + 12U);
			result.target.generation = read_u64(bytes.data() + 16U);
			result.target.content_hash = read_u64(bytes.data() + 24U);
			result.target.catalog_generation = read_u64(bytes.data() + 32U);
			result.target.row_generation = read_u64(bytes.data() + 40U);
			result.target.page_generation = read_u64(bytes.data() + 48U);
			for (std::size_t component = 0U; component < result.component_manifest_hashes.size(); ++component)
				result.component_manifest_hashes[component] = read_u64(bytes.data() + 56U + component * 8U);
			if ((result.previous_flags & ~15U) != 0U || result.target.generation == 0U)
				throw std::invalid_argument("external checkpoint restore journal fields are invalid");
			return result;
		}
		[[maybe_unused]] void cleanup_checkpoint_restore(const std::filesystem::path& directory)
		{
			std::error_code error;
			for (const auto* name : kCheckpointRestoreBackupNames)
			{
				std::filesystem::remove(directory / name, error);
				if (error)
					throw std::runtime_error("external checkpoint restore backup cannot be removed");
			}
			std::filesystem::remove(directory / "CHECKPOINT_RESTORE.wal", error);
			if (error)
				throw std::runtime_error("external checkpoint restore journal cannot be removed");
			for (const auto* name : { "CHECKPOINT_RESTORE.wal.tmp", "TRAINING_CURRENT.tmp",
					 "CHECKPOINT_TRAINING_ROLLBACK.tmp" })
			{
				std::filesystem::remove(directory / name, error);
				if (error)
					throw std::runtime_error("external checkpoint restore temporary journal cannot be removed");
			}
			for (const auto* component : kComponentNames)
				for (const auto* name : { "CHECKPOINT_CURRENT.tmp", "CHECKPOINT_ROLLBACK.tmp" })
				{
					std::filesystem::remove(directory / component / name, error);
					if (error)
						throw std::runtime_error("external checkpoint component temporary cannot be removed");
				}
		}
		[[maybe_unused]] std::vector<std::uint8_t> encode_checkpoint_component_manifest(
			std::uint32_t payload_bytes, std::uint64_t generation, std::uint64_t last_batch_number,
			const std::vector<MultiwayExternalCheckpointManifest::SegmentReference>& references)
		{
			std::vector<std::uint8_t> bytes(kManifestMagic.begin(), kManifestMagic.end());
			append_u32(bytes, kSchema);
			append_u32(bytes, payload_bytes);
			append_u64(bytes, generation);
			append_u64(bytes, references.size());
			append_u64(bytes, last_batch_number);
			for (const auto& reference : references)
			{
				append_u64(bytes, reference.generation);
				append_u64(bytes, reference.record_count);
				append_u64(bytes, reference.content_hash);
			}
			auto checksum = kHashOffset;
			for (const auto byte : bytes)
				texas::core::fingerprint::append_u8(checksum, byte);
			append_u64(bytes, texas::core::fingerprint::finish(checksum));
			return bytes;
		}
		[[maybe_unused]] void publish_checkpoint_component_manifest(const std::filesystem::path& component_directory,
			const std::vector<std::uint8_t>& bytes, bool synchronous_durability)
		{
			const auto temporary = component_directory / "CHECKPOINT_CURRENT.tmp";
			const auto destination = component_directory / "CURRENT";
			if (std::filesystem::exists(temporary))
				throw std::runtime_error("external checkpoint component temporary path already exists");
			std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
			if (!output)
				throw std::runtime_error("external checkpoint component manifest cannot be opened");
			output.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
			output.flush();
			if (!output)
				throw std::runtime_error("external checkpoint component manifest write failed");
			output.close();
			if (synchronous_durability)
				sync_file_path(temporary);
			texas::core::publish_atomic_replace(temporary, destination,
				"external checkpoint component manifest publish failed",
				texas::core::AtomicPublishFallbackPolicy::RejectNonAtomicFallback);
			if (synchronous_durability)
				sync_directory_path(component_directory);
		}
		[[maybe_unused]] void recover_checkpoint_restore(const std::filesystem::path& directory, bool synchronous_durability)
		{
			const auto wal_path = directory / "CHECKPOINT_RESTORE.wal";
			if (!std::filesystem::exists(wal_path))
			{
				cleanup_checkpoint_restore(directory);
				return;
			}
			const auto wal = read_checkpoint_restore_wal(wal_path);
			MultiwayExternalGenerationManifest published{};
			const bool has_generation = load_multiway_external_generation(directory, published);
			bool target_is_published = has_generation && published.generation == wal.target.generation && published.content_hash == wal.target.content_hash;
			const std::array<std::uint64_t, 3U> target_component_generations = {
				wal.target.catalog_generation, wal.target.row_generation, wal.target.page_generation
			};
			for (std::size_t component = 0U; target_is_published && component < kComponentNames.size(); ++component)
			{
				try
				{
					const auto component_directory = directory / kComponentNames[component];
					const auto current_manifest = component_directory / "CURRENT";
					if (read_component_manifest(component_directory).generation != target_component_generations[component])
					{
						target_is_published = false;
						continue;
					}
					std::ifstream input(current_manifest, std::ios::binary);
					std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(input)),
						std::istreambuf_iterator<char>());
					if (!input.eof() && input.fail())
						throw std::runtime_error("external checkpoint component manifest cannot be read");
					auto hash = kHashOffset;
					for (const auto byte : bytes)
						texas::core::fingerprint::append_u8(hash, byte);
					target_is_published = texas::core::fingerprint::finish(hash) == wal.component_manifest_hashes[component];
				}
				catch (...)
				{
					target_is_published = false;
				}
			}
			if (!target_is_published)
			{
				for (std::size_t component = 0U; component < kComponentNames.size(); ++component)
				{
					const auto component_directory = directory / kComponentNames[component];
					const auto current = component_directory / "CURRENT";
					const auto backup = directory / kCheckpointRestoreBackupNames[component];
					if ((wal.previous_flags & (1U << component)) != 0U)
					{
						const auto temporary = component_directory / "CHECKPOINT_ROLLBACK.tmp";
						std::filesystem::copy_file(backup, temporary, std::filesystem::copy_options::overwrite_existing);
						if (synchronous_durability)
							sync_file_path(temporary);
						texas::core::publish_atomic_replace(temporary, current,
							"external checkpoint rollback failed",
							texas::core::AtomicPublishFallbackPolicy::RejectNonAtomicFallback);
					}
					else
					{
						std::error_code error;
						std::filesystem::remove(current, error);
						if (error)
							throw std::runtime_error("external checkpoint rollback manifest cannot be removed");
					}
				}
				const auto training_current = directory / "TRAINING_CURRENT";
				const auto training_backup = directory / kCheckpointRestoreBackupNames[3];
				if ((wal.previous_flags & 8U) != 0U)
				{
					const auto temporary = directory / "CHECKPOINT_TRAINING_ROLLBACK.tmp";
					std::filesystem::copy_file(training_backup, temporary, std::filesystem::copy_options::overwrite_existing);
					if (synchronous_durability)
						sync_file_path(temporary);
					texas::core::publish_atomic_replace(temporary, training_current,
						"external checkpoint rollback failed",
						texas::core::AtomicPublishFallbackPolicy::RejectNonAtomicFallback);
				}
				else
				{
					std::error_code error;
					std::filesystem::remove(training_current, error);
					if (error)
						throw std::runtime_error("external checkpoint rollback generation cannot be removed");
				}
			}
			cleanup_checkpoint_restore(directory);
			if (synchronous_durability)
				sync_directory_path(directory);
		}
		[[maybe_unused]] void append_file_hash(std::uint64_t& hash, const std::filesystem::path& path)
		{
			std::ifstream input(path, std::ios::binary);
			if (!input)
				throw std::runtime_error("external storage run cannot be read");
			std::array<char, 64U * 1024U> buffer{};
			while (input)
			{
				input.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
				const auto count = input.gcount();
				for (std::streamsize index = 0; index < count; ++index)
					texas::core::fingerprint::append_u8(hash, static_cast<std::uint8_t>(buffer[static_cast<std::size_t>(index)]));
			}
			if (!input.eof())
				throw std::runtime_error("external storage run read failed");
		}
	} // namespace
} // namespace texas::solver::multiway
