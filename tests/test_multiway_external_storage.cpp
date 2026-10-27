#include "solver/multiway/engine/multiway_external_storage.hpp"
#include "solver/multiway/engine/multiway_compact_storage.hpp"
#include "solver/multiway/engine/multiway_solver.hpp"
#include "solver/multiway/abstraction/multiway_action_abstraction.hpp"
#include "core/fingerprint.hpp"
#include "test_harness.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <vector>

namespace
{
	class TestDirectory
	{
	public:
		TestDirectory()
		{
			static std::atomic<std::uint32_t> next{ 0U };
			const auto root = std::filesystem::temp_directory_path();
			for (;;)
			{
				path_ = root / ("texassolver_external_store_" + std::to_string(next.fetch_add(1U)));
				std::error_code error;
				if (std::filesystem::create_directory(path_, error))
					break;
				if (error)
					throw std::runtime_error("test directory cannot be created");
			}
		}
		~TestDirectory()
		{
			std::error_code ignored;
			std::filesystem::remove_all(path_, ignored);
		}
		const std::filesystem::path& path() const noexcept { return path_; }

	private:
		std::filesystem::path path_;
	};

	texas::solver::multiway::MultiwayExternalStorageConfig config(const std::filesystem::path& path)
	{
		return { path, 4096U, 4096U, 4096U, 2U, 8192U, 1U, false };
	}

	void append_u32(std::vector<std::uint8_t>& bytes, std::uint32_t value)
	{
		for (unsigned shift = 0U; shift < 32U; shift += 8U)
			bytes.push_back(static_cast<std::uint8_t>(value >> shift));
	}

	void append_u64(std::vector<std::uint8_t>& bytes, std::uint64_t value)
	{
		for (unsigned shift = 0U; shift < 64U; shift += 8U)
			bytes.push_back(static_cast<std::uint8_t>(value >> shift));
	}

	void write_empty_component_manifests(const std::filesystem::path& directory,
		const texas::solver::multiway::MultiwayExternalGenerationManifest& generation)
	{
		const std::array<std::pair<const char*, std::uint64_t>, 3U> components = { {
			{ "catalog", generation.catalog_generation },
			{ "rows", generation.row_generation },
			{ "pages", generation.page_generation },
		} };
		for (const auto& component : components)
		{
			const auto component_directory = directory / component.first;
			std::filesystem::create_directories(component_directory);
			std::vector<std::uint8_t> bytes{ 'T', 'X', 'M', 'W', 'E', 'X', 'T', '1' };
			append_u32(bytes, 1U);
			append_u32(bytes, 8U);
			append_u64(bytes, component.second);
			append_u64(bytes, 0U);
			append_u64(bytes, generation.committed_batch_number);
			auto hash = texas::core::fingerprint::FNV1A_OFFSET;
			for (const auto byte : bytes)
				texas::core::fingerprint::append_u8(hash, byte);
			append_u64(bytes, texas::core::fingerprint::finish(hash));
			std::ofstream output(component_directory / "CURRENT", std::ios::binary);
			output.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
			if (!output)
				throw std::runtime_error("test component manifest cannot be written");
		}
	}

	std::vector<texas::solver::multiway::MultiwayExternalRecord> records(std::uint64_t start)
	{
		using texas::solver::multiway::MultiwayExternalRecord;
		return { { start, { static_cast<std::uint8_t>(start), 0xa5U, 0x5aU, 0xffU } },
			{ start + 2U, { static_cast<std::uint8_t>(start + 2U), 0xa5U, 0x5aU, 0xffU } } };
	}

	struct OrderedRecordCapture
	{
		std::vector<std::uint64_t> keys;
		std::vector<std::vector<std::uint8_t>> payloads;
		std::size_t stop_after = std::numeric_limits<std::size_t>::max();
	};

	struct OrderedRowCapture
	{
		std::vector<std::uint64_t> state_ids;
		std::vector<std::int32_t> seats;
		std::vector<texas::solver::multiway::MultiwayExternalRowMetadata> rows;
	};

	bool capture_ordered_record(void* context, std::uint64_t key,
		const std::uint8_t* payload, std::size_t payload_size)
	{
		auto& capture = *static_cast<OrderedRecordCapture*>(context);
		capture.keys.push_back(key);
		capture.payloads.emplace_back(payload, payload + payload_size);
		return capture.keys.size() < capture.stop_after;
	}

	bool capture_ordered_row(void* context,
		const texas::solver::multiway::MultiwayExternalRowBinding& binding)
	{
		auto& capture = *static_cast<OrderedRowCapture*>(context);
		capture.state_ids.push_back(binding.public_state().value);
		capture.seats.push_back(binding.seat());
		capture.rows.push_back({ binding.bucket_count(), binding.action_count(),
			binding.first_page_id(), binding.page_count() });
		return true;
	}
} // namespace

TEST_CASE(multiway_external_record_store_round_trips_and_recovers_batches)
{
	using namespace texas::solver::multiway;
	TestDirectory directory;
	{
		MultiwayExternalRecordStore store(config(directory.path()), 4U);
		store.append_batch(1U, 0x1234U, records(10U));
		store.append_batch(3U, 0x5678U, records(20U));
		EXPECT_EQ(store.record_count(), 4U);
		EXPECT_EQ(store.generation(), 2U);
		std::vector<std::uint8_t> payload;
		EXPECT_TRUE(store.lookup(22U, payload));
		EXPECT_EQ(payload[0], 22U);
		EXPECT_TRUE(store.lookup(22U, payload));
		EXPECT_TRUE(!store.lookup(21U, payload));
		const auto stats = store.stats();
		EXPECT_TRUE(stats.cache_hits >= 1U);
		EXPECT_TRUE(stats.cache_misses >= 2U);
		EXPECT_TRUE(stats.disk_reads >= 2U);
	}
	{
		MultiwayExternalRecordStore reopened(config(directory.path()), 4U);
		EXPECT_EQ(reopened.record_count(), 4U);
		EXPECT_EQ(reopened.generation(), 2U);
		std::vector<std::uint8_t> payload;
		EXPECT_TRUE(reopened.lookup(10U, payload));
		EXPECT_EQ(payload[1], 0xa5U);
		EXPECT_TRUE(reopened.cache_capacity_records() > 0U);
		EXPECT_TRUE(reopened.cache_bytes() <= config(directory.path()).cache_bytes);
	}
}

TEST_CASE(multiway_external_profiled_record_lookup_preserves_newest_generation_and_cache)
{
	using namespace texas::solver::multiway;
	TestDirectory directory;
	MultiwayExternalRecordStore store(config(directory.path()), 2U);
	store.append_batch(1U, 0x111U, { { 1U, { 1U, 10U } }, { 4U, { 4U, 10U } } });
	store.append_batch(2U, 0x222U, { { 3U, { 3U, 20U } }, { 4U, { 4U, 20U } } });
	MultiwayExternalRecordLookupProfile profile;
	std::vector<std::uint8_t> payload;
	EXPECT_TRUE(store.lookup_profiled(4U, payload, profile));
	EXPECT_EQ(payload[1], 20U);
	EXPECT_EQ(profile.index_opens, 1U);
	EXPECT_EQ(profile.data_opens, 1U);
	EXPECT_TRUE(store.lookup_profiled(4U, payload, profile));
	EXPECT_EQ(profile.index_opens, 1U);
	EXPECT_TRUE(store.lookup_profiled(1U, payload, profile));
	EXPECT_EQ(payload[1], 10U);
	EXPECT_EQ(profile.index_opens, 2U);
	EXPECT_EQ(profile.data_opens, 2U);
	EXPECT_EQ(profile.run_range_skips, 1U);
	EXPECT_TRUE(!store.lookup_profiled(999U, payload, profile));
	EXPECT_EQ(profile.index_opens, 2U);
	EXPECT_EQ(profile.data_opens, 2U);
	EXPECT_EQ(profile.run_range_skips, 3U);
	EXPECT_TRUE(store.lookup(4U, payload));
	EXPECT_EQ(payload[1], 20U);
	store.append_batch(3U, 0x333U, { { 4U, { 4U, 30U } } });
	EXPECT_TRUE(store.lookup_profiled(4U, payload, profile));
	EXPECT_EQ(payload[1], 30U);
	EXPECT_EQ(profile.index_opens, 3U);
	EXPECT_EQ(profile.data_opens, 3U);
	EXPECT_EQ(profile.run_range_skips, 3U);
	MultiwayExternalRecordStore reopened(config(directory.path()), 2U);
	MultiwayExternalRecordLookupProfile reopened_profile;
	EXPECT_TRUE(reopened.lookup_profiled(1U, payload, reopened_profile));
	EXPECT_EQ(payload[1], 10U);
	EXPECT_EQ(reopened_profile.index_opens, 1U);
	EXPECT_EQ(reopened_profile.run_range_skips, 2U);
}

TEST_CASE(multiway_external_profiled_record_lookup_rejects_truncated_newest_run)
{
	using namespace texas::solver::multiway;
	TestDirectory directory;
	MultiwayExternalRecordStore store(config(directory.path()), 2U);
	store.append_batch(1U, 0x111U, { { 4U, { 4U, 10U } } });
	store.append_batch(2U, 0x222U, { { 4U, { 4U, 20U } } });
	std::filesystem::resize_file(directory.path() / "run_2.dat", 0U);
	MultiwayExternalRecordLookupProfile profile;
	std::vector<std::uint8_t> payload;
	EXPECT_THROW(store.lookup_profiled(4U, payload, profile), std::runtime_error);
	EXPECT_EQ(profile.index_opens, 1U);
	EXPECT_EQ(profile.data_reads, 1U);
}

TEST_CASE(multiway_external_profiled_record_lookup_rejects_truncated_newest_index)
{
	using namespace texas::solver::multiway;
	TestDirectory directory;
	MultiwayExternalRecordStore store(config(directory.path()), 2U);
	store.append_batch(1U, 0x111U, { { 4U, { 4U, 10U } } });
	store.append_batch(2U, 0x222U, { { 4U, { 4U, 20U } } });
	std::filesystem::resize_file(directory.path() / "run_2.idx", 0U);
	MultiwayExternalRecordLookupProfile profile;
	std::vector<std::uint8_t> payload;
	EXPECT_THROW(store.lookup_profiled(4U, payload, profile), std::runtime_error);
	EXPECT_EQ(profile.index_opens, 1U);
	EXPECT_EQ(profile.index_reads, 1U);
	EXPECT_EQ(profile.data_opens, 0U);
}

TEST_CASE(multiway_external_storage_reports_cache_compaction_and_disk_metrics)
{
	using namespace texas::solver::multiway;
	TestDirectory directory;
	MultiwayExternalRecordStore store(config(directory.path()), 4U);
	store.append_batch(1U, 0x111U, records(10U));
	store.append_batch(2U, 0x222U, records(20U));
	std::vector<std::uint8_t> payload;
	EXPECT_TRUE(store.lookup(10U, payload));
	EXPECT_TRUE(store.lookup(10U, payload));
	const auto cache_stats = store.stats();
	EXPECT_TRUE(cache_stats.cache_hits > 0U);
	EXPECT_TRUE(cache_stats.cache_misses > 0U);
	store.compact();
	const auto compacted_stats = store.stats();
	EXPECT_EQ(compacted_stats.compaction_read_bytes, 80U);
	EXPECT_EQ(compacted_stats.compaction_written_bytes, 80U);
	const auto usage = store.disk_usage();
	EXPECT_EQ(usage.live_segment_bytes, 80U);
	EXPECT_EQ(usage.retained_segment_bytes, 0U);
}

TEST_CASE(multiway_external_record_store_visits_latest_records_in_bounded_key_order)
{
	using namespace texas::solver::multiway;
	TestDirectory directory;
	MultiwayExternalRecordStore store(config(directory.path()), 2U);
	store.append_batch(1U, 0x111U, { { 1U, { 1U, 10U } }, { 4U, { 4U, 10U } } });
	store.append_batch(2U, 0x222U, { { 3U, { 3U, 20U } }, { 4U, { 4U, 20U } } });

	OrderedRecordCapture before_compaction;
	EXPECT_TRUE(store.visit_ordered_records(capture_ordered_record, &before_compaction));
	EXPECT_EQ(before_compaction.keys.size(), 3U);
	EXPECT_EQ(before_compaction.keys[0], 1U);
	EXPECT_EQ(before_compaction.keys[1], 3U);
	EXPECT_EQ(before_compaction.keys[2], 4U);
	EXPECT_EQ(before_compaction.payloads[2][1], 20U);

	OrderedRecordCapture stopped;
	stopped.stop_after = 1U;
	EXPECT_EQ(store.visit_ordered_records(capture_ordered_record, &stopped), false);
	EXPECT_EQ(stopped.keys.size(), 1U);

	store.compact();
	OrderedRecordCapture after_compaction;
	EXPECT_TRUE(store.visit_ordered_records(capture_ordered_record, &after_compaction));
	EXPECT_EQ(after_compaction.keys, before_compaction.keys);
	EXPECT_EQ(after_compaction.payloads, before_compaction.payloads);
	store.append_batch(3U, 0x333U, { { 5U, { 5U, 30U } } });
	store.append_batch(4U, 0x444U, { { 6U, { 6U, 40U } } });
	EXPECT_THROW(store.visit_ordered_records(capture_ordered_record, &after_compaction),
		std::runtime_error);
}

