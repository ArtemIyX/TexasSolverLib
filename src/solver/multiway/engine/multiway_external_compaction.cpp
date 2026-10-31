#include "multiway_external_storage_detail.hpp"

namespace texas::solver::multiway
{
	void MultiwayExternalRecordStore::compact(bool retain_input_runs)
	{
		compact_impl(retain_input_runs, 0U, false, true);
	}

	void MultiwayExternalRecordStore::compact_batch_runs(std::uint64_t base_generation)
	{
		for (;;)
		{
			std::size_t suffix_count = 0U;
			std::size_t run_count = 0U;
			{
				std::lock_guard<std::mutex> lock(mutex_);
				const auto suffix = std::upper_bound(runs_.begin(), runs_.end(), base_generation,
					[](std::uint64_t generation, const Run& run) { return generation < run.generation; });
				suffix_count = static_cast<std::size_t>(runs_.end() - suffix);
				run_count = runs_.size();
			}
			if (suffix_count > 1U)
			{
				compact_impl(false, base_generation, true, false);
				continue;
			}
			if (run_count <= config_.compaction_fan_in)
				return;
			const auto rollback_safe_run_bound =
				2ULL * static_cast<std::uint64_t>(config_.compaction_fan_in) - 1U;
			if (run_count > rollback_safe_run_bound)
				throw std::length_error("external storage run count exceeds the rollback-safe compaction bound");
			compact_impl(true, 0U, false, false);
			{
				std::lock_guard<std::mutex> lock(mutex_);
				if (runs_.size() > config_.compaction_fan_in)
					throw std::length_error("external storage run count remains above the configured fan-in");
			}
			return;
		}
	}

	void MultiwayExternalRecordStore::reclaim_orphans()
	{
		std::lock_guard<std::mutex> lock(mutex_);
		cleanup_orphans();
	}

	struct MultiwayExternalRecordStore::CompactionState
	{
		struct Cursor
		{
			std::ifstream index;
			std::ifstream data;
			std::uint64_t generation = 0U;
			std::uint64_t count = 0U;
			std::uint64_t position = 0U;
			std::uint64_t key = 0U;
			std::vector<std::uint8_t> payload;
		};
		struct Item
		{
			std::uint64_t key = 0U;
			std::uint64_t generation = 0U;
			std::size_t cursor = 0U;
		};
		struct ItemGreater
		{
			bool operator()(const Item& left, const Item& right) const noexcept
			{
				return left.key > right.key || (left.key == right.key && (left.generation < right.generation || (left.generation == right.generation && left.cursor > right.cursor)));
			}
		};

		using Queue = std::priority_queue<Item, std::vector<Item>, ItemGreater>;

		bool read_next(std::size_t index, std::size_t payload_bytes)
		{
			auto& cursor = cursors[index];
			std::array<std::uint8_t, 8U> bytes{};
			cursor.index.read(reinterpret_cast<char*>(bytes.data()), bytes.size());
			if (!cursor.index)
				return false;
			cursor.key = read_u64(bytes.data());
			std::array<std::uint8_t, 8U> data_key_bytes{};
			cursor.data.read(reinterpret_cast<char*>(data_key_bytes.data()), data_key_bytes.size());
			if (!cursor.data || read_u64(data_key_bytes.data()) != cursor.key)
				throw std::invalid_argument("external compaction index and data keys disagree");
			cursor.data.read(reinterpret_cast<char*>(cursor.payload.data()), static_cast<std::streamsize>(payload_bytes));
			if (!cursor.data)
				throw std::runtime_error("external compaction input is truncated");
			return true;
		}

		bool read_next_index(std::size_t index)
		{
			auto& cursor = cursors[index];
			std::array<std::uint8_t, 8U> bytes{};
			cursor.index.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
			if (!cursor.index)
				return false;
			cursor.key = read_u64(bytes.data());
			return true;
		}

		void rewind_data(std::size_t index)
		{
			auto& cursor = cursors[index];
			cursor.data.clear();
			cursor.data.seekg(0, std::ios::beg);
			if (!cursor.data)
				throw std::runtime_error("external compaction data run cannot be rewound");
		}

