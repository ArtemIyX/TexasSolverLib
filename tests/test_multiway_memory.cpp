#include "solver/multiway/engine/multiway_memory.hpp"
#include "test_harness.hpp"

#include <stdexcept>
#include <limits>
#include <string>

namespace
{

	texas::MultiwaySolverLimits limits()
	{
		texas::MultiwaySolverLimits result;
		result.worker_count = 2;
		result.trajectories_per_batch = 32;
		result.max_public_states = 64;
		result.max_sparse_rows = 128;
		result.max_sparse_values = 1024;
		result.max_worker_delta_entries = 256;
		result.max_discovery_entries = 64;
		return result;
	}

} // namespace

TEST_CASE(multiway_process_memory_phase_names_round_trip_in_order)
{
	using namespace texas::solver::multiway;
	static_assert(noexcept(capture_multiway_process_memory_snapshot()));
	const char* expected_names[MULTIWAY_MEMORY_PHASE_COUNT] = {
		"process_start",
		"before_bucket_map",
		"after_bucket_map_metadata",
		"after_bucket_assignment_validation",
		"before_memory_preflight",
		"after_coordinator_construction",
		"after_worker_pool_construction",
		"after_cold_batch",
		"after_steady_batch",
		"before_checkpoint",
		"after_checkpoint",
		"before_export",
		"after_export",
	};
	MultiwayProcessMemorySnapshotCollection snapshots;
	for (std::size_t index = 0U; index < MULTIWAY_MEMORY_PHASE_COUNT; ++index)
	{
		const auto phase = static_cast<MultiwayMemoryPhase>(index);
		EXPECT_TRUE(snapshots.record(phase, {}));
		EXPECT_EQ(std::string(multiway_memory_phase_name(phase)), std::string(expected_names[index]));
	}
	EXPECT_TRUE(snapshots.enabled);
	EXPECT_TRUE(snapshots.completed);
	EXPECT_TRUE(snapshots.valid());
}

TEST_CASE(multiway_process_memory_snapshots_reject_duplicate_and_out_of_order_phases)
{
	using namespace texas::solver::multiway;
	MultiwayProcessMemorySnapshotCollection snapshots;
	EXPECT_TRUE(!snapshots.record(MultiwayMemoryPhase::AfterBucketMapMetadata, {}));
	EXPECT_TRUE(snapshots.record(MultiwayMemoryPhase::ProcessStart, {}));
	EXPECT_TRUE(!snapshots.record(MultiwayMemoryPhase::ProcessStart, {}));
	EXPECT_TRUE(!snapshots.record(MultiwayMemoryPhase::AfterBucketAssignmentValidation, {}));
	EXPECT_TRUE(snapshots.valid());
}

TEST_CASE(multiway_process_memory_snapshot_reports_supported_fields)
{
	using namespace texas::solver::multiway;
	const auto snapshot = capture_multiway_process_memory_snapshot();
#if defined(_WIN32) || defined(__linux__)
	EXPECT_TRUE(snapshot.current_working_set_available);
	EXPECT_TRUE(snapshot.peak_working_set_available);
	EXPECT_TRUE(snapshot.current_working_set_bytes > 0U);
	EXPECT_TRUE(snapshot.peak_working_set_bytes >= snapshot.current_working_set_bytes);
#else
	EXPECT_TRUE(!snapshot.current_working_set_available);
	EXPECT_TRUE(!snapshot.peak_working_set_available);
#endif
	if (snapshot.private_committed_available)
		EXPECT_TRUE(snapshot.private_committed_bytes > 0U);
	if (snapshot.virtual_address_available)
		EXPECT_TRUE(snapshot.virtual_address_bytes > 0U);
	if (snapshot.anonymous_resident_available)
		EXPECT_TRUE(snapshot.anonymous_resident_bytes > 0U);
	if (snapshot.file_resident_available)
		EXPECT_TRUE(snapshot.file_resident_bytes > 0U);
}

TEST_CASE(multiway_process_memory_snapshot_keeps_unavailable_fields_explicit)
{
	using namespace texas::solver::multiway;
	MultiwayProcessMemorySnapshot snapshot;
	EXPECT_TRUE(snapshot.valid());
	EXPECT_TRUE(!snapshot.private_committed_available);
	EXPECT_TRUE(!snapshot.virtual_address_available);
	snapshot.private_committed_bytes = 1U;
	EXPECT_TRUE(!snapshot.valid());
}

TEST_CASE(multiway_memory_preflight_reports_warning_and_rejection)
{
	const auto input = limits();
	const auto ok = texas::preflight_multiway_memory(input, { 1'000'000, 2'000'000 });
	EXPECT_EQ(ok.status, texas::MultiwayMemoryStatus::Ok);
	EXPECT_TRUE(ok.estimate.total_bytes > 0U);

	const auto warning = texas::preflight_multiway_memory(input, { 1, 2'000'000 });
	EXPECT_EQ(warning.status, texas::MultiwayMemoryStatus::Warning);

	const auto rejected = texas::preflight_multiway_memory(input, { 1, 2 });
	EXPECT_EQ(rejected.status, texas::MultiwayMemoryStatus::Rejected);
}