TEST_CASE(multiway_external_record_store_uses_bounded_constant_time_cache_slots)
{
	using namespace texas::solver::multiway;
	TestDirectory directory;
	auto settings = config(directory.path());
	settings.cache_bytes = 64U;
	MultiwayExternalRecordStore store(settings, 4U);
	EXPECT_EQ(store.cache_capacity_records(), 1U);
	store.append_batch(1U, 0x55U, records(10U));
	std::vector<std::uint8_t> payload;
	EXPECT_TRUE(store.lookup(10U, payload));
	std::array<std::uint8_t, 4U> fixed_payload{};
	EXPECT_TRUE(store.lookup(10U, fixed_payload.data(), fixed_payload.size()));
	EXPECT_EQ(fixed_payload[0], payload[0]);
	EXPECT_TRUE(store.lookup(12U, payload));
	EXPECT_TRUE(store.lookup(12U, payload));
	const auto stats = store.stats();
	EXPECT_EQ(stats.cache_misses, 2U);
	EXPECT_EQ(stats.cache_hits, 2U);
	EXPECT_EQ(stats.cache_evictions, 1U);
}

TEST_CASE(multiway_external_generation_manifest_is_atomic_and_checksummed)
{
	using namespace texas::solver::multiway;
	TestDirectory directory;
	MultiwayExternalGenerationManifest first{};
	first.generation = 1U;
	first.committed_batch_number = 7U;
	first.merge_fingerprint = 0x1234U;
	first.catalog_generation = 2U;
	first.public_state_count = 100U;
	first.row_generation = 3U;
	first.row_count = 40U;
	first.page_generation = 4U;
	first.page_count = 75U;
	first.logical_value_count = 123U;
	first.content_hash = 0x9876U;
	publish_multiway_external_generation(directory.path(), first, false);
	MultiwayExternalGenerationManifest loaded{};
	EXPECT_TRUE(load_multiway_external_generation(directory.path(), loaded));
	EXPECT_EQ(loaded.generation, first.generation);
	EXPECT_EQ(loaded.committed_batch_number, first.committed_batch_number);
	EXPECT_EQ(loaded.public_state_count, first.public_state_count);
	EXPECT_EQ(loaded.page_count, first.page_count);
	EXPECT_EQ(loaded.logical_value_count, first.logical_value_count);
	EXPECT_EQ(loaded.content_hash, first.content_hash);

	auto second = first;
	second.generation = 2U;
	second.committed_batch_number = 8U;
	std::ofstream(directory.path() / "TRAINING_CURRENT.tmp", std::ios::binary).put('x');
	bool publication_rejected = false;
	try
	{
		publish_multiway_external_generation(directory.path(), second, false);
	}
	catch (const std::runtime_error&)
	{
		publication_rejected = true;
	}
	EXPECT_TRUE(publication_rejected);
	EXPECT_TRUE(load_multiway_external_generation(directory.path(), loaded));
	EXPECT_EQ(loaded.generation, first.generation);

	std::error_code ignored;
	std::filesystem::remove(directory.path() / "TRAINING_CURRENT.tmp", ignored);
	{
		std::fstream current(directory.path() / "TRAINING_CURRENT", std::ios::binary | std::ios::in | std::ios::out);
		char byte = 0;
		current.read(&byte, 1);
		byte ^= 1;
		current.seekp(0);
		current.write(&byte, 1);
	}
	bool corruption_rejected = false;
	try
	{
		load_multiway_external_generation(directory.path(), loaded);
	}
	catch (const std::invalid_argument&)
	{
		corruption_rejected = true;
	}
	EXPECT_TRUE(corruption_rejected);
}

TEST_CASE(multiway_external_generation_manifest_loads_schema_one_fail_closed)
{
	using namespace texas::solver::multiway;
	const auto legacy_bytes = [](std::uint64_t row_count) {
		std::vector<std::uint8_t> bytes{ 'T', 'X', 'M', 'W', 'G', 'E', 'N', '1' };
		const auto append_u32 = [&bytes](std::uint32_t value) {
			for (unsigned shift = 0U; shift < 32U; shift += 8U)
				bytes.push_back(static_cast<std::uint8_t>(value >> shift));
		};
		const auto append_u64 = [&bytes](std::uint64_t value) {
			for (unsigned shift = 0U; shift < 64U; shift += 8U)
				bytes.push_back(static_cast<std::uint8_t>(value >> shift));
		};
		append_u32(1U);
		append_u32(0U);
		for (const auto field : std::array<std::uint64_t, 10U>{
				 1U, 1U, 7U, 0U, 0U, 0U, row_count, 0U, 0U, 9U })
			append_u64(field);
		auto checksum = texas::core::fingerprint::FNV1A_OFFSET;
		for (const auto byte : bytes)
			texas::core::fingerprint::append_u8(checksum, byte);
		append_u64(texas::core::fingerprint::finish(checksum));
		return bytes;
	};
	const auto write_legacy = [&legacy_bytes](const std::filesystem::path& directory,
								  std::uint64_t row_count) {
		std::filesystem::create_directories(directory);
		const auto bytes = legacy_bytes(row_count);
		std::ofstream output(directory / "TRAINING_CURRENT", std::ios::binary);
		output.write(reinterpret_cast<const char*>(bytes.data()),
			static_cast<std::streamsize>(bytes.size()));
	};
	TestDirectory empty_rows_directory;
	write_legacy(empty_rows_directory.path(), 0U);
	MultiwayExternalGenerationManifest loaded{};
	EXPECT_TRUE(load_multiway_external_generation(empty_rows_directory.path(), loaded));
	EXPECT_EQ(loaded.logical_value_count, 0U);
	TestDirectory existing_rows_directory;
	write_legacy(existing_rows_directory.path(), 1U);
	EXPECT_TRUE(load_multiway_external_generation(existing_rows_directory.path(), loaded));
	EXPECT_EQ(loaded.logical_value_count, std::numeric_limits<std::uint64_t>::max());
}

TEST_CASE(multiway_external_checkpoint_manifest_round_trips_and_rejects_identity_drift)
{
	using namespace texas::solver::multiway;
	TestDirectory directory;
	const auto path = directory.path() / "checkpoint.manifest";
	const auto storage_directory = directory.path() / "storage";
	std::filesystem::create_directory(storage_directory);
	MultiwayExternalCheckpointManifest checkpoint{};
	checkpoint.model_identity_hash = 11U;
	checkpoint.workflow_identity_hash = 12U;
	checkpoint.storage_identity_hash = 13U;
	checkpoint.schedule_hash = 14U;
	checkpoint.completed_batches = 5U;
	checkpoint.completed_trajectories = 500U;
	checkpoint.pruned_negative_regrets = 3U;
	checkpoint.late_window_start_batch = 4U;
	checkpoint.terminal_visits = 501U;
	checkpoint.leaf_visits = 502U;
	checkpoint.missing_lookup_requests = 2U;
	checkpoint.preflop_rows = 1U;
	checkpoint.flop_rows = 2U;
	checkpoint.turn_rows = 3U;
	checkpoint.river_rows = 4U;
	checkpoint.street_visits = { 10U, 20U, 30U, 40U };
	checkpoint.linear_iteration_weighting = 1U;
	checkpoint.discounting_enabled = 1U;
	checkpoint.negative_regret_pruning_enabled = 0U;
	checkpoint.late_window_baseline = { 0.25, 0.75 };
	checkpoint.generation = { 7U, 6U, 15U, 4U, 3U, 5U, 2U, 8U, 9U, 10U, 16U };
	checkpoint.wal_generation = 6U;
	write_empty_component_manifests(storage_directory, checkpoint.generation);
	publish_multiway_external_generation(storage_directory, checkpoint.generation, false);
	save_multiway_external_checkpoint_manifest(path, storage_directory, checkpoint, false);
	const auto loaded = load_multiway_external_checkpoint_manifest(path, storage_directory,
		11U, 12U, 13U, 14U);
	EXPECT_EQ(loaded.backend_schema, MULTIWAY_EXTERNAL_NODE_RECORD_SCHEMA);
	EXPECT_EQ(loaded.completed_batches, 5U);
	EXPECT_EQ(loaded.completed_trajectories, 500U);
	EXPECT_EQ(loaded.pruned_negative_regrets, 3U);
	EXPECT_EQ(loaded.late_window_start_batch, 4U);
	EXPECT_EQ(loaded.terminal_visits, 501U);
	EXPECT_EQ(loaded.leaf_visits, 502U);
	EXPECT_EQ(loaded.missing_lookup_requests, 2U);
	EXPECT_EQ(loaded.preflop_rows, 1U);
	EXPECT_EQ(loaded.flop_rows, 2U);
	EXPECT_EQ(loaded.turn_rows, 3U);
	EXPECT_EQ(loaded.river_rows, 4U);
	EXPECT_EQ(loaded.street_visits[0], 10U);
	EXPECT_EQ(loaded.street_visits[3], 40U);
	EXPECT_EQ(loaded.linear_iteration_weighting, 1U);
	EXPECT_EQ(loaded.discounting_enabled, 1U);
	EXPECT_EQ(loaded.negative_regret_pruning_enabled, 0U);
	EXPECT_EQ(loaded.late_window_baseline.size(), 2U);
	EXPECT_NEAR(loaded.late_window_baseline[0], 0.25, 0.0);
	EXPECT_NEAR(loaded.late_window_baseline[1], 0.75, 0.0);
	EXPECT_EQ(loaded.generation.content_hash, 16U);
	auto inconsistent_progress = checkpoint;
	inconsistent_progress.completed_batches = checkpoint.generation.committed_batch_number;
	EXPECT_THROW(save_multiway_external_checkpoint_manifest(
					 directory.path() / "invalid.manifest", storage_directory, inconsistent_progress, false),
		std::invalid_argument);
	bool model_mismatch_rejected = false;
	try
	{
		static_cast<void>(load_multiway_external_checkpoint_manifest(path, storage_directory,
			99U, 12U, 13U, 14U));
	}
	catch (const std::invalid_argument&)
	{
		model_mismatch_rejected = true;
	}
	EXPECT_TRUE(model_mismatch_rejected);
	auto newer_generation = checkpoint.generation;
	++newer_generation.generation;
	auto committed_newer_generation = newer_generation;
	committed_newer_generation.committed_batch_number = 7U;
	committed_newer_generation.merge_fingerprint = 17U;
	write_empty_component_manifests(storage_directory, committed_newer_generation);
	publish_multiway_external_generation(storage_directory, committed_newer_generation, false);
	const auto older = load_multiway_external_checkpoint_manifest(path, storage_directory,
		11U, 12U, 13U, 14U);
	EXPECT_EQ(older.generation.generation, 7U);
	write_empty_component_manifests(storage_directory, checkpoint.generation);
	publish_multiway_external_generation(storage_directory, checkpoint.generation, false);
	{
		std::fstream file(path, std::ios::binary | std::ios::in | std::ios::out);
		file.seekg(-1, std::ios::end);
		char checksum_byte = 0;
		file.read(&checksum_byte, 1);
		checksum_byte ^= 0x01;
		file.seekp(-1, std::ios::end);
		file.write(&checksum_byte, 1);
	}
	bool checksum_mismatch_rejected = false;
	try
	{
		static_cast<void>(load_multiway_external_checkpoint_manifest(path, storage_directory,
			11U, 12U, 13U, 14U));
	}
	catch (const std::invalid_argument&)
	{
		checksum_mismatch_rejected = true;
	}
	EXPECT_TRUE(checksum_mismatch_rejected);
}

TEST_CASE(multiway_external_checkpoint_capture_uses_published_generation_without_payload_export)
{
	using namespace texas::solver::multiway;
	TestDirectory directory;
	MultiwayExternalGenerationManifest generation{};
	generation.generation = 9U;
	generation.committed_batch_number = 6U;
	generation.merge_fingerprint = 41U;
	generation.catalog_generation = 7U;
	generation.public_state_count = 12U;
	generation.row_generation = 8U;
	generation.row_count = 3U;
	generation.page_generation = 9U;
	generation.page_count = 4U;
	generation.logical_value_count = 16U;
	generation.content_hash = 55U;
	write_empty_component_manifests(directory.path(), generation);
	publish_multiway_external_generation(directory.path(), generation, false);

	const auto checkpoint = make_multiway_external_checkpoint_manifest(
		directory.path(), 11U, 12U, 13U, 14U, 500U);
	EXPECT_EQ(checkpoint.model_identity_hash, 11U);
	EXPECT_EQ(checkpoint.workflow_identity_hash, 12U);
	EXPECT_EQ(checkpoint.storage_identity_hash, 13U);
	EXPECT_EQ(checkpoint.schedule_hash, 14U);
	EXPECT_EQ(checkpoint.completed_trajectories, 500U);
	EXPECT_EQ(checkpoint.wal_generation, generation.committed_batch_number);
	EXPECT_EQ(checkpoint.generation.content_hash, generation.content_hash);
	EXPECT_TRUE(checkpoint.segments[0].empty());

	EXPECT_THROW(make_multiway_external_checkpoint_manifest(
					 directory.path(), 0U, 12U, 13U, 14U, 500U),
		std::invalid_argument);
}

