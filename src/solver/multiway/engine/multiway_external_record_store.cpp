#include "multiway_external_storage_detail.hpp"

namespace texas::solver::multiway
{
	MultiwayExternalRecordStore::MultiwayExternalRecordStore(MultiwayExternalStorageConfig config, std::size_t payload_bytes)
		: config_(std::move(config)), payload_bytes_(payload_bytes)
	{
		config_.validate(payload_bytes_);
		cache_capacity_ = static_cast<std::size_t>(config_.cache_bytes / (payload_bytes_ + sizeof(CacheEntry) + 8U));
		if (cache_capacity_ == 0U)
			throw std::invalid_argument("external storage cache cannot hold one record");
		cache_.resize(cache_capacity_);
		for (auto& entry : cache_)
			entry.payload.resize(payload_bytes_);
		std::error_code error;
		if (std::filesystem::exists(config_.directory, error))
		{
			if (error || !std::filesystem::is_directory(config_.directory, error) || error)
				throw std::invalid_argument("external storage path is not a directory");
			if (!std::filesystem::exists(config_.directory / "CURRENT", error))
			{
				if (error)
					throw std::runtime_error("external storage manifest cannot be inspected");
				for (const auto& entry : std::filesystem::directory_iterator(config_.directory))
				{
					const auto name = entry.path().filename().string();
					const bool checkpoint_pin = name.rfind("CHECKPOINT_", 0U) == 0U && name.size() >= 14U && name.substr(name.size() - 4U) == ".PIN";
					const bool checkpoint_pin_temp = name.rfind("CHECKPOINT_", 0U) == 0U && name.size() >= 18U && name.substr(name.size() - 8U) == ".PIN.tmp";
					if (name != "BATCH.wal" && name != "CURRENT.tmp" && !checkpoint_pin && !checkpoint_pin_temp && name.rfind("run_", 0U) != 0U)
						throw std::invalid_argument("external storage directory contains an unrecognized file without a manifest");
				}
			}
		}
		else
		{
			if (error)
				throw std::runtime_error("external storage directory cannot be inspected");
			std::filesystem::create_directories(config_.directory, error);
			if (error)
				throw std::runtime_error("external storage directory cannot be created");
		}
		if (std::filesystem::exists(config_.directory / "CURRENT"))
			load_manifest();
		cleanup_orphans();
		recover_wal();
	}

	MultiwayExternalRecordStore::~MultiwayExternalRecordStore() = default;

	void MultiwayExternalRecordStore::begin_export_filter() const
	{
		std::lock_guard<std::mutex> lock(mutex_);
		if (failed_ || export_filter_)
			throw std::logic_error("external export filter cannot be started");
		constexpr auto words = export_filter_bytes / sizeof(std::uint64_t);
		auto filter = std::make_unique<std::uint64_t[]>(words);
		constexpr auto mask = export_filter_bytes * 8U - 1U;
		for (const auto& run : runs_)
		{
			if (std::filesystem::file_size(run.index_path) != run.record_count * 8U || std::filesystem::file_size(run.data_path) != record_bytes(run.record_count, payload_bytes_) || run_hash(run) != run.content_hash)
				throw std::invalid_argument("external export filter run validation failed");
			std::ifstream index(run.index_path, std::ios::binary);
			if (!index)
				throw std::runtime_error("external index run cannot be opened");
			std::uint64_t previous = 0U;
			for (std::uint64_t i = 0U; i < run.record_count; ++i)
			{
				std::array<std::uint8_t, 8U> bytes{};
				index.read(reinterpret_cast<char*>(bytes.data()), bytes.size());
				if (!index)
					throw std::runtime_error("external index run is truncated");
				const auto key = read_u64(bytes.data());
				if (i != 0U && key <= previous)
					throw std::invalid_argument("external index run is not strictly ordered");
				previous = key;
				const auto hash = export_filter_hash(key);
				const auto a = hash & mask, b = (hash >> 23U) & mask;
				filter[a / 64U] |= 1ULL << (a % 64U);
				filter[b / 64U] |= 1ULL << (b % 64U);
			}
		}
		export_filter_ = std::move(filter);
	}

	void MultiwayExternalRecordStore::end_export_filter() const noexcept
	{
		std::lock_guard<std::mutex> lock(mutex_);
		export_filter_.reset();
	}

	void MultiwayExternalRecordStore::check_disk_reserve(std::uint64_t additional_bytes) const
	{
		std::error_code error;
		const auto space = std::filesystem::space(config_.directory, error);
		if (error || checked_add(additional_bytes, config_.disk_reserve_bytes) > space.available)
			throw std::runtime_error("external storage disk reserve would be violated");
	}