TEST_CASE(multiway_memory_budget_rejects_invalid_limits)
{
	const texas::MultiwayMemoryBudget invalid{ 2, 1 };
	EXPECT_THROW(invalid.validate(), std::invalid_argument);
}

TEST_CASE(multiway_memory_preflight_accounts_for_all_runtime_search_components)
{
	texas::MultiwayMemoryInputs inputs;
	inputs.blueprint_index_bytes = 11U;
	inputs.range_row_count = 6U;
	inputs.range_entry_count = 42U;
	inputs.future_bucket_cache_bytes = 13U;
	inputs.off_tree_menu_entries = 17U;
	inputs.continuation_scratch_bytes_per_worker = 19U;
	inputs.continuation_cache_bytes = 23U;
	inputs.export_action_capacity = 8U;

	const auto result = texas::preflight_multiway_memory(
		limits(), texas::MultiwayMemoryBudget{ 1U, 10'000'000U, 9'000'000U }, inputs);
	EXPECT_TRUE(result.estimate.blueprint_index_bytes >= 11U);
	EXPECT_TRUE(result.estimate.range_row_bytes > 0U);
	EXPECT_EQ(result.estimate.future_bucket_cache_bytes, 13U);
	EXPECT_TRUE(result.estimate.off_tree_menu_bytes > 0U);
	EXPECT_EQ(result.estimate.continuation_scratch_bytes, 38U);
	EXPECT_TRUE(result.estimate.merge_scratch_bytes > 0U);
	EXPECT_TRUE(result.estimate.discovery_stream_bytes > 0U);
	EXPECT_EQ(result.estimate.ledger.discovery_stream_bytes, result.estimate.discovery_stream_bytes);
	EXPECT_TRUE(result.estimate.export_bytes > 0U);
	EXPECT_EQ(result.estimate.continuation_cache_bytes, 23U);
	EXPECT_EQ(result.estimate.admitted_bytes, result.estimate.total_bytes);
	EXPECT_EQ(
		result.highest_admitted_stage,
		texas::MultiwayMemoryAdmissionStage::OptionalContinuationCache);
}

TEST_CASE(multiway_memory_preflight_drops_optional_cache_before_operating_cap)
{
	texas::MultiwayMemoryInputs inputs;
	inputs.continuation_cache_bytes = 4'096U;
	const auto baseline = texas::preflight_multiway_memory(
		limits(), texas::MultiwayMemoryBudget{ 1U, 10'000'000U, 9'000'000U }, inputs);
	const auto constrained = texas::preflight_multiway_memory(
		limits(),
		texas::MultiwayMemoryBudget{
			1U,
			baseline.estimate.total_bytes + 1U,
			baseline.estimate.mandatory_bytes },
		inputs);

	EXPECT_EQ(constrained.status, texas::MultiwayMemoryStatus::Warning);
	EXPECT_TRUE(constrained.degraded);
	EXPECT_TRUE(!constrained.optional_continuation_cache_admitted);
	EXPECT_EQ(constrained.estimate.admitted_bytes, constrained.estimate.mandatory_bytes);
	EXPECT_EQ(
		constrained.highest_admitted_stage,
		texas::MultiwayMemoryAdmissionStage::WorkerDeltas);
}

TEST_CASE(multiway_memory_preflight_stops_before_rejected_row_allocation)
{
	const auto roomy = texas::preflight_multiway_memory(
		limits(), texas::MultiwayMemoryBudget{ 1U, 10'000'000U, 9'000'000U });
	const auto root_bytes = roomy.estimate.mandatory_bytes - roomy.estimate.sparse_row_bytes - roomy.estimate.sparse_value_bytes - roomy.estimate.worker_delta_bytes - roomy.estimate.discovery_stream_bytes - roomy.estimate.merge_scratch_bytes - roomy.estimate.continuation_scratch_bytes;
	const auto rejected = texas::preflight_multiway_memory(
		limits(),
		texas::MultiwayMemoryBudget{ 1U, roomy.estimate.total_bytes + 1U, root_bytes });

	EXPECT_EQ(rejected.status, texas::MultiwayMemoryStatus::Rejected);
	EXPECT_EQ(rejected.highest_admitted_stage, texas::MultiwayMemoryAdmissionStage::Root);
	EXPECT_EQ(rejected.estimate.admitted_bytes, root_bytes);
}

TEST_CASE(multiway_memory_preflight_rejects_synthetic_maximums_without_estimate_overflow)
{
	auto maximum = limits();
	maximum.max_public_states = std::numeric_limits<std::size_t>::max();
	maximum.max_sparse_rows = std::numeric_limits<std::size_t>::max();
	maximum.max_sparse_values = std::numeric_limits<std::size_t>::max();
	const auto rejected = texas::preflight_multiway_memory(maximum);

	EXPECT_EQ(rejected.status, texas::MultiwayMemoryStatus::Rejected);
	EXPECT_EQ(rejected.highest_admitted_stage, texas::MultiwayMemoryAdmissionStage::None);
	EXPECT_EQ(rejected.estimate.total_bytes, std::numeric_limits<std::uint64_t>::max());
}