TEST_CASE(multiway_external_checkpoint_manifest_loads_legacy_schema6_without_training_extension)
{
	using namespace texas::solver::multiway;
	TestDirectory directory;
	const auto path = directory.path() / "checkpoint.manifest";
	MultiwayExternalCheckpointManifest checkpoint{};
	checkpoint.model_identity_hash = 11U;
	checkpoint.workflow_identity_hash = 12U;
	checkpoint.storage_identity_hash = 13U;
	checkpoint.schedule_hash = 14U;
	checkpoint.completed_trajectories = 20U;
	checkpoint.generation = { 3U, 2U, 15U, 2U, 2U, 2U, 1U, 2U, 2U, 4U, 16U };
	checkpoint.wal_generation = 2U;
	write_empty_component_manifests(directory.path(), checkpoint.generation);
	publish_multiway_external_generation(directory.path(), checkpoint.generation, false);
	save_multiway_external_checkpoint_manifest(path, directory.path(), checkpoint, false);

	std::ifstream input(path, std::ios::binary);
	std::vector<std::uint8_t> legacy((std::istreambuf_iterator<char>(input)),
		std::istreambuf_iterator<char>());
	constexpr std::size_t kTrainingExtensionBytes = 132U;
	EXPECT_TRUE(legacy.size() > kTrainingExtensionBytes + 8U);
	legacy.resize(legacy.size() - kTrainingExtensionBytes - 8U);
	auto checksum = texas::core::fingerprint::FNV1A_OFFSET;
	for (const auto byte : legacy)
		texas::core::fingerprint::append_u8(checksum, byte);
	append_u64(legacy, texas::core::fingerprint::finish(checksum));
	std::ofstream output(path, std::ios::binary | std::ios::trunc);
	output.write(reinterpret_cast<const char*>(legacy.data()),
		static_cast<std::streamsize>(legacy.size()));
	EXPECT_TRUE(static_cast<bool>(output));
	output.close();

	const auto loaded = load_multiway_external_checkpoint_manifest(path, directory.path(),
		11U, 12U, 13U, 14U);
	EXPECT_EQ(loaded.completed_trajectories, 20U);
	EXPECT_EQ(loaded.completed_batches, 0U);
	EXPECT_TRUE(loaded.late_window_baseline.empty());
}

TEST_CASE(multiway_external_checkpoint_manifest_publication_failure_preserves_prior_file)
{
	using namespace texas::solver::multiway;
	TestDirectory directory;
	const auto path = directory.path() / "checkpoint.manifest";
	const auto storage_directory = directory.path() / "storage";
	std::filesystem::create_directory(storage_directory);
	MultiwayExternalCheckpointManifest checkpoint{};
	checkpoint.model_identity_hash = 11U;
	checkpoint.workflow_identity_hash = 12U;
	checkpoint.storage_identity_hash = 13U;
	checkpoint.schedule_hash = 14U;
	checkpoint.generation = { 7U, 6U, 15U, 4U, 3U, 5U, 2U, 8U, 9U, 10U, 16U };
	checkpoint.wal_generation = 6U;
	write_empty_component_manifests(storage_directory, checkpoint.generation);
	publish_multiway_external_generation(storage_directory, checkpoint.generation, false);
	save_multiway_external_checkpoint_manifest(path, storage_directory, checkpoint, false);
	auto next = checkpoint;
	next.completed_trajectories = 600U;
	bool publication_failed = false;
	try
	{
		save_multiway_external_checkpoint_manifest(path, storage_directory, next, false, { true });
	}
	catch (const std::runtime_error&)
	{
		publication_failed = true;
	}
	EXPECT_TRUE(publication_failed);
	EXPECT_TRUE(!std::filesystem::exists(path.string() + ".tmp"));
	EXPECT_THROW(save_multiway_external_checkpoint_manifest(
					 path, storage_directory, next, false, {}, std::numeric_limits<std::uint64_t>::max()),
		std::length_error);
	const auto loaded = load_multiway_external_checkpoint_manifest(path, storage_directory,
		11U, 12U, 13U, 14U);
	EXPECT_EQ(loaded.generation.generation, 7U);
}

TEST_CASE(multiway_external_checkpoint_parallel_segment_validation_rejects_corruption)
{
	using namespace texas::solver::multiway;
	TestDirectory directory;
	constexpr std::uint64_t kBatchCount = 12U;
	const std::array<std::filesystem::path, 3U> component_paths = {
		directory.path() / "catalog", directory.path() / "rows", directory.path() / "pages"
	};
	std::array<std::uint64_t, 3U> component_generations{};
	for (std::size_t component = 0U; component < component_paths.size(); ++component)
	{
		MultiwayExternalRecordStore store(config(component_paths[component]), 4U);
		for (std::uint64_t batch = 1U; batch <= kBatchCount; ++batch)
			store.append_batch(batch, batch, records(batch * 10U));
		component_generations[component] = store.generation();
	}
	MultiwayExternalGenerationManifest generation{};
	generation.generation = 1U;
	generation.committed_batch_number = kBatchCount;
	generation.merge_fingerprint = 99U;
	generation.catalog_generation = component_generations[0];
	generation.public_state_count = kBatchCount;
	generation.row_generation = component_generations[1];
	generation.row_count = kBatchCount;
	generation.page_generation = component_generations[2];
	generation.page_count = kBatchCount;
	generation.logical_value_count = kBatchCount;
	generation.content_hash = 100U;
	publish_multiway_external_generation(directory.path(), generation, false);
	const auto checkpoint_path = directory.path() / "checkpoint.manifest";
	auto checkpoint = make_multiway_external_checkpoint_manifest(
		directory.path(), 11U, 12U, 13U, 14U, 1000U);
	EXPECT_TRUE(checkpoint.segments[2].size() >= 4U);
	save_multiway_external_checkpoint_manifest(checkpoint_path, directory.path(), checkpoint, false);
	const auto loaded = load_multiway_external_checkpoint_manifest(
		checkpoint_path, directory.path(), 11U, 12U, 13U, 14U);
	EXPECT_EQ(loaded.segments[2].size(), checkpoint.segments[2].size());

	const auto corrupted_segment = component_paths[2] / ("run_" + std::to_string(checkpoint.segments[2].back().generation) + ".dat");
	std::fstream file(corrupted_segment, std::ios::binary | std::ios::in | std::ios::out);
	char byte = 0;
	file.read(&byte, 1);
	EXPECT_TRUE(static_cast<bool>(file));
	byte ^= 0x01;
	file.seekp(0);
	file.write(&byte, 1);
	EXPECT_TRUE(static_cast<bool>(file));
	file.close();
	EXPECT_THROW(load_multiway_external_checkpoint_manifest(
					 checkpoint_path, directory.path(), 11U, 12U, 13U, 14U),
		std::invalid_argument);
}

TEST_CASE(multiway_external_reopen_removes_only_unreferenced_and_temporary_runs)
{
	using namespace texas::solver::multiway;
	TestDirectory directory;
	{
		MultiwayExternalRecordStore store(config(directory.path()), 4U);
		store.append_batch(1U, 1U, records(1U));
		store.append_batch(2U, 2U, records(10U));
		store.append_batch(3U, 3U, records(20U));
		store.compact();
	}
	for (const auto* name : { "run_2.dat", "run_2.idx", "run_01.dat", "run_4.dat.tmp",
			 "run_99.idx", "CURRENT.tmp" })
	{
		std::ofstream output(directory.path() / name, std::ios::binary);
		output.put('x');
		EXPECT_TRUE(static_cast<bool>(output));
	}
	{
		MultiwayExternalRecordStore reopened(config(directory.path()), 4U);
		std::vector<std::uint8_t> payload;
		EXPECT_TRUE(reopened.lookup(1U, payload));
		EXPECT_EQ(payload[0], 1U);
		EXPECT_TRUE(reopened.lookup(20U, payload));
		EXPECT_EQ(payload[0], 20U);
		for (const auto* name : { "run_1.dat", "run_1.idx", "run_4.dat", "run_4.idx" })
			EXPECT_TRUE(std::filesystem::exists(directory.path() / name));
		for (const auto* name : { "run_2.dat", "run_2.idx", "run_01.dat", "run_4.dat.tmp",
				 "run_99.idx", "CURRENT.tmp" })
			EXPECT_TRUE(!std::filesystem::exists(directory.path() / name));
	}
}

TEST_CASE(multiway_external_checkpoint_pins_runs_during_compaction)
{
	using namespace texas::solver::multiway;
	TestDirectory directory;
	const std::array<std::filesystem::path, 3U> component_paths = {
		directory.path() / "catalog", directory.path() / "rows", directory.path() / "pages"
	};
	for (const auto& component_path : component_paths)
	{
		MultiwayExternalRecordStore store(config(component_path), 4U);
		store.append_batch(1U, 1U, records(1U));
		store.append_batch(2U, 2U, records(8U));
	}
	MultiwayExternalGenerationManifest generation{};
	generation.generation = 1U;
	generation.committed_batch_number = 2U;
	generation.merge_fingerprint = 2U;
	generation.catalog_generation = generation.row_generation = generation.page_generation = 2U;
	generation.content_hash = 3U;
	publish_multiway_external_generation(directory.path(), generation, false);
	MultiwayExternalCheckpointManifest checkpoint{};
	checkpoint.model_identity_hash = 11U;
	checkpoint.workflow_identity_hash = 12U;
	checkpoint.storage_identity_hash = 13U;
	checkpoint.schedule_hash = 14U;
	checkpoint.generation = generation;
	checkpoint.wal_generation = generation.committed_batch_number;
	const auto checkpoint_path = directory.path() / "checkpoint.manifest";
	save_multiway_external_checkpoint_manifest(checkpoint_path, directory.path(), checkpoint, false);
	{
		MultiwayExternalRecordStore store(config(component_paths[0]), 4U);
		store.append_batch(3U, 3U, records(20U));
		store.compact();
		EXPECT_EQ(store.generation(), 4U);
		EXPECT_TRUE(std::filesystem::exists(component_paths[0] / "run_2.dat"));
	}
	for (const auto* name : { "run_0002.dat", "run_2.dat.tmp" })
	{
		std::ofstream output(component_paths[0] / name, std::ios::binary);
		output.put('x');
		EXPECT_TRUE(static_cast<bool>(output));
	}
	MultiwayExternalRecordStore reopened(config(component_paths[0]), 4U);
	EXPECT_TRUE(std::filesystem::exists(component_paths[0] / "run_1.dat"));
	EXPECT_TRUE(std::filesystem::exists(component_paths[0] / "run_2.dat"));
	EXPECT_TRUE(std::filesystem::exists(component_paths[0] / "run_0002.dat"));
	EXPECT_TRUE(!std::filesystem::exists(component_paths[0] / "run_2.dat.tmp"));
	const auto usage = reopened.disk_usage();
	EXPECT_TRUE(usage.live_segment_bytes > 0U);
	EXPECT_EQ(usage.retained_segment_bytes,
		std::filesystem::file_size(component_paths[0] / "run_2.dat") + std::filesystem::file_size(component_paths[0] / "run_2.idx"));
	std::vector<std::uint8_t> payload;
	EXPECT_TRUE(reopened.lookup(10U, payload));
	EXPECT_EQ(payload[0], 10U);
	const auto restored_checkpoint = load_multiway_external_checkpoint_manifest(
		checkpoint_path, directory.path(), 11U, 12U, 13U, 14U);
	EXPECT_EQ(restored_checkpoint.generation.generation, 1U);
	MultiwayExternalGenerationManifest advanced{};
	advanced.generation = 2U;
	advanced.committed_batch_number = 3U;
	advanced.merge_fingerprint = 3U;
	advanced.catalog_generation = 4U;
	advanced.row_generation = advanced.page_generation = 2U;
	advanced.content_hash = 4U;
	publish_multiway_external_generation(directory.path(), advanced, false);
	bool partial_restore_failed = false;
	try
	{
		static_cast<void>(restore_multiway_external_checkpoint(
			checkpoint_path, directory.path(), 11U, 12U, 13U, 14U, false, { 1U, false }));
	}
	catch (const std::runtime_error&)
	{
		partial_restore_failed = true;
	}
	EXPECT_TRUE(partial_restore_failed);
	MultiwayExternalGenerationManifest after_failed_restore{};
	EXPECT_TRUE(load_multiway_external_generation(directory.path(), after_failed_restore));
	EXPECT_EQ(after_failed_restore.generation, 2U);
	bool interrupted_restore = false;
	try
	{
		static_cast<void>(restore_multiway_external_checkpoint(
			checkpoint_path, directory.path(), 11U, 12U, 13U, 14U, false, { 1U, false, true }));
	}
	catch (const std::runtime_error&)
	{
		interrupted_restore = true;
	}
	EXPECT_TRUE(interrupted_restore);
	MultiwayExternalBatchTransaction recovered_restore(directory.path(), false);
	EXPECT_EQ(recovered_restore.current_manifest().generation, 2U);
	const auto restored = restore_multiway_external_checkpoint(
		checkpoint_path, directory.path(), 11U, 12U, 13U, 14U, false);
	EXPECT_EQ(restored.generation.generation, 1U);
	MultiwayExternalGenerationManifest live{};
	EXPECT_TRUE(load_multiway_external_generation(directory.path(), live));
	EXPECT_EQ(live.generation, 1U);
	{
		MultiwayExternalRecordStore old_generation(config(component_paths[0]), 4U);
		EXPECT_EQ(old_generation.generation(), 2U);
		EXPECT_EQ(old_generation.record_count(), 4U);
	}
	std::filesystem::remove(checkpoint_path);
	release_multiway_external_checkpoint_manifest_pins(checkpoint_path, directory.path(), 13U, false);
	{
		MultiwayExternalRecordStore old_generation(config(component_paths[0]), 4U);
		old_generation.append_batch(3U, 3U, records(20U));
		old_generation.compact();
	}
	MultiwayExternalRecordStore after_release(config(component_paths[0]), 4U);
	EXPECT_TRUE(!std::filesystem::exists(component_paths[0] / "run_2.dat"));
	EXPECT_EQ(after_release.record_count(), 6U);
}