		std::size_t input_count = 0U;
		std::size_t input_start = 0U;
		std::uint64_t total_records = 0U;
		std::uint64_t input_bytes = 0U;
		std::uint64_t input_index_bytes = 0U;
		std::uint64_t manifest_bytes = 0U;
		std::uint64_t next_generation = 0U;
		std::uint64_t output_records_bound = 0U;
		std::uint64_t compacted_count = 0U;
		std::uint64_t minimum_key = 0U;
		std::uint64_t maximum_key = 0U;
		bool has_key_range = false;
		std::chrono::steady_clock::time_point compaction_started{};
		ExternalDiskBudgetSnapshot disk_snapshot{};
		std::filesystem::path data_path;
		std::filesystem::path data_temp;
		std::filesystem::path index_path;
		std::filesystem::path index_temp;
		std::vector<Cursor> cursors;
		Queue queue;
		std::vector<std::size_t> consumed;
	};

	bool MultiwayExternalRecordStore::select_compaction_inputs(
		std::uint64_t base_generation, bool batch_suffix_only,
		bool cleanup_orphans_before, CompactionState& state)
	{
		if (export_filter_)
			throw std::logic_error("external storage cannot mutate during export filter session");
		const auto suffix = std::upper_bound(runs_.begin(), runs_.end(), base_generation,
			[](std::uint64_t generation, const Run& run) { return generation < run.generation; });
		const auto suffix_count = static_cast<std::size_t>(runs_.end() - suffix);
		if (batch_suffix_only ? suffix_count < 2U : runs_.size() < config_.compaction_fan_in)
			return false;
		state.compaction_started = std::chrono::steady_clock::now();
		if (generation_ == std::numeric_limits<std::uint64_t>::max())
			throw std::overflow_error("external storage generation overflows");
		if (cleanup_orphans_before)
			cleanup_orphans();
		state.input_count = batch_suffix_only
			? std::min<std::size_t>(config_.compaction_fan_in, suffix_count)
			: static_cast<std::size_t>(config_.compaction_fan_in);
		state.input_start = runs_.size() - state.input_count;
		for (std::size_t index = 0U; index < state.input_count; ++index)
		{
			const auto& run = runs_[state.input_start + index];
			state.total_records = checked_add(state.total_records, run.record_count);
			state.input_index_bytes = checked_add(state.input_index_bytes,
				record_bytes(run.record_count, 0U));
			state.input_bytes = checked_add(state.input_bytes,
				checked_add(record_bytes(run.record_count, payload_bytes_),
					record_bytes(run.record_count, 0U)));
		}
		const auto next_run_count = checked_add(runs_.size() - state.input_count, 1U);
		if (next_run_count > (std::numeric_limits<std::uint64_t>::max() - 48U) / 24U)
			throw std::length_error("external compaction manifest bound overflows");
		state.manifest_bytes = checked_add(48U, next_run_count * 24U);
		if (config_.disk_amplification_limit != 0U)
			state.disk_snapshot = read_external_disk_budget_snapshot(config_.disk_accounting_root);
		return true;
	}

