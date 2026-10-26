#include "solver/multiway/engine/multiway_external_storage.hpp"

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
			for (auto& cached : cache_)
				for (const auto& record : records)
					if (cached.valid && cached.key == record.key) cached.valid = false;
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
		std::lock_guard<std::mutex> lock(mutex_);
		if (failed_) throw std::runtime_error("external storage is unavailable after a failed batch");
		return lookup_unlocked(key, payload);
	}

	bool MultiwayExternalRecordStore::lookup_unlocked(std::uint64_t key, std::vector<std::uint8_t>& payload) const
	{
		++stats_.lookups;
		for (const auto& entry : cache_)
			if (entry.valid && entry.key == key)
			{
				++stats_.cache_hits;
				payload = entry.payload;
				return true;
			}
		++stats_.cache_misses;
		const auto started = std::chrono::steady_clock::now();
		for (auto run = runs_.rbegin(); run != runs_.rend(); ++run)
		{
			++stats_.disk_reads;
			if (!lookup_run(*run, key, payload)) continue;
			auto& entry = cache_[next_cache_entry_++ % cache_.size()];
			if (entry.valid) ++stats_.cache_evictions;
			entry.key = key; entry.valid = true;
			std::copy(payload.begin(), payload.end(), entry.payload.begin());
			stats_.read_nanoseconds += static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - started).count());
			return true;
		}
		stats_.read_nanoseconds += static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - started).count());
		return false;
	}

	bool MultiwayExternalRecordStore::lookup_run(const Run& run, std::uint64_t key, std::vector<std::uint8_t>& payload) const
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
		payload.resize(payload_bytes_);
		data.read(reinterpret_cast<char*>(payload.data()), static_cast<std::streamsize>(payload.size()));
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

	void MultiwayExternalRecordStore::compact()
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
	std::size_t MultiwayExternalRecordStore::cache_capacity_records() const noexcept { return cache_capacity_; }
	std::size_t MultiwayExternalRecordStore::cache_bytes() const noexcept { return cache_capacity_ * (payload_bytes_ + sizeof(CacheEntry) + 8U); }
	MultiwayExternalStorageStats MultiwayExternalRecordStore::stats() const
	{
		std::lock_guard<std::mutex> lock(mutex_);
		return stats_;
	}

} // namespace texas::solver::multiway