TEST_CASE(multiway_external_batch_transaction_rolls_back_partial_component_publication)
{
	using namespace texas::solver::multiway;
	TestDirectory directory;
	const auto catalog_path = directory.path() / "catalog";
	const auto rows_path = directory.path() / "rows";
	const auto pages_path = directory.path() / "pages";
	const std::vector<MultiwayExternalRecord> initial = { { 1U, { 1U, 2U, 3U, 4U } } };
	for (const auto& path : { catalog_path, rows_path, pages_path })
	{
		MultiwayExternalRecordStore store(config(path), 4U);
		store.append_batch(1U, 0x111U, initial);
	}
	MultiwayExternalGenerationManifest first{};
	first.generation = 1U;
	first.committed_batch_number = 1U;
	first.merge_fingerprint = 0x111U;
	first.catalog_generation = first.row_generation = first.page_generation = 1U;
	first.public_state_count = first.row_count = first.page_count = 1U;
	first.logical_value_count = 1U;
	publish_multiway_external_generation(directory.path(), first, false);
	{
		MultiwayExternalBatchTransaction transaction(directory.path(), false);
		transaction.begin_batch(2U, 0x222U);
		for (const auto& path : { catalog_path, rows_path })
		{
			MultiwayExternalRecordStore store(config(path), 4U);
			store.append_batch(2U, 0x222U, { { 2U, { 5U, 6U, 7U, 8U } } });
		}
	}
	MultiwayExternalBatchTransaction recovered(directory.path(), false);
	EXPECT_EQ(recovered.current_manifest().committed_batch_number, 1U);
	for (const auto& path : { catalog_path, rows_path, pages_path })
	{
		MultiwayExternalRecordStore store(config(path), 4U);
		std::vector<std::uint8_t> payload;
		EXPECT_TRUE(store.lookup(1U, payload));
		EXPECT_TRUE(!store.lookup(2U, payload));
		EXPECT_EQ(store.generation(), 1U);
	}
}

TEST_CASE(multiway_external_batch_transaction_publishes_one_verified_commit_point)
{
	using namespace texas::solver::multiway;
	TestDirectory directory;
	const auto catalog_path = directory.path() / "catalog";
	const auto rows_path = directory.path() / "rows";
	const auto pages_path = directory.path() / "pages";
	for (const auto& path : { catalog_path, rows_path, pages_path })
	{
		MultiwayExternalRecordStore store(config(path), 4U);
		store.append_batch(1U, 0x111U, { { 1U, { 1U, 2U, 3U, 4U } } });
	}
	MultiwayExternalGenerationManifest first{};
	first.generation = 1U;
	first.committed_batch_number = 1U;
	first.merge_fingerprint = 0x111U;
	first.catalog_generation = first.row_generation = first.page_generation = 1U;
	publish_multiway_external_generation(directory.path(), first, false);
	MultiwayExternalBatchTransaction transaction(directory.path(), false);
	transaction.begin_batch(2U, 0U);
	for (const auto& path : { catalog_path, rows_path, pages_path })
	{
		MultiwayExternalRecordStore store(config(path), 4U);
		store.append_batch(2U, 0x222U, { { 2U, { 5U, 6U, 7U, 8U } } });
	}
	auto second = first;
	second.generation = 2U;
	second.committed_batch_number = 2U;
	second.merge_fingerprint = 0x222U;
	second.catalog_generation = second.row_generation = second.page_generation = 2U;
	second.public_state_count = second.row_count = second.page_count = 2U;
	second.logical_value_count = 2U;
	transaction.set_merge_fingerprint(0x222U);
	transaction.commit_batch(second);
	EXPECT_TRUE(!transaction.batch_open());
	EXPECT_EQ(transaction.current_manifest().generation, 2U);
	MultiwayExternalBatchTransaction reopened(directory.path(), false);
	EXPECT_EQ(reopened.current_manifest().committed_batch_number, 2U);
}

TEST_CASE(multiway_external_batch_transaction_rolls_back_when_global_publish_fails)
{
	using namespace texas::solver::multiway;
	TestDirectory directory;
	const auto catalog_path = directory.path() / "catalog";
	const auto rows_path = directory.path() / "rows";
	const auto pages_path = directory.path() / "pages";
	for (const auto& path : { catalog_path, rows_path, pages_path })
	{
		MultiwayExternalRecordStore store(config(path), 4U);
		store.append_batch(1U, 0x111U, { { 1U, { 1U, 2U, 3U, 4U } } });
	}
	MultiwayExternalGenerationManifest first{};
	first.generation = 1U;
	first.committed_batch_number = 1U;
	first.merge_fingerprint = 0x111U;
	first.catalog_generation = first.row_generation = first.page_generation = 1U;
	publish_multiway_external_generation(directory.path(), first, false);
	MultiwayExternalBatchTransaction transaction(directory.path(), false);
	transaction.begin_batch(2U, 0x222U);
	for (const auto& path : { catalog_path, rows_path, pages_path })
	{
		MultiwayExternalRecordStore store(config(path), 4U);
		store.append_batch(2U, 0x222U, { { 2U, { 5U, 6U, 7U, 8U } } });
	}
	auto second = first;
	second.generation = 2U;
	second.committed_batch_number = 2U;
	second.merge_fingerprint = 0x222U;
	second.catalog_generation = second.row_generation = second.page_generation = 2U;
	second.public_state_count = second.row_count = second.page_count = 2U;
	second.logical_value_count = 2U;
	std::ofstream(directory.path() / "TRAINING_CURRENT.tmp", std::ios::binary).put('x');
	bool commit_rejected = false;
	try
	{
		transaction.commit_batch(second);
	}
	catch (const std::runtime_error&)
	{
		commit_rejected = true;
	}
	EXPECT_TRUE(commit_rejected);
	transaction.abort_batch();
	MultiwayExternalBatchTransaction reopened(directory.path(), false);
	EXPECT_EQ(reopened.current_manifest().committed_batch_number, 1U);
	for (const auto& path : { catalog_path, rows_path, pages_path })
	{
		MultiwayExternalRecordStore store(config(path), 4U);
		std::vector<std::uint8_t> payload;
		EXPECT_TRUE(!store.lookup(2U, payload));
		EXPECT_EQ(store.generation(), 1U);
	}
}

TEST_CASE(multiway_external_batch_transaction_recovers_unfinalized_merge_fingerprint)
{
	using namespace texas::solver::multiway;
	TestDirectory directory;
	const auto catalog_path = directory.path() / "catalog";
	const auto rows_path = directory.path() / "rows";
	const auto pages_path = directory.path() / "pages";
	for (const auto& path : { catalog_path, rows_path, pages_path })
	{
		MultiwayExternalRecordStore store(config(path), 4U);
		store.append_batch(1U, 0x111U, { { 1U, { 1U, 2U, 3U, 4U } } });
	}
	MultiwayExternalGenerationManifest first{};
	first.generation = 1U;
	first.committed_batch_number = 1U;
	first.merge_fingerprint = 0x111U;
	first.catalog_generation = first.row_generation = first.page_generation = 1U;
	publish_multiway_external_generation(directory.path(), first, false);
	{
		MultiwayExternalBatchTransaction transaction(directory.path(), false);
		transaction.begin_batch(2U, 0U);
		MultiwayExternalRecordStore rows(config(rows_path), 4U);
		rows.append_batch(2U, 0x222U, { { 2U, { 5U, 6U, 7U, 8U } } });
	}
	MultiwayExternalBatchTransaction recovered(directory.path(), false);
	EXPECT_EQ(recovered.current_manifest().committed_batch_number, 1U);
	MultiwayExternalRecordStore rows(config(rows_path), 4U);
	std::vector<std::uint8_t> payload;
	EXPECT_TRUE(!rows.lookup(2U, payload));
	EXPECT_EQ(rows.generation(), 1U);
}

TEST_CASE(multiway_external_training_storage_commits_components_atomically)
{
	using namespace texas::solver::multiway;
	TestDirectory directory;
	auto catalog_config = config(directory.path() / "catalog");
	const auto catalog_record_width = static_cast<std::uint64_t>(external_public_catalog_record_size()) + 8U;
	catalog_config.maximum_wal_bytes = 48U + catalog_record_width;
	catalog_config.segment_target_bytes = catalog_record_width;
	auto row_config = config(directory.path() / "rows");
	auto page_config = config(directory.path() / "pages");
	page_config.compaction_scratch_bytes = 32768U;
	MultiwayPackedPublicNode node{};
	node.id.value = 1U;
	MultiwayPublicStateDescriptor descriptor{};
	descriptor.id = node.id;
	MultiwayPublicStateDescriptor second_descriptor{};
	second_descriptor.id.value = 2U;
	const MultiwaySparseRowShape shape{ { node.id, 0 }, 2U, 2U };
	std::uint64_t committed_content_hash = 0U;
	{
		MultiwayExternalTrainingStorage storage(directory.path(), catalog_config,
			row_config, page_config, 16U, 65536U, 8U, false);
		storage.begin_batch(1U);
		storage.append_discovered_catalog(0x101U, { descriptor, second_descriptor }, { 1U, 1U });
		storage.admit_rows(0x102U, { shape });
		storage.begin_page_batch();
		MultiwayExternalRowBinding binding{};
		EXPECT_TRUE(storage.compact_storage().lookup_row(shape.infoset, binding));
		EXPECT_EQ(binding.public_state(), node.id);
		EXPECT_EQ(binding.seat(), 0);
		EXPECT_EQ(binding.bucket_count(), shape.bucket_count);
		EXPECT_EQ(binding.action_count(), shape.action_count);
		EXPECT_EQ(binding.first_page_id(), 1U);
		EXPECT_EQ(binding.page_count(), 1U);
		auto page = storage.compact_storage().pin_bucket(binding, 1U, true);
		storage.compact_storage().apply_delta(page, 1U, 2.0, 3.0);
		storage.compact_storage().unpin_bucket(page);
		EXPECT_TRUE(!storage.compact_storage().lookup_row({ { 99U }, 0 }, binding));
		EXPECT_EQ(binding.public_state().value, 0U);
		EXPECT_EQ(binding.seat(), -1);
		EXPECT_EQ(binding.bucket_count(), 0U);
		storage.commit_batch(0xabcU, 1U, 4U);
		EXPECT_EQ(storage.current_manifest().generation, 1U);
		EXPECT_EQ(storage.current_manifest().public_state_count, 2U);
		EXPECT_EQ(storage.current_manifest().row_count, 1U);
		EXPECT_EQ(storage.current_manifest().page_count, 1U);
		EXPECT_EQ(storage.current_manifest().logical_value_count, 4U);
		EXPECT_TRUE(storage.current_manifest().content_hash != 0U);
		committed_content_hash = storage.current_manifest().content_hash;
	}
	{
		MultiwayExternalTrainingStorage reopened(directory.path(), catalog_config,
			row_config, page_config, 16U, 65536U, 8U, false);
		EXPECT_EQ(reopened.current_manifest().committed_batch_number, 1U);
		EXPECT_EQ(reopened.current_manifest().content_hash, committed_content_hash);
		EXPECT_TRUE(reopened.catalog().contains(node.id));
		MultiwayExternalRowBinding binding{};
		EXPECT_TRUE(reopened.compact_storage().lookup_row(shape.infoset, binding));
		std::array<double, 2U> average{};
		reopened.compact_storage().average_strategy_into(binding, 1U,
			average.data(), average.size());
		EXPECT_EQ(average[0], 0.0);
		EXPECT_EQ(average[1], 1.0);
		reopened.begin_batch(2U);
		MultiwayPackedPublicNode child{};
		child.id.value = 3U;
		child.parent_id = node.id;
		MultiwayPublicStateDescriptor child_descriptor{};
		child_descriptor.id = child.id;
		MultiwayPublicStateDescriptor second_child_descriptor{};
		second_child_descriptor.id.value = 4U;
		second_child_descriptor.parent_id = node.id;
		const MultiwaySparseRowShape child_shape{ { child.id, 1 }, 1U, 1U };
		reopened.append_discovered_catalog(0x201U,
			{ child_descriptor, second_child_descriptor }, { 1U, 1U });
		reopened.admit_rows(0x202U, { child_shape });
		reopened.begin_page_batch();
		MultiwayExternalRowBinding child_binding{};
		EXPECT_TRUE(reopened.compact_storage().lookup_row(child_shape.infoset, child_binding));
		auto dirty_page = reopened.compact_storage().pin_bucket(child_binding, 0U, true);
		reopened.compact_storage().apply_delta(dirty_page, 0U, 1.0, 1.0);
		reopened.compact_storage().unpin_bucket(dirty_page);
		std::ofstream(directory.path() / "TRAINING_CURRENT.tmp", std::ios::binary).put('x');
		bool publication_failed = false;
		try
		{
			reopened.commit_batch(0xdefU, 2U, 5U);
		}
		catch (const std::runtime_error&)
		{
			publication_failed = true;
		}
		EXPECT_TRUE(publication_failed);
		reopened.abort_batch();
		EXPECT_EQ(reopened.current_manifest().generation, 1U);
		EXPECT_EQ(reopened.current_manifest().content_hash, committed_content_hash);
		EXPECT_TRUE(!reopened.catalog().contains(child.id));
		EXPECT_TRUE(!reopened.catalog().contains(second_child_descriptor.id));
		EXPECT_TRUE(!reopened.compact_storage().lookup_row(child_shape.infoset, child_binding));
	}
}