	void MultiwayExternalRecordStore::prepare_compaction_merge(CompactionState& state)
	{
		state.next_generation = generation_ + 1U;
		state.data_path = run_path(config_.directory, state.next_generation, ".dat");
		state.index_path = run_path(config_.directory, state.next_generation, ".idx");
		state.data_temp = state.data_path;
		state.data_temp += ".tmp";
		state.index_temp = state.index_path;
		state.index_temp += ".tmp";
		state.cursors.resize(state.input_count);
		state.consumed.reserve(state.input_count);
		for (std::size_t index = 0U; index < state.input_count; ++index)
		{
			state.cursors[index].index.open(runs_[state.input_start + index].index_path, std::ios::binary);
			state.cursors[index].data.open(runs_[state.input_start + index].data_path, std::ios::binary);
			state.cursors[index].generation = runs_[state.input_start + index].generation;
			state.cursors[index].count = runs_[state.input_start + index].record_count;
			state.cursors[index].payload.resize(payload_bytes_);
			if (!state.cursors[index].index || !state.cursors[index].data)
				throw std::runtime_error("external compaction input cannot be read");
		}
		state.output_records_bound = state.total_records;
		if (config_.disk_amplification_limit != 0U)
		{
			for (std::size_t index = 0U; index < state.input_count; ++index)
			{
				if (!state.read_next_index(index))
					throw std::runtime_error("external compaction index run is truncated");
				state.queue.push({ state.cursors[index].key, state.cursors[index].generation, index });
			}
			std::uint64_t distinct_keys = 0U;
			while (!state.queue.empty())
			{
				const auto key = state.queue.top().key;
				state.consumed.clear();
				while (!state.queue.empty() && state.queue.top().key == key)
				{
					state.consumed.push_back(state.queue.top().cursor);
					state.queue.pop();
				}
				distinct_keys = checked_add(distinct_keys, 1U);
				for (const auto cursor_index : state.consumed)
				{
					auto& cursor = state.cursors[cursor_index];
					++cursor.position;
					if (cursor.position < cursor.count)
					{
						if (!state.read_next_index(cursor_index))
							throw std::runtime_error("external compaction index run ended early");
						state.queue.push({ cursor.key, cursor.generation, cursor_index });
					}
				}
			}
			if (distinct_keys == 0U)
				throw std::logic_error("external compaction produced an empty run");
			state.output_records_bound = distinct_keys;
			for (std::size_t index = 0U; index < state.input_count; ++index)
			{
				auto& cursor = state.cursors[index];
				cursor.position = 0U;
				cursor.index.clear();
				cursor.index.seekg(0);
				state.rewind_data(index);
				if (!cursor.index || !state.read_next(index, payload_bytes_))
					throw std::runtime_error("external compaction input cannot be rewound");
				state.queue.push({ cursor.key, cursor.generation, index });
			}
			const auto output_bytes = checked_add(record_bytes(state.output_records_bound, payload_bytes_),
				record_bytes(state.output_records_bound, 0U));
			const auto staged_bytes = checked_add(
				checked_add(output_bytes, state.manifest_bytes), kGenerationManifestBytes);
			check_disk_reserve(staged_bytes);
			const auto disk_limit = disk_amplification_limit_bytes(state.disk_snapshot,
				config_.disk_amplification_limit);
			if (state.disk_snapshot.stored_bytes > disk_limit
				|| staged_bytes > disk_limit - state.disk_snapshot.stored_bytes)
				throw std::length_error("external compaction exceeds the disk amplification bound while staging");
			const auto old_run_count = static_cast<std::uint64_t>(runs_.size());
			if (old_run_count > (std::numeric_limits<std::uint64_t>::max() - 48U) / 24U)
				throw std::length_error("external compaction current manifest bound overflows");
			const auto old_manifest_bytes = checked_add(48U, old_run_count * 24U);
			if (state.disk_snapshot.stored_bytes < old_manifest_bytes
				|| state.disk_snapshot.live_segment_bytes < state.input_bytes)
				throw std::logic_error("external compaction disk snapshot is inconsistent");
			const auto post_publish_bytes = checked_add(
				checked_add(checked_add(state.disk_snapshot.stored_bytes - old_manifest_bytes,
								output_bytes),
					state.manifest_bytes),
				kGenerationManifestBytes);
			const auto post_publish_live_bytes = checked_add(
				state.disk_snapshot.live_segment_bytes - state.input_bytes, output_bytes);
			const ExternalDiskBudgetSnapshot post_publish_snapshot{ post_publish_bytes, post_publish_live_bytes };
			const auto post_publish_limit = disk_amplification_limit_bytes(post_publish_snapshot,
				config_.disk_amplification_limit);
			if (post_publish_bytes > post_publish_limit)
				throw std::length_error("external compaction exceeds the disk amplification bound after publication");
		}
		else
		{
			for (std::size_t index = 0U; index < state.input_count; ++index)
			{
				state.rewind_data(index);
				if (!state.read_next(index, payload_bytes_))
					throw std::runtime_error("external compaction input cannot be read");
				state.queue.push({ state.cursors[index].key, state.cursors[index].generation, index });
			}
			const auto output_peak_bytes = checked_add(
				checked_add(record_bytes(state.total_records, payload_bytes_),
					record_bytes(state.total_records, 0U)),
				state.manifest_bytes);
			check_disk_reserve(output_peak_bytes);
		}
	}