	void MultiwayExternalRecordStore::flush_file(const std::filesystem::path& path) const
	{
		if (!config_.synchronous_durability)
			return;
#ifdef _WIN32
		const auto handle = CreateFileW(path.c_str(), GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
		if (handle == INVALID_HANDLE_VALUE)
			throw std::runtime_error("external storage file cannot be opened for durable flush");
		const auto ok = FlushFileBuffers(handle);
		CloseHandle(handle);
		if (!ok)
			throw std::runtime_error("external storage durable file flush failed");
#else
		const auto descriptor = open(path.c_str(), O_RDONLY);
		if (descriptor < 0)
			throw std::runtime_error("external storage file cannot be opened for durable flush");
		const auto result = fsync(descriptor);
		close(descriptor);
		if (result != 0)
			throw std::runtime_error("external storage durable file flush failed");
#endif
	}

	void MultiwayExternalRecordStore::flush_directory() const
	{
		if (!config_.synchronous_durability)
			return;
#ifdef _WIN32
		// Atomic replacement uses Windows write-through semantics in the shared helper.
#else
		const auto descriptor = open(config_.directory.c_str(), O_RDONLY | O_DIRECTORY);
		if (descriptor < 0)
			throw std::runtime_error("external storage directory cannot be opened for durable flush");
		const auto result = fsync(descriptor);
		close(descriptor);
		if (result != 0)
			throw std::runtime_error("external storage durable directory flush failed");
#endif
	}

	void MultiwayExternalRecordStore::write_wal(
		std::uint64_t batch_number, std::uint64_t merge_fingerprint,
		const std::vector<MultiwayExternalRecord>& records, std::size_t first,
		std::size_t count) const
	{
		const auto wal = config_.directory / "BATCH.wal";
		const auto bytes = checked_add(48U, record_bytes(count, payload_bytes_));
		if (bytes > config_.maximum_wal_bytes)
			throw std::length_error("external storage batch exceeds the WAL limit");
		check_disk_reserve(bytes);
		if (std::filesystem::exists(wal))
			throw std::runtime_error("external storage has an unrecovered WAL");
		std::ofstream output(wal, std::ios::binary | std::ios::trunc);
		if (!output)
			throw std::runtime_error("external storage WAL cannot be opened");
		output.write(reinterpret_cast<const char*>(kWalMagic.data()), static_cast<std::streamsize>(kWalMagic.size()));
		auto wal_hash = kHashOffset;
		for (const auto byte : kWalMagic)
			texas::core::fingerprint::append_u8(wal_hash, byte);
		const auto hash_u64 = [&wal_hash](std::uint64_t value) {
			for (unsigned shift = 0U; shift < 64U; shift += 8U)
				texas::core::fingerprint::append_u8(wal_hash, static_cast<std::uint8_t>(value >> shift));
		};
		hash_u64(batch_number);
		hash_u64(merge_fingerprint);
		hash_u64(count);
		write_u64(output, batch_number);
		write_u64(output, merge_fingerprint);
		write_u64(output, count);
		for (std::size_t record_index = first; record_index < first + count; ++record_index)
		{
			const auto& record = records[record_index];
			write_u64(output, record.key);
			output.write(reinterpret_cast<const char*>(record.payload.data()), static_cast<std::streamsize>(record.payload.size()));
			hash_u64(record.key);
			for (const auto byte : record.payload)
				texas::core::fingerprint::append_u8(wal_hash, byte);
		}
		hash_u64(0x434f4d4d49545445ULL);
		write_u64(output, 0x434f4d4d49545445ULL);
		write_u64(output, texas::core::fingerprint::finish(wal_hash));
		output.flush();
		if (!output)
			throw std::runtime_error("external storage WAL write failed");
		output.close();
		flush_file(wal);
	}

	void MultiwayExternalRecordStore::append_batch(
		std::uint64_t batch_number, std::uint64_t merge_fingerprint,
		const std::vector<MultiwayExternalRecord>& records)
	{
		append_batch_impl(batch_number, merge_fingerprint, records, false);
	}

	void MultiwayExternalRecordStore::append_batch_continuation(
		std::uint64_t batch_number, std::uint64_t merge_fingerprint,
		const std::vector<MultiwayExternalRecord>& records)
	{
		append_batch_impl(batch_number, merge_fingerprint, records, true);
	}

	void MultiwayExternalRecordStore::append_batch_impl(
		std::uint64_t batch_number, std::uint64_t merge_fingerprint,
		const std::vector<MultiwayExternalRecord>& records, bool allow_same_batch)
	{
		const auto maximum_static_records = validate_append_batch_input(batch_number, records);
		std::size_t first = 0U;
		while (first < records.size())
		{
			const auto publication_started = std::chrono::steady_clock::now();
			std::unique_lock<std::mutex> lock(mutex_);
			const auto context = prepare_append_batch_chunk(batch_number, records, first,
				maximum_static_records, allow_same_batch);
			try
			{
				publish_append_batch_chunk(batch_number, merge_fingerprint, records, context);
				stats_.commit_nanoseconds = multiway_memory_saturating_add(stats_.commit_nanoseconds,
					static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
						std::chrono::steady_clock::now() - publication_started)
							.count()));
			}
			catch (...)
			{
				failed_ = true;
				std::error_code ignored;
				std::filesystem::remove(context.data_temp, ignored);
				std::filesystem::remove(context.index_temp, ignored);
				throw;
			}
			const auto observer = config_.publication_observer;
			const auto observer_context = config_.publication_observer_context;
			const auto component = config_.publication_component;
			lock.unlock();
			if (observer != nullptr)
				observer(observer_context, component, context.next_generation, batch_number, context.count);
			first += context.count;
		}
	}

	std::uint64_t MultiwayExternalRecordStore::validate_append_batch_input(
		std::uint64_t batch_number, const std::vector<MultiwayExternalRecord>& records) const
	{
		if (records.empty())
			throw std::invalid_argument("external storage batch number must increase and contain records");
		for (std::size_t index = 0U; index < records.size(); ++index)
		{
			if (records[index].payload.size() != payload_bytes_ || (index != 0U && records[index - 1U].key >= records[index].key))
				throw std::invalid_argument("external storage batch records must be fixed-width and strictly sorted");
		}
		if (config_.aggregate_transaction_covers_append_recovery)
		{
			const auto transaction_wal = config_.disk_accounting_root / "TRAINING_BATCH.wal";
			if (!std::filesystem::exists(transaction_wal) || read_transaction_wal(transaction_wal).batch_number > batch_number)
				throw std::logic_error("bounded aggregate append requires its active transaction journal");
		}
		const auto record_width = checked_add(payload_bytes_, 8U);
		const auto maximum_wal_records = (config_.maximum_wal_bytes - 48U) / record_width;
		const auto maximum_segment_records = config_.segment_target_bytes / record_width;
		const auto maximum_static_records = std::min(maximum_wal_records, maximum_segment_records);
		if (maximum_static_records == 0U || (config_.disk_amplification_limit == 0U && records.size() > maximum_static_records))
			throw std::length_error("external storage batch exceeds its WAL or segment bound");
		return maximum_static_records;
	}

	MultiwayExternalRecordStore::AppendBatchContext
	MultiwayExternalRecordStore::prepare_append_batch_chunk(
		std::uint64_t batch_number, const std::vector<MultiwayExternalRecord>& records,
		std::size_t first, std::uint64_t maximum_static_records, bool allow_same_batch) const
	{
		if (failed_)
			throw std::runtime_error("external storage is unavailable after a failed batch");
		if (export_filter_)
			throw std::logic_error("external storage cannot mutate during export filter session");
		if ((allow_same_batch || first != 0U) ? batch_number < last_batch_number_ : batch_number <= last_batch_number_)
			throw std::invalid_argument("external storage batch number must increase");
		if (generation_ == std::numeric_limits<std::uint64_t>::max())
			throw std::overflow_error("external storage generation overflows");

		const auto remaining = records.size() - first;
		auto count = static_cast<std::size_t>(std::min<std::uint64_t>(maximum_static_records, remaining));
		const auto record_width = checked_add(payload_bytes_, 8U);
		if (config_.disk_amplification_limit != 0U)
		{
			const auto snapshot = read_external_disk_budget_snapshot(config_.disk_accounting_root);
			if (!(snapshot.live_segment_bytes == 0U && generation_ == 0U && batch_number == 1U && runs_.empty() && first == 0U && remaining == 1U))
			{
				const auto disk_limit = disk_amplification_limit_bytes(snapshot, config_.disk_amplification_limit);
				const auto manifest_run_count = checked_add(static_cast<std::uint64_t>(runs_.size()), 1U);
				if (manifest_run_count > (std::numeric_limits<std::uint64_t>::max() - 48U) / 24U)
					throw std::length_error("external storage manifest bound overflows");
				const auto manifest_bytes = checked_add(48U, manifest_run_count * 24U);
				const auto aggregate_recovery = config_.aggregate_transaction_covers_append_recovery;
				const auto fixed_bytes = aggregate_recovery
					? manifest_bytes
					: checked_add(checked_add(48U, manifest_bytes), kGenerationManifestBytes);
				const auto per_record_peak_bytes = aggregate_recovery
					? checked_add(record_width, 8U)
					: checked_add(checked_add(record_width, record_width), 8U);
				if (snapshot.stored_bytes > disk_limit || disk_limit - snapshot.stored_bytes <= fixed_bytes)
					throw std::length_error("external storage disk amplification budget cannot admit a run");
				const auto budget_records = (disk_limit - snapshot.stored_bytes - fixed_bytes) / per_record_peak_bytes;
				if (budget_records == 0U)
					throw std::length_error("external storage disk amplification budget cannot admit a record");
				count = static_cast<std::size_t>(std::min<std::uint64_t>(count, budget_records));
			}
		}

		AppendBatchContext context{};
		context.first = first;
		context.count = count;
		context.next_generation = generation_ + 1U;
		context.data_path = run_path(config_.directory, context.next_generation, ".dat");
		context.data_temp = context.data_path;
		context.data_temp += ".tmp";
		context.index_path = run_path(config_.directory, context.next_generation, ".idx");
		context.index_temp = context.index_path;
		context.index_temp += ".tmp";
		context.batch_bytes = record_bytes(count, payload_bytes_);
		context.index_bytes = record_bytes(count, 0U);
		const auto manifest_run_count = checked_add(static_cast<std::uint64_t>(runs_.size()), 1U);
		if (manifest_run_count > (std::numeric_limits<std::uint64_t>::max() - 48U) / 24U)
			throw std::length_error("external storage manifest bound overflows");
		const auto next_manifest_bytes = checked_add(48U, manifest_run_count * 24U);
		context.wal_bytes = checked_add(48U, context.batch_bytes);
		if (config_.disk_amplification_limit != 0U)
		{
			const auto staged_segment_bytes = checked_add(context.batch_bytes, context.index_bytes);
			const auto staged_manifest_bytes = config_.aggregate_transaction_covers_append_recovery
				? next_manifest_bytes
				: checked_add(next_manifest_bytes, kGenerationManifestBytes);
			const auto physical_peak_bytes = config_.aggregate_transaction_covers_append_recovery
				? checked_add(staged_segment_bytes, staged_manifest_bytes)
				: checked_add(context.wal_bytes,
					  checked_add(staged_segment_bytes, staged_manifest_bytes));
			check_disk_reserve(physical_peak_bytes);
		}
		else
			check_disk_reserve(checked_add(checked_add(context.batch_bytes, context.index_bytes),
				config_.maximum_wal_bytes));
		return context;
	}

	void MultiwayExternalRecordStore::publish_append_batch_chunk(
		std::uint64_t batch_number, std::uint64_t merge_fingerprint,
		const std::vector<MultiwayExternalRecord>& records, const AppendBatchContext& context)
	{
		// The aggregate journal restores the previous CURRENT before component
		// recovery runs; any published but uncommitted runs then become orphans.
		if (!config_.aggregate_transaction_covers_append_recovery)
			write_wal(batch_number, merge_fingerprint, records, context.first, context.count);
		std::ofstream data(context.data_temp, std::ios::binary | std::ios::trunc);
		std::ofstream index(context.index_temp, std::ios::binary | std::ios::trunc);
		if (!data || !index)
			throw std::runtime_error("external storage segment cannot be opened");
		const auto end = context.first + context.count;
		for (std::size_t record_index = context.first; record_index < end; ++record_index)
		{
			const auto& record = records[record_index];
			write_u64(data, record.key);
			data.write(reinterpret_cast<const char*>(record.payload.data()), static_cast<std::streamsize>(record.payload.size()));
			write_u64(index, record.key);
		}
		data.flush();
		index.flush();
		if (!data || !index)
			throw std::runtime_error("external storage segment write failed");
		data.close();
		index.close();
		flush_file(context.data_temp);
		flush_file(context.index_temp);
		if (std::filesystem::exists(context.data_path) || std::filesystem::exists(context.index_path))
			throw std::runtime_error("external storage generation file already exists");
		texas::core::publish_atomic_replace(context.data_temp, context.data_path,
			"external data segment publish failed", texas::core::AtomicPublishFallbackPolicy::RejectNonAtomicFallback);
		texas::core::publish_atomic_replace(context.index_temp, context.index_path,
			"external index run publish failed", texas::core::AtomicPublishFallbackPolicy::RejectNonAtomicFallback);
		flush_directory();
		Run run{ context.next_generation, context.count, 0U, context.data_path, context.index_path };
		run.minimum_key = records[context.first].key;
		run.maximum_key = records[context.first + context.count - 1U].key;
		run.content_hash = run_hash(run);
		auto next_runs = runs_;
		next_runs.push_back(run);
		publish_manifest(next_runs, context.next_generation, batch_number);
		runs_.swap(next_runs);
		generation_ = context.next_generation;
		record_count_ = checked_add(record_count_, context.count);
		last_batch_number_ = batch_number;
		stats_.publications = multiway_memory_saturating_add(stats_.publications, 1U);
		stats_.published_records = multiway_memory_saturating_add(stats_.published_records, context.count);
		for (std::size_t record_index = context.first; record_index < context.first + context.count; ++record_index)
		{
			const auto key = records[record_index].key;
			auto& cached = cache_[external_cache_slot(key, cache_.size())];
			if (cached.valid && cached.key == key)
				cached.valid = false;
		}
		flush_directory();
		std::error_code ignored;
		std::filesystem::remove(config_.directory / "BATCH.wal", ignored);
	}

	void MultiwayExternalRecordStore::set_publication_observer(
		MultiwayExternalPublicationObserver observer, void* context, std::uint32_t component)
	{
		std::lock_guard<std::mutex> lock(mutex_);
		config_.publication_observer = observer;
		config_.publication_observer_context = context;
		config_.publication_component = component;
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
		if (failed_)
			throw std::runtime_error("external storage is unavailable after a failed batch");
		return lookup_unlocked(key, payload, payload_size);
	}

	bool MultiwayExternalRecordStore::lookup_profiled(std::uint64_t key,
		std::vector<std::uint8_t>& payload, MultiwayExternalRecordLookupProfile& profile) const
	{
		payload.resize(payload_bytes_);
		std::lock_guard<std::mutex> lock(mutex_);
		if (failed_)
			throw std::runtime_error("external storage is unavailable after a failed batch");
		++stats_.lookups;
		auto& cached = cache_[external_cache_slot(key, cache_.size())];
		if (cached.valid && cached.key == key)
		{
			++stats_.cache_hits;
			std::copy_n(cached.payload.data(), payload_bytes_, payload.data());
			return true;
		}
		++stats_.cache_misses;
		if (export_filter_ && !export_filter_maybe_contains(export_filter_.get(), key))
		{
			++profile.membership_misses;
			return false;
		}
		const auto started = std::chrono::steady_clock::now();
		for (auto run = runs_.rbegin(); run != runs_.rend(); ++run)
		{
			if (key < run->minimum_key || key > run->maximum_key)
			{
				profile.run_range_skips = multiway_memory_saturating_add(
					profile.run_range_skips, 1U);
				continue;
			}
			++stats_.disk_reads;
			if (!lookup_run_profiled(*run, key, payload.data(), payload.size(), profile))
				continue;
			auto& entry = cache_[external_cache_slot(key, cache_.size())];
			if (entry.valid)
				++stats_.cache_evictions;
			entry.key = key;
			entry.valid = true;
			std::copy_n(payload.data(), payload_bytes_, entry.payload.data());
			stats_.run_checks = multiway_memory_saturating_add(stats_.run_checks,
				static_cast<std::uint64_t>(run - runs_.rbegin() + 1));
			stats_.read_nanoseconds += static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
				std::chrono::steady_clock::now() - started)
					.count());
			return true;
		}
		stats_.run_checks = multiway_memory_saturating_add(stats_.run_checks, runs_.size());
		stats_.read_nanoseconds += static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
			std::chrono::steady_clock::now() - started)
				.count());
		return false;
	}

	bool MultiwayExternalRecordStore::visit_ordered_records(
		OrderedRecordVisitor visitor, void* context) const
	{
		if (visitor == nullptr)
			throw std::invalid_argument("external ordered-record visitor is missing");
		std::lock_guard<std::mutex> lock(mutex_);
		if (failed_)
			throw std::runtime_error("external storage is unavailable after a failed batch");
		if (runs_.size() > config_.compaction_fan_in)
			throw std::runtime_error("external ordered iteration requires bounded run fan-in");
		if (runs_.empty())
			return true;
		const auto scan_started = std::chrono::steady_clock::now();

#pragma pack(push, 8)
		struct Cursor
		{
			std::ifstream index;
			std::ifstream data;
			std::uint64_t generation = 0U;
			std::uint64_t count = 0U;
			std::uint64_t position = 0U;
			std::uint64_t key = 0U;
			std::uint64_t previous_key = 0U;
			bool has_previous_key = false;
			std::vector<std::uint8_t> payload;
		};
#pragma pack(pop)
#pragma pack(push, 8)
		struct Item
		{
			std::uint64_t key;
			std::uint64_t generation;
			std::size_t cursor;
		};
#pragma pack(pop)
		const auto greater = [](const Item& left, const Item& right) {
			return left.key > right.key || (left.key == right.key && (left.generation < right.generation || (left.generation == right.generation && left.cursor > right.cursor)));
		};
		std::vector<Cursor> cursors(runs_.size());
		std::priority_queue<Item, std::vector<Item>, decltype(greater)> queue(greater);
		const auto read_next = [&](std::size_t cursor_index) {
			auto& cursor = cursors[cursor_index];
			std::array<std::uint8_t, 8U> index_key{};
			cursor.index.read(reinterpret_cast<char*>(index_key.data()),
				static_cast<std::streamsize>(index_key.size()));
			if (!cursor.index)
				throw std::runtime_error("external ordered index run is truncated");
			cursor.key = read_u64(index_key.data());
			std::array<std::uint8_t, 8U> data_key{};
			cursor.data.read(reinterpret_cast<char*>(data_key.data()),
				static_cast<std::streamsize>(data_key.size()));
			if (!cursor.data || read_u64(data_key.data()) != cursor.key)
				throw std::invalid_argument("external ordered index and data keys disagree");
			cursor.data.read(reinterpret_cast<char*>(cursor.payload.data()),
				static_cast<std::streamsize>(payload_bytes_));
			if (!cursor.data || (cursor.has_previous_key && cursor.previous_key >= cursor.key))
				throw std::invalid_argument("external ordered run keys are not strictly increasing");
			cursor.previous_key = cursor.key;
			cursor.has_previous_key = true;
			return true;
		};
		for (std::size_t index = 0U; index < runs_.size(); ++index)
		{
			auto& cursor = cursors[index];
			const auto& run = runs_[index];
			cursor.index.open(run.index_path, std::ios::binary);
			cursor.data.open(run.data_path, std::ios::binary);
			cursor.generation = run.generation;
			cursor.count = run.record_count;
			cursor.payload.resize(payload_bytes_);
			if (!cursor.index || !cursor.data || cursor.count == 0U)
				throw std::runtime_error("external ordered run cannot be opened");
			read_next(index);
			queue.push({ cursor.key, cursor.generation, index });
		}
		std::vector<std::size_t> consumed;
		consumed.reserve(runs_.size());
		while (!queue.empty())
		{
			const auto item = queue.top();
			queue.pop();
			consumed.clear();
			consumed.push_back(item.cursor);
			while (!queue.empty() && queue.top().key == item.key)
			{
				consumed.push_back(queue.top().cursor);
				queue.pop();
			}
			const auto& newest = cursors[item.cursor];
			if (!visitor(context, item.key, newest.payload.data(), payload_bytes_))
			{
				stats_.read_nanoseconds = multiway_memory_saturating_add(
					stats_.read_nanoseconds, static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - scan_started).count()));
				return false;
			}
			for (const auto cursor_index : consumed)
			{
				auto& cursor = cursors[cursor_index];
				++cursor.position;
				if (cursor.position < cursor.count)
				{
					read_next(cursor_index);
					queue.push({ cursor.key, cursor.generation, cursor_index });
				}
			}
		}
		stats_.read_nanoseconds = multiway_memory_saturating_add(
			stats_.read_nanoseconds, static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - scan_started).count()));
		return true;
	}

	bool MultiwayExternalRecordStore::visit_matching_records(
		const std::vector<std::uint64_t>& sorted_keys,
		OrderedRecordVisitor visitor, void* context,
		MultiwayExternalRecordLookupProfile* profile) const
	{
		if (visitor == nullptr)
			throw std::invalid_argument("external matching-record visitor is missing");
		for (std::size_t index = 1U; index < sorted_keys.size(); ++index)
			if (sorted_keys[index - 1U] >= sorted_keys[index])
				throw std::invalid_argument("external matching-record keys must be sorted and unique");
		if (sorted_keys.empty())
			return true;
		std::lock_guard<std::mutex> lock(mutex_);
		if (failed_)
			throw std::runtime_error("external storage is unavailable after a failed batch");
		if (runs_.size() > config_.compaction_fan_in)
			throw std::runtime_error("external matching lookup requires bounded run fan-in");
		if (runs_.empty())
			return true;

#pragma pack(push, 8)
		struct RunReader
		{
			std::ifstream index;
			std::ifstream data;
			std::vector<std::uint8_t> index_buffer;
			std::uint64_t position = 0U;
			std::uint64_t index_buffer_first = 0U;
			std::size_t index_buffer_entries = 0U;
			std::uint64_t current_key = 0U;
			std::uint64_t data_next_position = 0U;
			bool index_initialized = false;
			bool has_current = false;
		};
#pragma pack(pop)
		std::vector<RunReader> readers(runs_.size());
		std::vector<std::uint8_t> payload(payload_bytes_);
		constexpr std::uint64_t kSequentialDataSkipThresholdBytes = 1024U;
		constexpr std::size_t kIndexKeysPerChunk = 4096U;
		const auto load_index_chunk = [profile, kIndexKeysPerChunk](RunReader& reader, const Run& run,
										  std::uint64_t first_position) {
			if (first_position >= run.record_count)
				throw std::logic_error("external matching index chunk starts beyond its run");
			const auto remaining = run.record_count - first_position;
			const auto entry_count = static_cast<std::size_t>(std::min<std::uint64_t>(
				remaining, kIndexKeysPerChunk));
			reader.index_buffer.resize(entry_count * 8U);
			reader.index_buffer_first = first_position;
			reader.index_buffer_entries = entry_count;
			reader.index.read(reinterpret_cast<char*>(reader.index_buffer.data()),
				static_cast<std::streamsize>(reader.index_buffer.size()));
			if (profile != nullptr)
				++profile->index_reads;
			if (!reader.index || reader.index.gcount() != static_cast<std::streamsize>(reader.index_buffer.size()))
				throw std::runtime_error("external matching index run is truncated");
		};
		const auto initialize_index = [profile, &load_index_chunk](RunReader& reader, const Run& run) {
			const auto open_started = std::chrono::steady_clock::now();
			reader.index.open(run.index_path, std::ios::binary);
			if (profile != nullptr)
			{
				++profile->index_opens;
				profile->index_open_nanoseconds = multiway_memory_saturating_add(
					profile->index_open_nanoseconds, static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - open_started).count()));
			}
			if (!reader.index)
				throw std::runtime_error("external matching run cannot be opened");
			reader.index_initialized = true;
			if (run.record_count == 0U)
				return;
			load_index_chunk(reader, run, 0U);
			reader.current_key = read_u64(reader.index_buffer.data());
			reader.has_current = true;
		};
		const auto advance_index = [&load_index_chunk](RunReader& reader, const Run& run) {
			++reader.position;
			if (reader.position == run.record_count)
			{
				reader.has_current = false;
				return;
			}
			if (reader.position >= reader.index_buffer_first + reader.index_buffer_entries)
				load_index_chunk(reader, run, reader.position);
			const auto offset = static_cast<std::size_t>(
									reader.position - reader.index_buffer_first)
				* 8U;
			reader.current_key = read_u64(reader.index_buffer.data() + offset);
		};
		for (const auto key : sorted_keys)
		{
			++stats_.lookups;
			auto& entry = cache_[external_cache_slot(key, cache_.size())];
			if (entry.valid && entry.key == key)
			{
				++stats_.cache_hits;
				if (!visitor(context, key, entry.payload.data(), payload_bytes_))
					return false;
				continue;
			}
			++stats_.cache_misses;
			if (export_filter_ && !export_filter_maybe_contains(export_filter_.get(), key))
				continue;
			const auto started = std::chrono::steady_clock::now();
			bool found = false;
			std::size_t checked_runs = 0U;
			for (std::size_t run_index = runs_.size(); run_index-- > 0U;)
			{
				const auto& run = runs_[run_index];
				++checked_runs;
				if (key < run.minimum_key || key > run.maximum_key)
					continue;
				auto& reader = readers[run_index];
				if (!reader.index_initialized)
					initialize_index(reader, run);
				while (reader.has_current && reader.current_key < key)
					advance_index(reader, run);
				if (!reader.has_current || reader.current_key != key)
					continue;

				if (!reader.data.is_open())
				{
					reader.data.open(run.data_path, std::ios::binary);
					if (!reader.data)
						throw std::runtime_error("external matching data run cannot be opened");
					const auto data_offset = record_bytes(reader.position, payload_bytes_);
					if (data_offset > static_cast<std::uint64_t>(std::numeric_limits<std::streamoff>::max()))
						throw std::length_error("external matching data offset exceeds stream limits");
					reader.data.seekg(static_cast<std::streamoff>(data_offset));
					if (!reader.data)
						throw std::runtime_error("external matching data segment is truncated");
					reader.data_next_position = reader.position;
				}
				if (reader.data_next_position > reader.position)
					throw std::logic_error("external matching data cursor passed its index cursor");
				const auto skipped_records = reader.position - reader.data_next_position;
				const auto skipped_bytes = checked_add(record_bytes(skipped_records, payload_bytes_), 8U);
				if (skipped_bytes > kSequentialDataSkipThresholdBytes)
				{
					const auto data_offset = checked_add(record_bytes(reader.position, payload_bytes_), 8U);
					if (data_offset > static_cast<std::uint64_t>(std::numeric_limits<std::streamoff>::max()))
						throw std::length_error("external matching data offset exceeds stream limits");
					reader.data.clear();
					reader.data.seekg(static_cast<std::streamoff>(data_offset));
					if (!reader.data)
						throw std::runtime_error("external matching data segment is truncated");
				}
				else
				{
					if (skipped_bytes > static_cast<std::uint64_t>(std::numeric_limits<std::streamsize>::max()))
						throw std::length_error("external matching data skip exceeds stream limits");
					reader.data.ignore(static_cast<std::streamsize>(skipped_bytes));
					if (reader.data.gcount() != static_cast<std::streamsize>(skipped_bytes))
						throw std::runtime_error("external matching data segment is truncated");
				}
				reader.data.read(reinterpret_cast<char*>(payload.data()),
					static_cast<std::streamsize>(payload.size()));
				if (!reader.data)
					throw std::runtime_error("external matching data segment is truncated");
				reader.data_next_position = reader.position + 1U;
				found = true;
				break;
			}
			stats_.run_checks = multiway_memory_saturating_add(stats_.run_checks,
				static_cast<std::uint64_t>(checked_runs));
			stats_.read_nanoseconds = multiway_memory_saturating_add(stats_.read_nanoseconds,
				static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
					std::chrono::steady_clock::now() - started)
						.count()));
			if (!found)
				continue;
			if (entry.valid)
				++stats_.cache_evictions;
			entry.key = key;
			entry.valid = true;
			std::copy_n(payload.data(), payload_bytes_, entry.payload.data());
			if (!visitor(context, key, entry.payload.data(), payload_bytes_))
				return false;
		}
		return true;
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
		if (export_filter_ && !export_filter_maybe_contains(export_filter_.get(), key))
			return false;
		const auto started = std::chrono::steady_clock::now();
		for (auto run = runs_.rbegin(); run != runs_.rend(); ++run)
		{
			if (key < run->minimum_key || key > run->maximum_key)
				continue;
			++stats_.disk_reads;
			if (!lookup_run(*run, key, payload, payload_size))
				continue;
			auto& entry = cache_[external_cache_slot(key, cache_.size())];
			if (entry.valid)
				++stats_.cache_evictions;
			entry.key = key;
			entry.valid = true;
			std::copy_n(payload, payload_size, entry.payload.data());
			stats_.run_checks = multiway_memory_saturating_add(stats_.run_checks,
				static_cast<std::uint64_t>(run - runs_.rbegin() + 1));
			stats_.read_nanoseconds += static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - started).count());
			return true;
		}
		stats_.run_checks = multiway_memory_saturating_add(stats_.run_checks, runs_.size());
		stats_.read_nanoseconds += static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - started).count());
		return false;
	}

	bool MultiwayExternalRecordStore::lookup_run(const Run& run, std::uint64_t key,
		std::uint8_t* payload, std::size_t payload_size) const
	{
		std::ifstream index(run.index_path, std::ios::binary);
		if (!index)
			throw std::runtime_error("external index run cannot be opened");
		std::uint64_t first = 0U, last = run.record_count;
		while (first < last)
		{
			const auto middle = first + (last - first) / 2U;
			index.seekg(static_cast<std::streamoff>(middle * 8U));
			std::array<std::uint8_t, 8U> bytes{};
			index.read(reinterpret_cast<char*>(bytes.data()), bytes.size());
			if (!index)
				throw std::runtime_error("external index run is truncated");
			const auto candidate = read_u64(bytes.data());
			if (candidate < key)
				first = middle + 1U;
			else
				last = middle;
		}
		if (first == run.record_count)
			return false;
		index.clear();
		index.seekg(static_cast<std::streamoff>(first * 8U));
		std::array<std::uint8_t, 8U> key_bytes{};
		index.read(reinterpret_cast<char*>(key_bytes.data()), key_bytes.size());
		if (!index)
			throw std::runtime_error("external index run is truncated");
		if (read_u64(key_bytes.data()) != key)
			return false;
		std::ifstream data(run.data_path, std::ios::binary);
		if (!data)
			throw std::runtime_error("external data segment cannot be opened");
		const auto width = payload_bytes_ + 8U;
		data.seekg(static_cast<std::streamoff>(first * width + 8U));
		data.read(reinterpret_cast<char*>(payload), static_cast<std::streamsize>(payload_size));
		if (!data)
			throw std::runtime_error("external data segment is truncated");
		return true;
	}

	bool MultiwayExternalRecordStore::lookup_run_profiled(const Run& run, std::uint64_t key,
		std::uint8_t* payload, std::size_t payload_size, MultiwayExternalRecordLookupProfile& profile) const
	{
		std::ifstream index;
		{
			OptionalExternalTimer timer(&profile.index_open_nanoseconds);
			++profile.index_opens;
			index.open(run.index_path, std::ios::binary);
			if (!index)
				throw std::runtime_error("external index run cannot be opened");
		}
		std::uint64_t first = 0U, last = run.record_count;
		{
			OptionalExternalTimer timer(&profile.index_search_nanoseconds);
			while (first < last)
			{
				const auto middle = first + (last - first) / 2U;
				index.seekg(static_cast<std::streamoff>(middle * 8U));
				std::array<std::uint8_t, 8U> bytes{};
				++profile.index_reads;
				profile.requested_bytes = multiway_memory_saturating_add(profile.requested_bytes, bytes.size());
				index.read(reinterpret_cast<char*>(bytes.data()), bytes.size());
				if (!index)
					throw std::runtime_error("external index run is truncated");
				const auto candidate = read_u64(bytes.data());
				if (candidate < key)
					first = middle + 1U;
				else
					last = middle;
			}
			if (first == run.record_count)
				return false;
			index.clear();
			index.seekg(static_cast<std::streamoff>(first * 8U));
			std::array<std::uint8_t, 8U> key_bytes{};
			++profile.index_reads;
			profile.requested_bytes = multiway_memory_saturating_add(profile.requested_bytes, key_bytes.size());
			index.read(reinterpret_cast<char*>(key_bytes.data()), key_bytes.size());
			if (!index)
				throw std::runtime_error("external index run is truncated");
			if (read_u64(key_bytes.data()) != key)
				return false;
		}
		std::ifstream data;
		{
			OptionalExternalTimer timer(&profile.data_open_nanoseconds);
			++profile.data_opens;
			data.open(run.data_path, std::ios::binary);
			if (!data)
				throw std::runtime_error("external data segment cannot be opened");
		}
		{
			OptionalExternalTimer timer(&profile.data_read_nanoseconds);
			const auto width = payload_bytes_ + 8U;
			data.seekg(static_cast<std::streamoff>(first * width + 8U));
			++profile.data_reads;
			profile.requested_bytes = multiway_memory_saturating_add(profile.requested_bytes, payload_size);
			data.read(reinterpret_cast<char*>(payload), static_cast<std::streamsize>(payload_size));
			if (!data)
				throw std::runtime_error("external data segment is truncated");
		}
		return true;
	}

	void MultiwayExternalRecordStore::load_run_key_range(Run& run) const
	{
		if (run.record_count == 0U || run.record_count > (std::numeric_limits<std::uint64_t>::max() / 8U))
			throw std::invalid_argument("external index run key range has an invalid record count");
		const auto last_offset = (run.record_count - 1U) * 8U;
		if (last_offset > static_cast<std::uint64_t>(std::numeric_limits<std::streamoff>::max()))
			throw std::length_error("external index run key range offset overflows stream offset");
		std::ifstream index(run.index_path, std::ios::binary);
		if (!index)
			throw std::runtime_error("external index run cannot be opened for key range");
		std::array<std::uint8_t, 8U> first_key{};
		index.read(reinterpret_cast<char*>(first_key.data()),
			static_cast<std::streamsize>(first_key.size()));
		if (!index)
			throw std::runtime_error("external index run is truncated at its first key");
		run.minimum_key = read_u64(first_key.data());
		if (run.record_count == 1U)
		{
			run.maximum_key = run.minimum_key;
			return;
		}
		index.clear();
		index.seekg(static_cast<std::streamoff>(last_offset));
		std::array<std::uint8_t, 8U> last_key{};
		index.read(reinterpret_cast<char*>(last_key.data()),
			static_cast<std::streamsize>(last_key.size()));
		if (!index)
			throw std::runtime_error("external index run is truncated at its last key");
		run.maximum_key = read_u64(last_key.data());
		if (run.minimum_key >= run.maximum_key)
			throw std::invalid_argument("external index run key range is not strictly increasing");
	}

	void MultiwayExternalRecordStore::publish_manifest(const std::vector<Run>& runs, std::uint64_t next_generation, std::uint64_t last_batch_number)
	{
		std::vector<std::uint8_t> bytes(kManifestMagic.begin(), kManifestMagic.end());
		append_u32(bytes, kSchema);
		append_u32(bytes, static_cast<std::uint32_t>(payload_bytes_));
		append_u64(bytes, next_generation);
		append_u64(bytes, runs.size());
		append_u64(bytes, last_batch_number);
		for (const auto& run : runs)
		{
			append_u64(bytes, run.generation);
			append_u64(bytes, run.record_count);
			append_u64(bytes, run.content_hash);
		}
		auto hash = kHashOffset;
		for (const auto byte : bytes)
			texas::core::fingerprint::append_u8(hash, byte);
		append_u64(bytes, hash);
		const auto temp = config_.directory / "CURRENT.tmp";
		const auto destination = config_.directory / "CURRENT";
		if (std::filesystem::exists(temp))
			throw std::runtime_error("external manifest temporary path already exists");
		std::ofstream output(temp, std::ios::binary | std::ios::trunc);
		if (!output)
			throw std::runtime_error("external manifest cannot be opened");
		output.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
		output.flush();
		if (!output)
			throw std::runtime_error("external manifest write failed");
		output.close();
		flush_file(temp);
		texas::core::publish_atomic_replace(temp, destination, "external manifest publish failed",
			texas::core::AtomicPublishFallbackPolicy::RejectNonAtomicFallback);
		flush_directory();
		stats_.manifest_bytes_written = multiway_memory_saturating_add(stats_.manifest_bytes_written, bytes.size());
	}

	void MultiwayExternalRecordStore::load_manifest()
	{
		const auto path = config_.directory / "CURRENT";
		std::ifstream input(path, std::ios::binary);
		if (!input)
			throw std::runtime_error("external storage manifest cannot be opened");
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
		for (std::size_t index = 0U; index + 8U < bytes.size(); ++index)
			texas::core::fingerprint::append_u8(hash, bytes[index]);
		if (hash != read_u64(bytes.data() + bytes.size() - 8U))
			throw std::invalid_argument("external storage manifest checksum mismatch");
		std::vector<Run> runs;
		runs.reserve(static_cast<std::size_t>(run_count));
		std::size_t cursor = 40U;
		std::uint64_t total_records = 0U;
		std::uint64_t max_run_generation = 0U;
		for (std::uint64_t index = 0U; index < run_count; ++index)
		{
			Run run;
			run.generation = read_u64(bytes.data() + cursor);
			cursor += 8U;
			run.record_count = read_u64(bytes.data() + cursor);
			cursor += 8U;
			run.content_hash = read_u64(bytes.data() + cursor);
			cursor += 8U;
			if (run.generation <= max_run_generation || run.record_count == 0U)
				throw std::invalid_argument("external storage manifest run order is invalid");
			max_run_generation = run.generation;
			run.data_path = run_path(config_.directory, run.generation, ".dat");
			run.index_path = run_path(config_.directory, run.generation, ".idx");
			if (std::filesystem::file_size(run.data_path) != record_bytes(run.record_count, payload_bytes_) || std::filesystem::file_size(run.index_path) != run.record_count * 8U)
				throw std::invalid_argument("external storage run length is invalid");
			if (run_hash(run) != run.content_hash)
				throw std::invalid_argument("external storage run checksum mismatch");
			load_run_key_range(run);
			total_records = checked_add(total_records, run.record_count);
			runs.push_back(std::move(run));
		}
		if (next_generation < max_run_generation)
			throw std::invalid_argument("external storage generation regressed");
		generation_ = next_generation;
		record_count_ = total_records;
		runs_.swap(runs);
	}

	void MultiwayExternalRecordStore::cleanup_orphans()
	{
		bool removed = false;
		const auto pinned_generations = load_pinned_run_generations(config_.directory);
		for (const auto& entry : std::filesystem::directory_iterator(config_.directory))
		{
			if (!entry.is_regular_file())
				throw std::invalid_argument("external storage directory contains a non-file entry");
			const auto name = entry.path().filename().string();
			if (name == "CURRENT" || name == "BATCH.wal")
				continue;
			bool keep = false;
			if (name.rfind("CHECKPOINT_", 0U) == 0U && name.size() >= 14U && name.substr(name.size() - 4U) == ".PIN")
			{
				(void)read_checkpoint_pin(entry.path());
				keep = true;
			}
			else if (name.rfind("CHECKPOINT_", 0U) == 0U && name.size() >= 18U && name.substr(name.size() - 8U) == ".PIN.tmp")
				keep = false;
			else if (name.rfind("run_", 0U) == 0U)
			{
				const auto separator = name.find('.', 4U);
				if (separator == std::string::npos)
					throw std::invalid_argument("external storage run filename is invalid");
				const auto generation_text = name.substr(4U, separator - 4U);
				std::size_t consumed = 0U;
				std::uint64_t run_generation = 0U;
				try
				{
					run_generation = std::stoull(generation_text, &consumed, 10);
				}
				catch (...)
				{
					throw std::invalid_argument("external storage run generation is invalid");
				}
				if (consumed != generation_text.size())
					throw std::invalid_argument("external storage run generation is invalid");
				const auto suffix = name.substr(separator);
				if (suffix != ".dat" && suffix != ".idx" && suffix != ".dat.tmp" && suffix != ".idx.tmp")
					throw std::invalid_argument("external storage run filename has an invalid suffix");
				if ((suffix == ".dat" || suffix == ".idx") && run_generation <= generation_)
				{
					const auto run = std::lower_bound(runs_.begin(), runs_.end(), run_generation,
						[](const Run& candidate, std::uint64_t generation) { return candidate.generation < generation; });
					if (run != runs_.end() && run->generation == run_generation && entry.path() == (suffix == ".dat" ? run->data_path : run->index_path))
						keep = true;
				}
				if ((suffix == ".dat" || suffix == ".idx") && std::binary_search(pinned_generations.begin(), pinned_generations.end(), run_generation))
					keep = true;
			}
			else if (name != "CURRENT.tmp")
				throw std::invalid_argument("external storage directory contains an unrecognized file");
			if (!keep)
			{
				std::error_code error;
				std::filesystem::remove(entry.path(), error);
				if (error)
					throw std::runtime_error("external storage orphan cannot be removed");
				removed = true;
			}
		}
		if (removed)
			flush_directory();
	}

	void MultiwayExternalRecordStore::recover_wal()
	{
		const auto path = config_.directory / "BATCH.wal";
		if (!std::filesystem::exists(path))
			return;
		const auto file_bytes = std::filesystem::file_size(path);
		if (file_bytes > config_.maximum_wal_bytes)
			throw std::invalid_argument("external storage WAL length is invalid");
		const auto discard_uncommitted = [&]() {
			std::error_code error;
			std::filesystem::remove(path, error);
			if (error)
				throw std::runtime_error("external storage uncommitted WAL cannot be removed");
			if (generation_ != std::numeric_limits<std::uint64_t>::max())
			{
				const auto next_generation = generation_ + 1U;
				for (const auto* suffix : { ".dat", ".idx", ".dat.tmp", ".idx.tmp" })
				{
					std::filesystem::remove(run_path(config_.directory, next_generation, suffix), error);
					if (error)
						throw std::runtime_error("external storage orphan generation cannot be removed");
				}
			}
			std::filesystem::remove(config_.directory / "CURRENT.tmp", error);
			if (error)
				throw std::runtime_error("external storage orphan manifest cannot be removed");
			flush_directory();
		};
		if (file_bytes < 32U)
		{
			discard_uncommitted();
			return;
		}
		std::ifstream input(path, std::ios::binary);
		if (!input)
			throw std::runtime_error("external storage WAL cannot be opened for recovery");
		std::vector<std::uint8_t> bytes(static_cast<std::size_t>(file_bytes));
		input.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
		if (!input || !std::equal(kWalMagic.begin(), kWalMagic.end(), bytes.begin()))
		{
			input.close();
			discard_uncommitted();
			return;
		}
		const auto batch_number = read_u64(bytes.data() + 8U);
		const auto merge_fingerprint = read_u64(bytes.data() + 16U);
		const auto count = read_u64(bytes.data() + 24U);
		input.close();
		if (batch_number <= last_batch_number_)
		{
			std::error_code error;
			std::filesystem::remove(path, error);
			if (error)
				throw std::runtime_error("external storage already committed WAL cannot be removed");
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
			record.key = read_u64(bytes.data() + cursor);
			cursor += 8U;
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
		if (records.empty())
			throw std::invalid_argument("external storage WAL contains no records");
		if (generation_ == std::numeric_limits<std::uint64_t>::max())
			throw std::overflow_error("external storage generation overflows");
		const auto next_generation = generation_ + 1U;
		for (const auto* suffix : { ".dat", ".idx", ".dat.tmp", ".idx.tmp" })
		{
			std::error_code error;
			std::filesystem::remove(run_path(config_.directory, next_generation, suffix), error);
			if (error)
				throw std::runtime_error("external storage orphan generation cannot be removed");
		}
		{
			std::error_code error;
			std::filesystem::remove(config_.directory / "CURRENT.tmp", error);
			if (error)
				throw std::runtime_error("external storage orphan manifest cannot be removed");
		}
		std::error_code remove_error;
		std::filesystem::remove(path, remove_error);
		if (remove_error)
			throw std::runtime_error("external storage recovered WAL cannot be removed");
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

	std::uint64_t MultiwayExternalRecordStore::generation() const
	{
		std::lock_guard<std::mutex> lock(mutex_);
		return generation_;
	}

	std::uint64_t MultiwayExternalRecordStore::record_count() const
	{
		std::lock_guard<std::mutex> lock(mutex_);
		return record_count_;
	}

	std::uint64_t MultiwayExternalRecordStore::last_batch_number() const
	{
		std::lock_guard<std::mutex> lock(mutex_);
		return last_batch_number_;
	}

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

	std::size_t MultiwayExternalRecordStore::cache_capacity_records() const noexcept
	{
		return cache_capacity_;
	}

	std::size_t MultiwayExternalRecordStore::cache_bytes() const noexcept
	{
		return cache_capacity_ * (payload_bytes_ + sizeof(CacheEntry) + 8U);
	}

	MultiwayExternalStorageStats MultiwayExternalRecordStore::stats() const
	{
		std::lock_guard<std::mutex> lock(mutex_);
		auto result = stats_;
		result.run_count = runs_.size();
		return result;
	}

	MultiwayExternalDiskUsage MultiwayExternalRecordStore::disk_usage() const
	{
		std::lock_guard<std::mutex> lock(mutex_);
		MultiwayExternalDiskUsage result{};
		const auto add_run_bytes = [](std::uint64_t& total, const std::filesystem::path& path) {
			std::error_code error;
			const auto bytes = std::filesystem::file_size(path, error);
			if (error)
				throw std::runtime_error("external storage segment size is unavailable");
			total = checked_add(total, bytes);
		};
		for (const auto& run : runs_)
		{
			add_run_bytes(result.live_segment_bytes, run.data_path);
			add_run_bytes(result.live_segment_bytes, run.index_path);
		}
		const auto pinned_generations = load_pinned_run_generations(config_.directory);
		for (const auto generation : pinned_generations)
		{
			const auto active = std::lower_bound(runs_.begin(), runs_.end(), generation,
				[](const Run& run, std::uint64_t value) { return run.generation < value; });
			if (active != runs_.end() && active->generation == generation)
				continue;
			add_run_bytes(result.retained_segment_bytes,
				run_path(config_.directory, generation, ".dat"));
			add_run_bytes(result.retained_segment_bytes,
				run_path(config_.directory, generation, ".idx"));
		}
		return result;
	}
} // namespace texas::solver::multiway