TEST_CASE(multiway_external_training_storage_seeds_root_as_a_durable_generation)
{
	using namespace texas::solver::multiway;
	TestDirectory directory;
	auto catalog_config = config(directory.path() / "catalog");
	auto row_config = config(directory.path() / "rows");
	auto page_config = config(directory.path() / "pages");
	page_config.compaction_scratch_bytes = 32768U;
	MultiwayPublicStateDescriptor root{};
	root.id.value = 0x1234U;
	root.canonical_history_id = 0x5678U;
	{
		MultiwayExternalTrainingStorage storage(directory.path(), catalog_config,
			row_config, page_config, 16U, 65536U, 8U, false);
		storage.initialize_root(root);
		EXPECT_EQ(storage.current_manifest().generation, 1U);
		EXPECT_EQ(storage.current_manifest().committed_batch_number, 1U);
		EXPECT_EQ(storage.current_manifest().public_state_count, 1U);
		EXPECT_EQ(storage.current_manifest().row_count, 0U);
		EXPECT_EQ(storage.next_batch_number(), 2U);
		storage.initialize_root(root);
		EXPECT_EQ(storage.current_manifest().generation, 1U);
	}
	MultiwayExternalTrainingStorage reopened(directory.path(), catalog_config,
		row_config, page_config, 16U, 65536U, 8U, false);
	reopened.initialize_root(root);
	EXPECT_EQ(reopened.next_batch_number(), 2U);
	auto conflicting_root = root;
	conflicting_root.canonical_history_id++;
	EXPECT_THROW(reopened.initialize_root(conflicting_root), std::invalid_argument);
}

TEST_CASE(multiway_external_training_generation_hash_is_deterministic)
{
	using namespace texas::solver::multiway;
	const auto run = [] {
		TestDirectory directory;
		auto catalog_config = config(directory.path() / "catalog");
		auto row_config = config(directory.path() / "rows");
		auto page_config = config(directory.path() / "pages");
		page_config.compaction_scratch_bytes = 32768U;
		MultiwayPackedPublicNode node{};
		node.id.value = 1U;
		MultiwayPublicStateDescriptor descriptor{};
		descriptor.id = node.id;
		const MultiwaySparseRowShape shape{ { node.id, 0 }, 2U, 2U };
		MultiwayExternalTrainingStorage storage(directory.path(), catalog_config,
			row_config, page_config, 16U, 65536U, 8U, false);
		storage.begin_batch(1U);
		storage.append_catalog(0x101U, { node }, { descriptor });
		storage.admit_rows(0x102U, { shape });
		storage.begin_page_batch();
		MultiwayExternalRowBinding binding{};
		EXPECT_TRUE(storage.compact_storage().lookup_row(shape.infoset, binding));
		auto page = storage.compact_storage().pin_bucket(binding, 1U, true);
		storage.compact_storage().apply_delta(page, 1U, 2.0, 3.0);
		storage.compact_storage().unpin_bucket(page);
		storage.commit_batch(0xabcU, 1U, 4U);
		return storage.current_manifest().content_hash;
	};
	EXPECT_EQ(run(), run());
}

TEST_CASE(multiway_external_batch_transaction_keeps_published_generation_after_cleanup_failure)
{
	using namespace texas::solver::multiway;
	TestDirectory directory;
	const auto catalog_path = directory.path() / "catalog";
	const auto rows_path = directory.path() / "rows";
	const auto pages_path = directory.path() / "pages";
	for (const auto& path : { catalog_path, rows_path, pages_path })
	{
		MultiwayExternalRecordStore store(config(path), 4U);
		store.append_batch(1U, 0x111U, { { 1U, { 1U, 2U, 3U, 4U } } });
	}
	MultiwayExternalGenerationManifest first{};
	first.generation = 1U;
	first.committed_batch_number = 1U;
	first.merge_fingerprint = 0x111U;
	first.catalog_generation = first.row_generation = first.page_generation = 1U;
	first.public_state_count = first.row_count = first.page_count = 1U;
	first.logical_value_count = 1U;
	publish_multiway_external_generation(directory.path(), first, false);
	MultiwayExternalBatchTransaction transaction(directory.path(), false);
	transaction.begin_batch(2U, 0x222U);
	for (const auto& path : { catalog_path, rows_path, pages_path })
	{
		MultiwayExternalRecordStore store(config(path), 4U);
		store.append_batch(2U, 0x222U, { { 2U, { 5U, 6U, 7U, 8U } } });
	}
	auto second = first;
	second.generation = 2U;
	second.committed_batch_number = 2U;
	second.merge_fingerprint = 0x222U;
	second.catalog_generation = second.row_generation = second.page_generation = 2U;
	second.public_state_count = second.row_count = second.page_count = 2U;
	second.logical_value_count = 2U;
	const auto catalog_backup = directory.path() / "CATALOG_CURRENT.previous";
	std::filesystem::remove(catalog_backup);
	std::filesystem::create_directory(catalog_backup);
	std::ofstream(catalog_backup / "held", std::ios::binary).put('x');
	bool cleanup_failed = false;
	try
	{
		transaction.commit_batch(second);
	}
	catch (const std::runtime_error&)
	{
		cleanup_failed = true;
	}
	EXPECT_TRUE(cleanup_failed);
	MultiwayExternalBatchTransaction reopened(directory.path(), false);
	EXPECT_EQ(reopened.current_manifest().committed_batch_number, 2U);
	for (const auto& path : { catalog_path, rows_path, pages_path })
	{
		MultiwayExternalRecordStore store(config(path), 4U);
		std::vector<std::uint8_t> payload;
		EXPECT_TRUE(store.lookup(2U, payload));
		EXPECT_EQ(store.generation(), 2U);
	}
}

TEST_CASE(multiway_external_public_node_codec_is_packed_and_deterministic)
{
	using namespace texas::solver::multiway;
	MultiwayPackedPublicNode node{};
	node.id.value = 0x0102030405060708ULL;
	node.parent_id.value = 17U;
	node.canonical_history_id = 0x8877665544332211ULL;
	node.betting.seat_count = 6U;
	node.betting.current_player = 4;
	node.betting.last_aggressor = 2;
	node.betting.current_bet = 1200;
	node.betting.last_full_raise_size = 600;
	node.betting.big_blind = 100;
	node.betting.street = texas::core::Street::Turn;
	node.betting.stacks[0] = 50000;
	node.betting.folded[3] = 1U;
	node.board = { 1U, 2U, 3U, 4U, 5U };
	node.board_count = 5U;
	node.board_runout.remaining_board_cards = 1U;
	node.board_runout.chance_only_runout = true;
	node.incoming_kind = MultiwayPublicParentEdgeKind::BettingAction;
	node.incoming_action.action_index = 3U;
	node.incoming_action.target_street_contribution = 1200;
	node.incoming_action.action_menu_id = 0x123456789ULL;
	node.incoming_actor = 4;
	node.dealt_card = 7U;
	node.dealt_cards = { 7U, 8U, 9U };
	node.dealt_card_count = 3U;
	node.transition_board = { 1U, 2U, 3U, 4U, 5U };
	node.transition_board_count = 5U;
	node.legal_menu_handle = 19U;

	std::vector<std::uint8_t> encoded;
	encode_external_public_node(node, encoded);
	EXPECT_EQ(encoded.size(), external_public_node_record_size());
	EXPECT_TRUE(encoded.size() < sizeof(MultiwayPackedPublicNode));
	MultiwayPackedPublicNode decoded{};
	EXPECT_TRUE(decode_external_public_node(encoded.data(), encoded.size(), decoded));
	std::vector<std::uint8_t> reencoded;
	encode_external_public_node(decoded, reencoded);
	EXPECT_TRUE(reencoded == encoded);
	encoded[0] ^= 1U;
	EXPECT_TRUE(!decode_external_public_node(encoded.data(), encoded.size(), decoded));
}

TEST_CASE(multiway_external_catalog_splits_batches_to_wal_and_segment_bounds)
{
	using namespace texas::solver::multiway;
	TestDirectory directory;
	auto bounded_config = config(directory.path());
	const auto record_width = static_cast<std::uint64_t>(external_public_catalog_record_size()) + 8U;
	bounded_config.maximum_wal_bytes = 48U + record_width;
	bounded_config.segment_target_bytes = 2U * record_width;
	MultiwayPackedPublicNode first{};
	first.id.value = 1U;
	MultiwayPackedPublicNode second{};
	second.id.value = 2U;
	MultiwayExternalPublicCatalog catalog(bounded_config, 4U);
	catalog.append_batch(1U, 0x77U, { first, second });
	EXPECT_EQ(catalog.record_count(), 2U);
	EXPECT_EQ(catalog.generation(), 2U);
	MultiwayPackedPublicNode loaded{};
	EXPECT_TRUE(catalog.lookup(first.id, loaded));
	EXPECT_EQ(loaded.id, first.id);
	EXPECT_TRUE(catalog.lookup(second.id, loaded));
	EXPECT_EQ(loaded.id, second.id);
	{
		MultiwayExternalPublicCatalog reopened(bounded_config, 4U);
		EXPECT_EQ(reopened.record_count(), 2U);
		EXPECT_EQ(reopened.generation(), 2U);
		EXPECT_TRUE(reopened.lookup(second.id, loaded));
		EXPECT_EQ(loaded.id, second.id);
	}
}

TEST_CASE(multiway_external_public_catalog_reconstructs_parent_first_after_restart)
{
	using namespace texas::solver::multiway;
	TestDirectory directory;
	MultiwayPackedPublicNode root{};
	root.id.value = 1U;
	MultiwayPackedPublicNode child{};
	child.id.value = 2U;
	child.parent_id.value = root.id.value;
	{
		MultiwayExternalPublicCatalog catalog(config(directory.path()), 8U);
		catalog.append_batch(1U, 0x44U, { root, child });
		EXPECT_EQ(catalog.record_count(), 2U);
		EXPECT_TRUE(catalog.contains(child.id));
		MultiwayPublicStateDescriptor root_candidate{};
		root_candidate.id = root.id;
		MultiwayPublicStateDescriptor child_candidate{};
		child_candidate.id = child.id;
		MultiwayPublicStateDescriptor new_candidate{};
		new_candidate.id.value = 3U;
		std::vector<std::uint8_t> new_flags;
		std::vector<MultiwayPackedPublicNode> existing_nodes;
		catalog.classify_batch({ root_candidate, child_candidate, new_candidate },
			new_flags, existing_nodes);
		EXPECT_EQ(new_flags, (std::vector<std::uint8_t>{ 0U, 0U, 1U }));
		std::vector<std::uint8_t> bounded_flags;
		catalog.classify_batch({ root_candidate, child_candidate, new_candidate }, bounded_flags);
		EXPECT_EQ(bounded_flags, new_flags);
		EXPECT_EQ(existing_nodes[0].id.value, root.id.value);
		EXPECT_EQ(existing_nodes[1].id.value, child.id.value);
		EXPECT_EQ(existing_nodes[2].id.value, 0U);
		catalog.append_discovered_batch(2U, 0x46U,
			{ root_candidate, child_candidate, new_candidate }, new_flags);
		EXPECT_EQ(catalog.record_count(), 3U);
		EXPECT_TRUE(catalog.contains(new_candidate.id));
		MultiwayPublicStateDescriptor missing_candidate{};
		missing_candidate.id.value = 4U;
		bool stale_classification_rejected = false;
		try
		{
			catalog.append_discovered_batch(3U, 0x47U, { missing_candidate }, { 0U });
		}
		catch (const std::invalid_argument&)
		{
			stale_classification_rejected = true;
		}
		EXPECT_TRUE(stale_classification_rejected);
		bool unsorted_rejected = false;
		try
		{
			catalog.classify_batch({ child_candidate, root_candidate }, new_flags, existing_nodes);
		}
		catch (const std::invalid_argument&)
		{
			unsorted_rejected = true;
		}
		EXPECT_TRUE(unsorted_rejected);
		std::vector<MultiwayPackedPublicNode> chain;
		EXPECT_TRUE(catalog.reconstruct_chain(child.id, chain));
		EXPECT_EQ(chain.size(), 2U);
		EXPECT_EQ(chain[0].id.value, root.id.value);
		EXPECT_EQ(chain[1].id.value, child.id.value);
		std::size_t empty_menu_count = 99U;
		EXPECT_TRUE(catalog.lookup_menu(child.id, nullptr, 0U, empty_menu_count));
		EXPECT_EQ(empty_menu_count, 0U);
		bool duplicate_rejected = false;
		try
		{
			catalog.append_batch(3U, 0x45U, { child });
		}
		catch (const std::invalid_argument&)
		{
			duplicate_rejected = true;
		}
		EXPECT_TRUE(duplicate_rejected);
	}
	MultiwayExternalPublicCatalog reopened(config(directory.path()), 8U);
	MultiwayPackedPublicNode found{};
	EXPECT_TRUE(reopened.lookup(child.id, found));
	EXPECT_EQ(found.parent_id.value, root.id.value);
}