	void MultiwayExternalRecordStore::write_compaction_output(CompactionState& state)
	{
		std::ofstream data(state.data_temp, std::ios::binary | std::ios::trunc);
		std::ofstream index(state.index_temp, std::ios::binary | std::ios::trunc);
		if (!data || !index)
			throw std::runtime_error("external compaction output cannot be opened");
		while (!state.queue.empty())
		{
			const auto item = state.queue.top();
			state.queue.pop();
			state.consumed.clear();
			state.consumed.push_back(item.cursor);
			while (!state.queue.empty() && state.queue.top().key == item.key)
			{
				state.consumed.push_back(state.queue.top().cursor);
				state.queue.pop();
			}
			const auto& newest = state.cursors[item.cursor];
			if (!state.has_key_range)
			{
				state.minimum_key = item.key;
				state.has_key_range = true;
			}
			state.maximum_key = item.key;
			write_u64(data, item.key);
			data.write(reinterpret_cast<const char*>(newest.payload.data()), static_cast<std::streamsize>(payload_bytes_));
			write_u64(index, item.key);
			++state.compacted_count;
			for (const auto cursor_index : state.consumed)
			{
				auto& cursor = state.cursors[cursor_index];
				++cursor.position;
				if (cursor.position < cursor.count)
				{
					if (!state.read_next(cursor_index, payload_bytes_))
						throw std::runtime_error("external compaction input ended early");
					state.queue.push({ cursor.key, cursor.generation, cursor_index });
				}
			}
		}
		if (config_.disk_amplification_limit != 0U
			&& state.compacted_count != state.output_records_bound)
			throw std::logic_error("external compaction output count changed after disk preflight");
		data.flush();
		index.flush();
		if (!data || !index)
			throw std::runtime_error("external compaction write failed");
		data.close();
		index.close();
		flush_file(state.data_temp);
		flush_file(state.index_temp);
	}