TEST_CASE(multiway_external_public_catalog_persists_action_menus)
{
	using namespace texas::solver::multiway;
	TestDirectory directory;
	MultiwayPackedPublicNode root{};
	root.id.value = 11U;
	root.legal_menu_handle = 4U;
	MultiwayPublicStateDescriptor descriptor{};
	descriptor.id = root.id;
	descriptor.legal_actions = {
		{ texas::games::multiway::MultiwayAction::Check, 0U, 100, 0xabcU },
		{ texas::games::multiway::MultiwayAction::Bet, 1U, 300, 0xabcU }
	};
	{
		MultiwayExternalPublicCatalog catalog(config(directory.path()), 8U);
		catalog.append_discovered_batch(1U, 0x55U, { descriptor }, { 1U });
		MultiwayPackedPublicNode packed{};
		EXPECT_TRUE(catalog.lookup(root.id, packed));
		EXPECT_EQ(packed.legal_menu_handle, 0U);
		std::array<MultiwayActionDescriptor, MULTIWAY_MAX_ABSTRACTED_ACTIONS> actions{};
		std::size_t action_count = 0U;
		EXPECT_TRUE(catalog.lookup_menu(root.id, actions.data(), actions.size(), action_count));
		EXPECT_EQ(action_count, descriptor.legal_actions.size());
		for (std::size_t index = 0U; index < action_count; ++index)
			EXPECT_EQ(actions[index], descriptor.legal_actions[index]);
	}
	MultiwayExternalPublicCatalog reopened(config(directory.path()), 8U);
	std::array<MultiwayActionDescriptor, MULTIWAY_MAX_ABSTRACTED_ACTIONS> actions{};
	std::size_t action_count = 0U;
	EXPECT_TRUE(reopened.lookup_menu(root.id, actions.data(), actions.size(), action_count));
	EXPECT_EQ(action_count, 2U);
	EXPECT_EQ(actions[0].action_menu_id, 0xabcU);
	MultiwayPublicStateDescriptor reconstructed{};
	EXPECT_TRUE(reopened.reconstruct(root.id, reconstructed));
	EXPECT_EQ(reconstructed.id.value, descriptor.id.value);
	EXPECT_EQ(reconstructed.legal_actions.size(), descriptor.legal_actions.size());
	for (std::size_t index = 0U; index < descriptor.legal_actions.size(); ++index)
		EXPECT_EQ(reconstructed.legal_actions[index], descriptor.legal_actions[index]);
	EXPECT_EQ(external_public_catalog_record_size(), external_public_node_record_size() + 156U);
}

TEST_CASE(multiway_external_row_index_preserves_full_ids_and_deterministic_page_ranges)
{
	using namespace texas::solver::multiway;
	TestDirectory directory;
	std::uint64_t next_page_id = 1U;
	{
		MultiwayExternalRowIndex rows(config(directory.path()), 16U);
		rows.append_batch(1U, 0x77U, {
										 { { { 10U }, 0 }, 17U, 3U },
										 { { { 10U }, 1 }, 17U, 2U },
										 { { { 20U }, 2 }, 16U, 1U },
									 },
			next_page_id);
		EXPECT_EQ(next_page_id, 6U);
		MultiwayExternalRowMetadata metadata{};
		EXPECT_TRUE(rows.lookup({ 10U }, 1, metadata));
		EXPECT_EQ(metadata.bucket_count, 17U);
		EXPECT_EQ(metadata.action_count, 2U);
		EXPECT_EQ(metadata.first_page_id, 3U);
		EXPECT_EQ(metadata.page_count, 2U);
		MultiwayExternalRowPage page{};
		EXPECT_TRUE(rows.locate_bucket({ 10U }, 1, 16U, page));
		EXPECT_EQ(page.page_id, 4U);
		EXPECT_EQ(page.page_index, 1U);
		EXPECT_EQ(page.bucket_count, 1U);
		EXPECT_EQ(page.action_count, 2U);
		EXPECT_TRUE(!rows.locate_bucket({ 10U }, 1, 17U, page));
		EXPECT_TRUE(!rows.lookup({ 10U }, 5, metadata));
		std::uint64_t unchanged_page_id = 6U;
		bool invalid_rejected = false;
		try
		{
			rows.append_batch(2U, 0x78U, {
											 { { { 30U }, 0 }, 1U, 1U },
											 { { { 30U }, 6 }, 1U, 1U },
										 },
				unchanged_page_id);
		}
		catch (const std::invalid_argument&)
		{
			invalid_rejected = true;
		}
		EXPECT_TRUE(invalid_rejected);
		EXPECT_EQ(unchanged_page_id, 6U);
		rows.append_batch(2U, 0x79U, {
										 { { { 30U }, 0 }, 1U, 1U },
										 { { { 25U }, 1 }, 1U, 1U },
									 },
			next_page_id);
		EXPECT_EQ(next_page_id, 8U);
	}
	MultiwayExternalRowIndex reopened(config(directory.path()), 16U);
	EXPECT_EQ(reopened.next_page_id(), 8U);
	MultiwayExternalRowMetadata metadata{};
	EXPECT_TRUE(reopened.lookup({ 20U }, 2, metadata));
	EXPECT_EQ(metadata.first_page_id, 5U);
	EXPECT_EQ(metadata.page_count, 1U);
	EXPECT_TRUE(reopened.lookup({ 25U }, 1, metadata));
	EXPECT_EQ(metadata.first_page_id, 6U);
	EXPECT_TRUE(reopened.lookup({ 30U }, 0, metadata));
	EXPECT_EQ(metadata.first_page_id, 7U);
	OrderedRowCapture ordered_rows;
	EXPECT_TRUE(reopened.visit_ordered_rows(capture_ordered_row, &ordered_rows));
	EXPECT_EQ(ordered_rows.state_ids.size(), 5U);
	EXPECT_EQ(ordered_rows.state_ids[0], 10U);
	EXPECT_EQ(ordered_rows.seats[0], 0);
	EXPECT_EQ(ordered_rows.state_ids[1], 10U);
	EXPECT_EQ(ordered_rows.seats[1], 1);
	EXPECT_EQ(ordered_rows.state_ids[2], 20U);
	EXPECT_EQ(ordered_rows.seats[2], 2);
	EXPECT_EQ(ordered_rows.state_ids[3], 25U);
	EXPECT_EQ(ordered_rows.state_ids[4], 30U);
	EXPECT_EQ(ordered_rows.rows[1].bucket_count, 17U);
}

TEST_CASE(multiway_external_compact_pages_commit_reopen_and_rollback)
{
	using namespace texas::solver::multiway;
	TestDirectory directory;
	auto page_config = config(directory.path());
	page_config.cache_bytes = 10000U;
	page_config.maximum_wal_bytes = 65536U;
	page_config.segment_target_bytes = 65536U;
	{
		MultiwayExternalCompactPages pages(page_config, 16U, page_config.cache_bytes);
		EXPECT_EQ(pages.page_capacity(), 1U);
		EXPECT_TRUE(pages.memory_bytes() <= page_config.cache_bytes);
		pages.begin_batch(1U, 0xabcdefU);
		auto view = pages.pin(1U, { 42U }, 3, 0U, 16U, 4U);
		EXPECT_EQ(view.regrets[2U * view.action_stride + 7U], 0);
		EXPECT_EQ(view.strategy_masses[1U * view.action_stride + 5U], 0U);
		pages.mark_dirty(1U);
		view.regrets[2U * view.action_stride + 7U] = -12345;
		view.strategy_masses[1U * view.action_stride + 5U] = 0xfedcba9876543210ULL;
		pages.unpin(1U);
		pages.commit_batch();
	}
	{
		MultiwayExternalCompactPages pages(page_config, 16U, page_config.cache_bytes);
		auto view = pages.pin(1U, { 42U }, 3, 0U, 16U, 4U);
		EXPECT_EQ(view.regrets[2U * view.action_stride + 7U], -12345);
		EXPECT_EQ(view.strategy_masses[1U * view.action_stride + 5U], 0xfedcba9876543210ULL);
		pages.unpin(1U);
		pages.begin_batch(2U, 0x123456U);
		view = pages.pin(1U, { 42U }, 3, 0U, 16U, 4U);
		pages.mark_dirty(1U);
		view.regrets[2U * view.action_stride + 7U] = 999;
		pages.unpin(1U);
		pages.abort_batch();
		view = pages.pin(1U, { 42U }, 3, 0U, 16U, 4U);
		EXPECT_EQ(view.regrets[2U * view.action_stride + 7U], -12345);
		pages.unpin(1U);
		const auto stats = pages.stats();
		EXPECT_EQ(stats.page_cache_misses, 1U);
		EXPECT_EQ(stats.page_cache_hits, 2U);
	}
}

TEST_CASE(multiway_external_sparse_page_lookup_fixture)
{
	using namespace texas::solver::multiway;
	TestDirectory directory;
	auto page_config = config(directory.path());
	page_config.maximum_wal_bytes = 65536U;
	page_config.segment_target_bytes = 65536U;
	{
		MultiwayExternalCompactPages pages(page_config, 16U, 32000U);
		for (std::uint64_t generation = 1U; generation <= 24U; ++generation)
		{
			pages.begin_batch(generation, generation);
			for (const auto id : std::array<std::uint64_t, 3U>{ generation * 100U,
					 generation * 100U + 3000U, 6000U })
			{
				auto page = pages.pin(id, { id }, 0, 0U, 1U, 2U);
				pages.mark_dirty(id);
				page.strategy_masses[0] = generation;
				pages.unpin(id);
			}
			pages.commit_batch();
		}
	}
	MultiwayExternalCompactPages pages(page_config, 16U, 10000U);
	const bool filter_enabled = std::getenv("TEXASSOLVER_PB12_FILTER") != nullptr;
	const auto fixture_started = std::chrono::steady_clock::now();
	if (filter_enabled)
		pages.begin_export_filter();
	const auto filter_finished = std::chrono::steady_clock::now();
	MultiwayExternalPageProfile profile;
	for (const auto request : { std::pair<std::uint64_t, std::uint64_t>{ 100U, 1U },
			 std::pair<std::uint64_t, std::uint64_t>{ 6000U, 24U } })
	{
		auto page = pages.pin_profiled(request.first, { request.first }, 0, 0U, 1U, 2U, &profile);
		EXPECT_EQ(page.strategy_masses[0], request.second);
		pages.unpin(request.first);
	}
	std::uint64_t hits = 0U;
	const auto* extended = std::getenv("TEXASSOLVER_PB12_EXTENDED");
	const std::uint64_t request_count = extended != nullptr ? 50000U : 1000U;
	for (std::uint64_t i = 0U; i < request_count; ++i)
	{
		const auto id = 1U + ((i * 7919U) % 5500U);
		auto page = pages.pin_profiled(id, { id }, 0, 0U, 1U, 2U, &profile);
		const auto expected = id % 100U == 0U && id >= 100U && id <= 2400U
			? id / 100U
			: (id % 100U == 0U && id >= 3100U && id <= 5400U
					  ? (id - 3000U) / 100U
					  : 0U);
		EXPECT_EQ(page.strategy_masses[0], expected);
		hits += expected != 0U;
		pages.unpin(id);
	}
	EXPECT_TRUE(profile.record_lookup.index_opens > 0U);
	EXPECT_TRUE(profile.zero_pages > profile.decoded_pages);
	if (filter_enabled)
		EXPECT_TRUE(profile.record_lookup.membership_misses > 0U);
	std::cout << "PB12_SPARSE_PAGE_PROFILE index_opens=" << profile.record_lookup.index_opens
			  << " index_reads=" << profile.record_lookup.index_reads
			  << " range_skips=" << profile.record_lookup.run_range_skips
			  << " membership_misses=" << profile.record_lookup.membership_misses
			  << " filter_build_ns=" << std::chrono::duration_cast<std::chrono::nanoseconds>(filter_finished - fixture_started).count()
			  << " data_reads=" << profile.record_lookup.data_reads
			  << " zero_pages=" << profile.zero_pages << " hits=" << hits << '\n';
	if (filter_enabled)
		pages.end_export_filter();
}

TEST_CASE(multiway_external_export_filter_rejects_corruption_and_allows_retry)
{
	using namespace texas::solver::multiway;
	TestDirectory directory;
	MultiwayExternalRecordStore store(config(directory.path()), 2U);
	store.append_batch(1U, 1U, { { 4U, { 4U, 10U } }, { 8U, { 8U, 20U } } });
	std::filesystem::resize_file(directory.path() / "run_1.idx", 0U);
	EXPECT_THROW(store.begin_export_filter(), std::invalid_argument);
	EXPECT_THROW(store.begin_export_filter(), std::invalid_argument);
}

TEST_CASE(multiway_external_export_filter_preserves_lookup_and_blocks_mutation)
{
	using namespace texas::solver::multiway;
	TestDirectory directory;
	MultiwayExternalRecordStore store(config(directory.path()), 2U);
	store.append_batch(1U, 1U, { { 4U, { 4U, 10U } }, { 8U, { 8U, 10U } } });
	store.append_batch(2U, 2U, { { 4U, { 4U, 20U } }, { 9U, { 9U, 20U } } });
	store.begin_export_filter();
	MultiwayExternalRecordLookupProfile profile;
	std::vector<std::uint8_t> payload;
	EXPECT_TRUE(store.lookup_profiled(4U, payload, profile));
	EXPECT_EQ(payload[1], 20U);
	EXPECT_TRUE(store.lookup_profiled(8U, payload, profile));
	EXPECT_EQ(payload[1], 10U);
	EXPECT_TRUE(!store.lookup_profiled(7U, payload, profile));
	EXPECT_EQ(profile.membership_misses, 1U);
	EXPECT_THROW(store.append_batch(3U, 3U, { { 7U, { 7U, 30U } } }), std::logic_error);
	EXPECT_THROW(store.compact(), std::logic_error);
	store.end_export_filter();
	store.append_batch(3U, 3U, { { 7U, { 7U, 30U } } });
	EXPECT_TRUE(store.lookup(7U, payload));
	EXPECT_EQ(payload[1], 30U);
}