	void MultiwayExternalRecordStore::publish_compaction_result(
		bool retain_input_runs, CompactionState& state)
	{
		texas::core::publish_atomic_replace(state.data_temp, state.data_path,
			"external compacted segment publish failed",
			texas::core::AtomicPublishFallbackPolicy::RejectNonAtomicFallback);
		texas::core::publish_atomic_replace(state.index_temp, state.index_path,
			"external compacted index publish failed",
			texas::core::AtomicPublishFallbackPolicy::RejectNonAtomicFallback);
		flush_directory();
		// Close sources before unlinking them; Windows rejects deletion with open handles.
		for (auto& cursor : state.cursors)
		{
			cursor.index.clear();
			cursor.index.close();
			cursor.data.clear();
			cursor.data.close();
			if (cursor.index.fail() || cursor.data.fail())
				throw std::runtime_error("external compaction input cannot be closed");
		}
		Run merged{ state.next_generation, state.compacted_count, 0U,
			state.data_path, state.index_path };
		if (!state.has_key_range)
			throw std::logic_error("external compaction produced an empty run");
		merged.minimum_key = state.minimum_key;
		merged.maximum_key = state.maximum_key;
		merged.content_hash = run_hash(merged);
		auto next_runs = runs_;
		next_runs.erase(next_runs.begin() + static_cast<std::ptrdiff_t>(state.input_start), next_runs.end());
		next_runs.push_back(merged);
		publish_manifest(next_runs, state.next_generation, last_batch_number_);
		flush_directory();
		if (!retain_input_runs)
		{
			const auto pinned_generations = load_pinned_run_generations(config_.directory);
			for (std::size_t index = 0U; index < state.input_count; ++index)
			{
				const auto run_generation = runs_[state.input_start + index].generation;
				if (std::binary_search(pinned_generations.begin(), pinned_generations.end(), run_generation))
					continue;
				for (const auto* path : { &runs_[state.input_start + index].data_path,
						 &runs_[state.input_start + index].index_path })
				{
					std::error_code remove_error;
					const auto removed = std::filesystem::remove(*path, remove_error);
					if (config_.disk_amplification_limit != 0U && (remove_error || !removed))
						throw std::runtime_error("external compaction input cleanup failed within the disk amplification bound");
				}
			}
		}
		runs_.swap(next_runs);
		generation_ = state.next_generation;
		record_count_ = record_count_ - state.total_records + state.compacted_count;
		stats_.compaction_read_bytes = checked_add(stats_.compaction_read_bytes,
			checked_add(state.input_bytes, config_.disk_amplification_limit != 0U ? state.input_index_bytes : 0U));
		stats_.compaction_written_bytes = checked_add(stats_.compaction_written_bytes,
			checked_add(record_bytes(state.compacted_count, payload_bytes_),
				record_bytes(state.compacted_count, 0U)));
		flush_directory();
		stats_.compactions = multiway_memory_saturating_add(stats_.compactions, 1U);
		stats_.compaction_nanoseconds = multiway_memory_saturating_add(stats_.compaction_nanoseconds,
			static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
				std::chrono::steady_clock::now() - state.compaction_started)
					.count()));
	}

	void MultiwayExternalRecordStore::compact_impl(
		bool retain_input_runs, std::uint64_t base_generation, bool batch_suffix_only,
		bool cleanup_orphans_before)
	{
		std::lock_guard<std::mutex> lock(mutex_);
		CompactionState state{};
		if (!select_compaction_inputs(base_generation, batch_suffix_only,
				cleanup_orphans_before, state))
			return;
		struct CompactionFailureLatch
		{
			bool& failed;
			bool complete = false;
			~CompactionFailureLatch() noexcept
			{
				if (!complete)
					failed = true;
			}
		} failure_latch{ failed_ };
		prepare_compaction_merge(state);
		write_compaction_output(state);
		publish_compaction_result(retain_input_runs, state);
		failure_latch.complete = true;
	}

	void MultiwayExternalCompactPages::compact(bool retain_input_runs)
	{
		std::lock_guard<std::mutex> lock(mutex_);
		for (const auto& page : pages_)
			if (page.pin_count != 0U || page.dirty)
				throw std::logic_error("external compact page compaction requires a clean cache");
		store_->compact(retain_input_runs);
	}

	void MultiwayExternalCompactPages::compact_batch_runs(std::uint64_t base_generation)
	{
		std::lock_guard<std::mutex> lock(mutex_);
		for (const auto& page : pages_)
			if (page.pin_count != 0U || page.dirty)
				throw std::logic_error("external compact page compaction requires a clean cache");
		store_->compact_batch_runs(base_generation);
	}

	void MultiwayExternalCompactPages::reclaim_orphans()
	{
		std::lock_guard<std::mutex> lock(mutex_);
		for (const auto& page : pages_)
			if (page.pin_count != 0U || page.dirty)
				throw std::logic_error("external compact page cleanup requires a clean cache");
		store_->reclaim_orphans();
	}

	void MultiwayExternalCompactStorage::compact(bool retain_input_runs)
	{
		rows_.compact(retain_input_runs);
		pages_.compact(retain_input_runs);
	}

	void MultiwayExternalCompactStorage::compact_batch_runs(
		std::uint64_t base_row_generation, std::uint64_t base_page_generation)
	{
		rows_.compact_batch_runs(base_row_generation);
		pages_.compact_batch_runs(base_page_generation);
	}

	void MultiwayExternalCompactStorage::reclaim_orphans()
	{
		rows_.reclaim_orphans();
		pages_.reclaim_orphans();
	}
} // namespace texas::solver::multiway