TEST_CASE(multiway_external_compact_pages_match_reference_cell_encoding)
{
	using namespace texas::solver::multiway;
	TestDirectory directory;
	auto page_config = config(directory.path());
	page_config.cache_bytes = 10000U;
	page_config.maximum_wal_bytes = 65536U;
	page_config.segment_target_bytes = 65536U;
	const MultiwaySparseRowShape shape{ { { 42U }, 3 }, 16U, 4U };
	MultiwayCompactStorage reference(1U, 64U, 16U);
	reference.admit_row(shape);
	MultiwayExternalCompactPages external(page_config, 16U, page_config.cache_bytes);
	external.begin_batch(1U, 0xabcU);
	auto page = external.pin(1U, { 42U }, 3, 0U, 16U, 4U);
	const auto* row = reference.metadata(shape.infoset);
	const std::array<std::array<double, 2U>, 4U> deltas = { { { { -1.25, 0.5 } }, { { 2.0, 3.25 } }, { { -0.75, 1.0 } }, { { 4.5, 2.0 } } } };
	for (std::uint8_t action = 0U; action < shape.action_count; ++action)
	{
		reference.apply_delta(*row, 7U, action, deltas[action][0], deltas[action][1]);
		const auto cell = static_cast<std::size_t>(action) * page.action_stride + 7U;
		page.regrets[cell] = static_cast<std::int32_t>(page.regrets[cell] + deltas[action][0] * 1024.0);
		page.strategy_masses[cell] += static_cast<std::uint64_t>(deltas[action][1] * MultiwayCompactStorage::kMassScale);
	}
	external.mark_dirty(1U);
	external.unpin(1U);
	external.commit_batch();
	const auto reference_page = reference.checkpoint_page(0U);
	auto persisted = external.pin(1U, { 42U }, 3, 0U, 16U, 4U);
	for (std::uint8_t action = 0U; action < shape.action_count; ++action)
	{
		const auto cell = static_cast<std::size_t>(action) * persisted.action_stride + 7U;
		EXPECT_EQ(persisted.regrets[cell], reference_page.regrets[cell]);
		EXPECT_EQ(persisted.strategy_masses[cell], reference_page.strategy_masses[cell]);
	}
	external.unpin(1U);
}

TEST_CASE(multiway_external_compact_storage_matches_reference_batch_arithmetic)
{
	using namespace texas::solver::multiway;
	static_assert(!std::is_copy_constructible<MultiwayExternalCompactStorage::PinnedBucket>::value,
		"pinned external bucket handles must not duplicate cache pins");
	TestDirectory directory;
	auto page_config = config(directory.path() / "pages");
	page_config.maximum_wal_bytes = 65536U;
	page_config.segment_target_bytes = 65536U;
	const auto row_config = config(directory.path() / "rows");
	const MultiwaySparseRowShape shape{ { { 42U }, 3 }, 17U, 3U };
	MultiwayCompactStorage reference(1U, 51U, 16U);
	reference.admit_row(shape);
	MultiwayExternalCompactStorage external(row_config, page_config, 16U, 16000U);
	external.admit_rows(1U, 0xabcU, { shape });
	MultiwayExternalRowBinding binding{};
	EXPECT_TRUE(external.lookup_row(shape.infoset, binding));
	external.begin_batch(1U, 0xabcU);
	const std::array<MultiwayWorkerDelta, 6U> deltas = { {
		{ shape.infoset, 0U, 0U, -2.5, 1.25, 0U },
		{ shape.infoset, 0U, 1U, 3.0, 4.5, 1U },
		{ shape.infoset, 15U, 1U, -0.75, 2.0, 2U },
		{ shape.infoset, 16U, 2U, 1'900'000.0, 1'099'511'627'776.0, 3U },
		{ shape.infoset, 16U, 2U, 200'000.0, 1'099'511'627'776.0, 4U },
		{ shape.infoset, 16U, 0U, -2'500'000.0, 0.0, 5U },
	} };
	std::size_t delta_index = 0U;
	while (delta_index < deltas.size())
	{
		auto pinned = external.pin_bucket(binding, deltas[delta_index].bucket, true);
		const auto bucket = deltas[delta_index].bucket;
		while (delta_index < deltas.size() && deltas[delta_index].bucket == bucket)
		{
			const auto& delta = deltas[delta_index++];
			reference.apply_delta(delta.infoset, delta.bucket, delta.action, delta.regret, delta.strategy_sum);
			external.apply_delta(pinned, delta.action, delta.regret, delta.strategy_sum);
		}
		external.unpin_bucket(pinned);
	}
	external.commit_batch();
	for (const auto bucket : { 0U, 15U, 16U })
	{
		std::array<double, 3U> expected_regret{};
		std::array<double, 3U> actual_regret{};
		std::array<double, 3U> expected_average{};
		std::array<double, 3U> actual_average{};
		reference.regret_matched_strategy_into(shape.infoset, bucket,
			expected_regret.data(), expected_regret.size());
		reference.average_strategy_into(shape.infoset, bucket,
			expected_average.data(), expected_average.size());
		external.regret_matched_strategy_into(binding, bucket,
			actual_regret.data(), actual_regret.size());
		external.average_strategy_into(binding, bucket,
			actual_average.data(), actual_average.size());
		for (std::size_t action = 0U; action < expected_regret.size(); ++action)
		{
			EXPECT_EQ(actual_regret[action], expected_regret[action]);
			EXPECT_EQ(actual_average[action], expected_average[action]);
		}
	}
	EXPECT_EQ(external.action_below_regret(binding, 16U, 2U, 0.0),
		reference.action_below_regret(shape.infoset, 16U, 2U, 0.0));
	external.compact();
	MultiwayExternalCompactStorage reopened(row_config, page_config, 16U, 16000U);
	MultiwayExternalRowBinding reopened_binding{};
	EXPECT_TRUE(reopened.lookup_row(shape.infoset, reopened_binding));
	std::array<double, 3U> reopened_policy{};
	reopened.regret_matched_strategy_into(reopened_binding, 16U,
		reopened_policy.data(), reopened_policy.size());
	std::array<double, 3U> expected_policy{};
	reference.regret_matched_strategy_into(shape.infoset, 16U,
		expected_policy.data(), expected_policy.size());
	for (std::size_t action = 0U; action < expected_policy.size(); ++action)
		EXPECT_EQ(reopened_policy[action], expected_policy[action]);
	auto reopened_pinned = reopened.pin_bucket(reopened_binding, 16U, false);
	std::array<double, 3U> pinned_policy{};
	reopened.regret_matched_strategy_into(reopened_pinned,
		pinned_policy.data(), pinned_policy.size());
	for (std::size_t action = 0U; action < expected_policy.size(); ++action)
		EXPECT_EQ(pinned_policy[action], expected_policy[action]);
	EXPECT_EQ(reopened.action_below_regret(reopened_pinned, 2U, 0.0),
		reference.action_below_regret(shape.infoset, 16U, 2U, 0.0));
	std::array<bool, 3U> pinned_thresholds{};
	for (std::uint8_t action = 0U; action < pinned_thresholds.size(); ++action)
		pinned_thresholds[action] = reopened.action_below_regret(reopened_pinned, action, 0.0);
	for (std::uint8_t action = 0U; action < pinned_thresholds.size(); ++action)
		EXPECT_EQ(pinned_thresholds[action], reference.action_below_regret(shape.infoset, 16U, action, 0.0));
	bool read_only_update_rejected = false;
	try
	{
		reopened.apply_delta(reopened_pinned, 2U, 1.0, 0.0);
	}
	catch (const std::invalid_argument&)
	{
		read_only_update_rejected = true;
	}
	EXPECT_TRUE(read_only_update_rejected);
	reopened.unpin_bucket(reopened_pinned);
	bool foreign_binding_rejected = false;
	try
	{
		external.regret_matched_strategy_into(reopened_binding, 16U,
			reopened_policy.data(), reopened_policy.size());
	}
	catch (const std::invalid_argument&)
	{
		foreign_binding_rejected = true;
	}
	EXPECT_TRUE(foreign_binding_rejected);
}

TEST_CASE(multiway_external_pinned_page_average_matches_bucket_order)
{
	using namespace texas::solver::multiway;
	TestDirectory directory;
	auto page_config = config(directory.path() / "pages");
	page_config.maximum_wal_bytes = 65536U;
	page_config.segment_target_bytes = 65536U;
	const auto row_config = config(directory.path() / "rows");
	constexpr std::uint32_t bucket_count = 18U;
	constexpr std::uint32_t page_width = 16U;
	constexpr std::uint8_t action_count = 3U;
	const MultiwaySparseRowShape shape{ { { 42U }, 3 }, bucket_count, action_count };
	MultiwayCompactStorage reference(1U, 54U, page_width);
	reference.admit_row(shape);
	MultiwayExternalCompactStorage external(row_config, page_config, page_width, 16000U);
	external.admit_rows(1U, 0xdefU, { shape });
	MultiwayExternalRowBinding binding{};
	EXPECT_TRUE(external.lookup_row(shape.infoset, binding));
	external.begin_batch(1U, 0xdefU);
	for (std::uint32_t bucket = 0U; bucket < bucket_count; ++bucket)
	{
		// Leave one bucket in each page at zero mass to cover uniform fallback.
		if (bucket == 0U || bucket == page_width)
			continue;
		for (std::uint8_t action = 0U; action < action_count; ++action)
		{
			const auto strategy_sum = static_cast<double>((bucket + 1U) * (action + 1U));
			reference.apply_delta(shape.infoset, bucket, action, 0.0, strategy_sum);
			external.apply_delta(binding, bucket, action, 0.0, strategy_sum);
		}
	}
	external.commit_batch();

	std::array<double, action_count> expected{};
	std::array<double, action_count> direct{};
	std::array<double, action_count> reused_page{};
	std::uint32_t next_bucket = 0U;
	std::uint32_t page_count = 0U;
	for (std::uint32_t first_bucket = 0U; first_bucket < bucket_count;)
	{
		auto pinned = external.pin_bucket(binding, first_bucket, false);
		try
		{
			const auto buckets_in_page = pinned.page.bucket_count;
			EXPECT_EQ(pinned.local_bucket, 0U);
			if (buckets_in_page == 0U || buckets_in_page > bucket_count - first_bucket)
				throw std::runtime_error("pinned external page has an invalid bucket range");
			for (std::uint32_t local_bucket = 0U; local_bucket < buckets_in_page; ++local_bucket)
			{
				const auto bucket = first_bucket + local_bucket;
				EXPECT_EQ(bucket, next_bucket);
				pinned.local_bucket = local_bucket;
				reference.average_strategy_into(shape.infoset, bucket,
					expected.data(), expected.size());
				external.average_strategy_into(binding, bucket, direct.data(), direct.size());
				external.average_strategy_into(pinned, reused_page.data(), reused_page.size());
				for (std::size_t action = 0U; action < action_count; ++action)
				{
					EXPECT_EQ(direct[action], expected[action]);
					EXPECT_EQ(reused_page[action], direct[action]);
				}
				++next_bucket;
			}
			first_bucket += buckets_in_page;
			++page_count;
		}
		catch (...)
		{
			external.unpin_bucket(pinned);
			throw;
		}
		external.unpin_bucket(pinned);
	}
	EXPECT_EQ(next_bucket, bucket_count);
	EXPECT_EQ(page_count, 2U);
}

TEST_CASE(multiway_external_page_profile_distinguishes_absent_cached_and_reopened_pages)
{
	using namespace texas::solver::multiway;
	TestDirectory directory;
	auto page_config = config(directory.path() / "pages");
	page_config.maximum_wal_bytes = 65536U;
	page_config.segment_target_bytes = 65536U;
	const auto row_config = config(directory.path() / "rows");
	const MultiwaySparseRowShape shape{ { { 51U }, 0 }, 1U, 2U };
	{
		MultiwayExternalCompactStorage storage(row_config, page_config, 16U, 16000U);
		storage.admit_rows(1U, 0x123U, { shape });
		MultiwayExternalRowBinding binding;
		EXPECT_TRUE(storage.lookup_row(shape.infoset, binding));
		MultiwayExternalPageProfile profile;
		auto first = storage.pin_bucket_profiled(binding, 0U, false, &profile);
		std::array<double, 2U> average{};
		storage.average_strategy_into(first, average.data(), average.size());
		EXPECT_EQ(average[0], 0.5);
		EXPECT_EQ(average[1], 0.5);
		storage.unpin_bucket(first);
		EXPECT_EQ(profile.zero_pages, 1U);
		EXPECT_EQ(profile.decoded_pages, 0U);
		auto cached = storage.pin_bucket_profiled(binding, 0U, false, &profile);
		storage.unpin_bucket(cached);
		EXPECT_EQ(profile.zero_pages, 1U);
		storage.begin_batch(1U, 0x123U);
		storage.apply_delta(binding, 0U, 0U, 0.0, 1.0);
		storage.commit_batch();
	}
	{
		MultiwayExternalCompactStorage reopened(row_config, page_config, 16U, 16000U);
		MultiwayExternalRowBinding binding;
		EXPECT_TRUE(reopened.lookup_row(shape.infoset, binding));
		MultiwayExternalPageProfile profile;
		auto page = reopened.pin_bucket_profiled(binding, 0U, false, &profile);
		std::array<double, 2U> average{};
		reopened.average_strategy_into(page, average.data(), average.size());
		reopened.unpin_bucket(page);
		EXPECT_EQ(profile.decoded_pages, 1U);
		EXPECT_EQ(profile.zero_pages, 0U);
		EXPECT_EQ(profile.record_lookup.index_opens, 1U);
		EXPECT_EQ(profile.record_lookup.index_reads, 2U);
		EXPECT_EQ(profile.record_lookup.data_opens, 1U);
		EXPECT_EQ(profile.record_lookup.data_reads, 1U);
		EXPECT_TRUE(profile.record_lookup.requested_bytes > 16U);
		EXPECT_EQ(average[0], 1.0);
		EXPECT_EQ(average[1], 0.0);
		EXPECT_THROW(reopened.pin_bucket_profiled(binding, 1U, false, &profile), std::out_of_range);
		EXPECT_THROW(reopened.pin_bucket_profiled(binding, 0U, true, &profile), std::logic_error);
		// Compaction requires every page to be unpinned and clean.
		reopened.compact();
	}
}

TEST_CASE(multiway_external_profiled_page_rejects_wrong_cached_identity)
{
	using namespace texas::solver::multiway;
	TestDirectory directory;
	auto page_config = config(directory.path());
	page_config.maximum_wal_bytes = 65536U;
	page_config.segment_target_bytes = 65536U;
	MultiwayExternalCompactPages pages(page_config, 16U, 16000U);
	MultiwayExternalPageProfile profile;
	auto first = pages.pin_profiled(1U, { 51U }, 0, 0U, 1U, 2U, &profile);
	pages.unpin(first.page_id);
	EXPECT_THROW(pages.pin_profiled(1U, { 52U }, 0, 0U, 1U, 2U, &profile), std::invalid_argument);
	EXPECT_EQ(profile.zero_pages, 1U);
	EXPECT_EQ(profile.decoded_pages, 0U);
	auto again = pages.pin_profiled(1U, { 51U }, 0, 0U, 1U, 2U, &profile);
	pages.unpin(again.page_id);
	pages.compact();
}

TEST_CASE(multiway_external_profiled_pin_rejects_corrupt_page_without_leaking_pin)
{
	using namespace texas::solver::multiway;
	TestDirectory directory;
	auto page_config = config(directory.path() / "pages");
	page_config.maximum_wal_bytes = 65536U;
	page_config.segment_target_bytes = 65536U;
	const auto row_config = config(directory.path() / "rows");
	const MultiwaySparseRowShape shape{ { { 52U }, 0 }, 1U, 2U };
	{
		MultiwayExternalCompactStorage storage(row_config, page_config, 16U, 16000U);
		storage.admit_rows(1U, 0x124U, { shape });
		MultiwayExternalRowBinding binding;
		EXPECT_TRUE(storage.lookup_row(shape.infoset, binding));
		storage.begin_batch(1U, 0x124U);
		storage.apply_delta(binding, 0U, 0U, 0.0, 1.0);
		storage.commit_batch();
	}
	{
		MultiwayExternalCompactStorage reopened(row_config, page_config, 16U, 16000U);
		MultiwayExternalRowBinding binding;
		EXPECT_TRUE(reopened.lookup_row(shape.infoset, binding));
		std::fstream file(directory.path() / "pages" / "run_1.dat",
			std::ios::binary | std::ios::in | std::ios::out);
		EXPECT_TRUE(static_cast<bool>(file));
		file.seekg(16);
		char byte = 0;
		file.read(&byte, 1);
		EXPECT_TRUE(static_cast<bool>(file));
		byte ^= 1;
		file.seekp(16);
		file.write(&byte, 1);
		file.close();
		EXPECT_TRUE(static_cast<bool>(file));
		MultiwayExternalPageProfile profile;
		EXPECT_THROW(reopened.pin_bucket_profiled(binding, 0U, false, &profile), std::invalid_argument);
		EXPECT_THROW(reopened.pin_bucket_profiled(binding, 0U, false, &profile), std::invalid_argument);
		EXPECT_EQ(profile.zero_pages, 0U);
		EXPECT_EQ(profile.decoded_pages, 0U);
		EXPECT_EQ(profile.record_lookup.data_reads, 1U);
		reopened.compact();
	}
}

TEST_CASE(multiway_external_record_store_compacts_deterministically)
{
	using namespace texas::solver::multiway;
	TestDirectory left_directory;
	TestDirectory right_directory;
	for (const auto& path : { left_directory.path(), right_directory.path() })
	{
		MultiwayExternalRecordStore store(config(path), 4U);
		store.append_batch(1U, 1U, records(1U));
		store.append_batch(2U, 2U, records(8U));
		store.compact();
		EXPECT_EQ(store.record_count(), 4U);
		EXPECT_EQ(store.generation(), 3U);
	}
	const auto left = std::filesystem::file_size(left_directory.path() / "run_3.dat");
	const auto right = std::filesystem::file_size(right_directory.path() / "run_3.dat");
	EXPECT_EQ(left, right);
	std::ifstream left_data(left_directory.path() / "run_3.dat", std::ios::binary);
	std::ifstream right_data(right_directory.path() / "run_3.dat", std::ios::binary);
	std::array<char, 128U> left_bytes{};
	std::array<char, 128U> right_bytes{};
	left_data.read(left_bytes.data(), left_bytes.size());
	right_data.read(right_bytes.data(), right_bytes.size());
	EXPECT_EQ(left_data.gcount(), right_data.gcount());
	EXPECT_TRUE(std::equal(left_bytes.begin(), left_bytes.begin() + left_data.gcount(), right_bytes.begin()));
}

TEST_CASE(multiway_external_record_store_fails_closed_after_compaction_read_failure)
{
	using namespace texas::solver::multiway;
	TestDirectory directory;
	MultiwayExternalRecordStore store(config(directory.path()), 4U);
	store.append_batch(1U, 1U, records(1U));
	store.append_batch(2U, 2U, records(8U));
	std::filesystem::remove(directory.path() / "run_1.dat");
	bool compaction_failed = false;
	try
	{
		store.compact();
	}
	catch (const std::runtime_error&)
	{
		compaction_failed = true;
	}
	EXPECT_TRUE(compaction_failed);
	std::vector<std::uint8_t> payload;
	bool later_lookup_failed_closed = false;
	try
	{
		static_cast<void>(store.lookup(1U, payload));
	}
	catch (const std::runtime_error&)
	{
		later_lookup_failed_closed = true;
	}
	EXPECT_TRUE(later_lookup_failed_closed);
}

TEST_CASE(multiway_external_compaction_can_retain_previous_runs_until_commit)
{
	using namespace texas::solver::multiway;
	TestDirectory directory;
	{
		MultiwayExternalRecordStore store(config(directory.path()), 4U);
		store.append_batch(1U, 1U, records(1U));
		store.append_batch(2U, 2U, records(8U));
		store.compact(true);
		EXPECT_EQ(store.generation(), 3U);
		EXPECT_TRUE(std::filesystem::exists(directory.path() / "run_1.dat"));
		EXPECT_TRUE(std::filesystem::exists(directory.path() / "run_2.dat"));
		EXPECT_TRUE(std::filesystem::exists(directory.path() / "run_3.dat"));
	}
	{
		MultiwayExternalRecordStore reopened(config(directory.path()), 4U);
		EXPECT_EQ(reopened.record_count(), 4U);
		EXPECT_TRUE(!std::filesystem::exists(directory.path() / "run_1.dat"));
		EXPECT_TRUE(!std::filesystem::exists(directory.path() / "run_2.dat"));
		std::vector<std::uint8_t> payload;
		EXPECT_TRUE(reopened.lookup(10U, payload));
	}
}

TEST_CASE(multiway_external_record_store_compaction_keeps_newest_page_version)
{
	using namespace texas::solver::multiway;
	TestDirectory directory;
	MultiwayExternalRecordStore store(config(directory.path()), 4U);
	store.append_batch(1U, 1U, records(10U));
	store.append_batch(2U, 2U, { { 10U, { 0xeeU, 0U, 0U, 0U } }, { 30U, { 0x30U, 0U, 0U, 0U } } });
	EXPECT_EQ(store.record_count(), 4U);
	store.compact();
	EXPECT_EQ(store.record_count(), 3U);
	std::vector<std::uint8_t> payload;
	EXPECT_TRUE(store.lookup(10U, payload));
	EXPECT_EQ(payload[0], 0xeeU);
	MultiwayExternalRecordStore reopened(config(directory.path()), 4U);
	EXPECT_TRUE(reopened.lookup(10U, payload));
	EXPECT_EQ(payload[0], 0xeeU);
}

TEST_CASE(multiway_external_page_cache_pins_bounds_commits_and_rolls_back_pages)
{
	using namespace texas::solver::multiway;
	TestDirectory directory;
	MultiwayExternalRecordStore store(config(directory.path()), 16U);
	MultiwayExternalPageCache cache(store, 8192U);
	EXPECT_TRUE(cache.page_capacity() > 0U);
	cache.begin_batch(1U, 77U);
	auto page = cache.pin(101U);
	EXPECT_EQ(page.size, 16U);
	cache.mark_dirty(page.key);
	page.bytes[0] = 0x33U;
	page.bytes[15] = 0x44U;
	cache.unpin(page.key);
	EXPECT_EQ(cache.dirty_page_count(), 1U);
	cache.commit_batch();
	MultiwayExternalPageCache restored(store, 8192U);
	const auto restored_page = restored.pin(101U);
	EXPECT_EQ(restored_page.bytes[0], 0x33U);
	EXPECT_EQ(restored_page.bytes[15], 0x44U);
	restored.unpin(restored_page.key);

	cache.begin_batch(2U, 88U);
	page = cache.pin(101U);
	cache.mark_dirty(page.key);
	page.bytes[0] = 0x99U;
	cache.unpin(page.key);
	cache.abort_batch();
	page = cache.pin(101U);
	EXPECT_EQ(page.bytes[0], 0x33U);
	cache.unpin(page.key);
}

TEST_CASE(multiway_external_page_cache_does_not_evict_pinned_or_dirty_pages)
{
	using namespace texas::solver::multiway;
	TestDirectory directory;
	MultiwayExternalRecordStore store(config(directory.path()), 16U);
	MultiwayExternalPageCache cache(store, 4096U);
	std::vector<MultiwayExternalPageCache::PageView> pinned;
	pinned.reserve(cache.page_capacity());
	for (std::size_t index = 0U; index < cache.page_capacity(); ++index)
		pinned.push_back(cache.pin(index + 1U));
	EXPECT_THROW(cache.pin(cache.page_capacity() + 1U), std::length_error);
	for (const auto page : pinned)
		cache.unpin(page.key);
	cache.begin_batch(1U, 1U);
	for (const auto page : pinned)
	{
		const auto dirty = cache.pin(page.key);
		cache.mark_dirty(dirty.key);
		cache.unpin(dirty.key);
	}
	EXPECT_THROW(cache.pin(cache.page_capacity() + 1U), std::length_error);
	cache.abort_batch();
	EXPECT_EQ(cache.pin(cache.page_capacity() + 1U).key, cache.page_capacity() + 1U);
}

TEST_CASE(multiway_external_record_store_rejects_invalid_batches_without_publication)
{
	using namespace texas::solver::multiway;
	TestDirectory directory;
	MultiwayExternalRecordStore store(config(directory.path()), 4U);
	auto unsorted = records(4U);
	std::swap(unsorted[0], unsorted[1]);
	EXPECT_THROW(store.append_batch(1U, 0U, unsorted), std::invalid_argument);
	EXPECT_EQ(store.record_count(), 0U);
	EXPECT_TRUE(!std::filesystem::exists(directory.path() / "CURRENT"));
	const std::vector<MultiwayExternalRecord> wrong_width = { { 7U, { 1U, 2U } } };
	EXPECT_THROW(store.append_batch(1U, 0U, wrong_width), std::invalid_argument);
	EXPECT_EQ(store.record_count(), 0U);
}

TEST_CASE(multiway_external_record_store_discards_a_partial_uncommitted_wal)
{
	using namespace texas::solver::multiway;
	TestDirectory directory;
	{
		std::ofstream wal(directory.path() / "BATCH.wal", std::ios::binary);
		wal << "partial";
	}
	MultiwayExternalRecordStore recovered(config(directory.path()), 4U);
	EXPECT_EQ(recovered.record_count(), 0U);
	EXPECT_TRUE(!std::filesystem::exists(directory.path() / "BATCH.wal"));
}

TEST_CASE(multiway_external_record_store_replays_a_committed_wal_after_segment_failure)
{
	using namespace texas::solver::multiway;
	TestDirectory directory;
	{
		MultiwayExternalRecordStore store(config(directory.path()), 4U);
		std::ofstream collision(directory.path() / "run_1.dat", std::ios::binary);
		collision << "reserved test collision";
		collision.close();
		EXPECT_THROW(store.append_batch(1U, 99U, records(30U)), std::runtime_error);
		EXPECT_EQ(store.record_count(), 0U);
	}
	{
		MultiwayExternalRecordStore recovered(config(directory.path()), 4U);
		EXPECT_EQ(recovered.record_count(), 2U);
		std::vector<std::uint8_t> payload;
		EXPECT_TRUE(recovered.lookup(32U, payload));
		EXPECT_EQ(payload[0], 32U);
	}
}
