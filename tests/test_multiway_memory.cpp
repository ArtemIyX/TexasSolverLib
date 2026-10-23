#include "solver/multiway/engine/multiway_memory.hpp"
#include "solver/multiway/continuation/multiway_continuation_selector.hpp"
#include "solver/multiway/engine/multiway_compact_storage.hpp"
#include "solver/multiway/engine/multiway_traversal.hpp"
#include "solver/multiway/engine/multiway_allocation_probe.hpp"
#include "util/tracy.hpp"
#include "test_harness.hpp"

#include <cstddef>
#include <stdexcept>
#include <limits>
#include <new>
#include <string>
#include <utility>

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

TEST_CASE(multiway_memory_component_is_checked_and_saturating)
{
	using namespace texas::solver::multiway;
	MultiwayMemoryComponent component;
	component.add_buffer(std::numeric_limits<std::size_t>::max(), std::numeric_limits<std::size_t>::max(), sizeof(std::uint64_t));
	EXPECT_EQ(component.live_element_bytes, std::numeric_limits<std::uint64_t>::max());
	EXPECT_EQ(component.retained_capacity_bytes, std::numeric_limits<std::uint64_t>::max());
	EXPECT_TRUE(component.arithmetic_saturated);
	EXPECT_TRUE(component.valid());
	EXPECT_EQ(multiway_memory_saturating_add(std::numeric_limits<std::uint64_t>::max() - 1U, 2U), std::numeric_limits<std::uint64_t>::max());
	MultiwayMemoryComponent known;
	known.add_buffer(1U, 2U, sizeof(std::uint32_t));
	known.add_buffer(0U, 0U, sizeof(std::uint32_t));
	EXPECT_TRUE(known.allocation_count_known);
	EXPECT_EQ(known.allocation_count, 1U);
}

TEST_CASE(multiway_memory_snapshots_report_live_and_retained_bytes_after_rewind)
{
	using namespace texas::solver::multiway;
	static_assert(noexcept(std::declval<const MultiwayWorkerDeltaStream&>().memory_snapshot()));
	static_assert(noexcept(std::declval<const MultiwayContinuationDeltaStream&>().memory_snapshot()));
	static_assert(noexcept(std::declval<const MultiwayDiscoveryStream&>().memory_snapshot()));

	MultiwayWorkerDeltaStream worker_stream(0U, 4U);
	const auto empty = worker_stream.memory_snapshot();
	EXPECT_EQ(empty.live_element_bytes, 0U);
	EXPECT_EQ(empty.retained_capacity_bytes, 4U * sizeof(MultiwayWorkerDelta));
	MultiwayWorkerDelta delta;
	delta.infoset = { { 7U }, 0 };
	delta.trajectory_id = 1U;
	EXPECT_TRUE(worker_stream.try_append(delta));
	const auto filled = worker_stream.memory_snapshot();
	EXPECT_EQ(filled.live_element_bytes, sizeof(MultiwayWorkerDelta));
	EXPECT_EQ(filled.retained_capacity_bytes, empty.retained_capacity_bytes);
	worker_stream.rewind(0U);
	const auto rewound = worker_stream.memory_snapshot();
	EXPECT_EQ(rewound.live_element_bytes, 0U);
	EXPECT_EQ(rewound.retained_capacity_bytes, empty.retained_capacity_bytes);

	MultiwayDiscoveryStream discovery(3U);
	EXPECT_TRUE(discovery.try_append_row({ { { 1U }, 0 }, 2U, 2U }));
	const auto discovery_filled = discovery.memory_snapshot();
	EXPECT_EQ(discovery_filled.live_element_bytes, sizeof(MultiwaySparseRowShape));
	discovery.rewind();
	const auto discovery_rewound = discovery.memory_snapshot();
	EXPECT_EQ(discovery_rewound.live_element_bytes, 0U);
	EXPECT_EQ(discovery_rewound.retained_capacity_bytes, discovery_filled.retained_capacity_bytes);
}

TEST_CASE(multiway_memory_snapshots_are_allocation_free)
{
	using namespace texas::solver::multiway;
	MultiwayWorkerDeltaStream worker_stream(0U, 2U);
	MultiwayContinuationDeltaStream continuation_stream(0U, 2U);
	MultiwayDiscoveryStream discovery(2U);
	MultiwayAllocationProbe probe;
	{
		MultiwayAllocationProbeScope scope(probe);
		(void)worker_stream.memory_snapshot();
		(void)continuation_stream.memory_snapshot();
		(void)discovery.memory_snapshot();
	}
	const auto allocations = probe.snapshot();
	EXPECT_EQ(allocations.allocation_count, 0U);
	EXPECT_EQ(allocations.allocated_bytes, 0U);
}

TEST_CASE(multiway_tracy_memory_wrappers_are_noops_in_normal_build)
{
	static_assert(!texas::util::tracy_memory_events_enabled);
	int value = 0;
	texas::util::tracy_memory_alloc(&value, sizeof(value));
	texas::util::tracy_memory_free(&value);
	texas::util::tracy_memory_free(nullptr);
}

TEST_CASE(multiway_global_allocation_probe_covers_all_allocation_paths)
{
	using namespace texas::solver::multiway;
	MultiwayAllocationProbe probe;
	void* scalar = nullptr;
	void* array = nullptr;
	void* aligned = nullptr;
	void* aligned_array = nullptr;
	void* nothrow = nullptr;
	void* nothrow_aligned = nullptr;
	{
		MultiwayAllocationProbeScope scope(probe);
		scalar = ::operator new(24U);
		array = ::operator new[](32U);
		aligned = ::operator new(48U, std::align_val_t(64U));
		aligned_array = ::operator new[](80U, std::align_val_t(64U));
		nothrow = ::operator new(16U, std::nothrow);
		nothrow_aligned = ::operator new(32U, std::align_val_t(64U), std::nothrow);
	}
	EXPECT_TRUE(scalar != nullptr);
	EXPECT_TRUE(array != nullptr);
	EXPECT_TRUE(aligned != nullptr);
	EXPECT_TRUE(aligned_array != nullptr);
	EXPECT_TRUE(nothrow != nullptr);
	EXPECT_TRUE(nothrow_aligned != nullptr);
	const auto allocations = probe.snapshot();
	EXPECT_TRUE(allocations.allocation_count >= 6U);

	::operator delete(scalar, 24U);
	::operator delete[](array, 32U);
	::operator delete(aligned, 48U, std::align_val_t(64U));
	::operator delete[](aligned_array, 80U, std::align_val_t(64U));
	::operator delete(nothrow);
	::operator delete(nothrow_aligned, 32U, std::align_val_t(64U));

	void* failed_nothrow = ::operator new(std::numeric_limits<std::size_t>::max(), std::nothrow);
	EXPECT_TRUE(failed_nothrow == nullptr);
	void* failed_aligned_nothrow = ::operator new(
		std::numeric_limits<std::size_t>::max(), std::align_val_t(64U), std::nothrow);
	EXPECT_TRUE(failed_aligned_nothrow == nullptr);
}

TEST_CASE(multiway_memory_snapshot_collection_preserves_phase_order_and_validation)
{
	using namespace texas::solver::multiway;
	MultiwayMemorySubsystemSnapshotCollection empty_snapshots;
	EXPECT_TRUE(empty_snapshots.record(MultiwayMemoryPhase::ProcessStart, {}));
	EXPECT_TRUE(empty_snapshots.record(MultiwayMemoryPhase::BeforeBucketMap, {}));
	EXPECT_TRUE(empty_snapshots.valid());

	MultiwayMemorySubsystemSnapshotCollection snapshots;
	for (std::size_t index = 0U; index < MULTIWAY_MEMORY_PHASE_COUNT; ++index)
	{
		MultiwayMemorySubsystemSnapshot snapshot;
		snapshot.worker_count = index == static_cast<std::size_t>(MultiwayMemoryPhase::AfterWorkerPoolConstruction) ? 4U : 0U;
		snapshot.workers[0U].regret_delta_stream.add_buffer(1U, 2U, sizeof(MultiwayWorkerDelta));
		snapshot.recompute_totals();
		EXPECT_TRUE(snapshots.record(static_cast<MultiwayMemoryPhase>(index), snapshot));
	}
	EXPECT_TRUE(snapshots.completed);
	EXPECT_TRUE(snapshots.valid());
	EXPECT_EQ(snapshots.snapshots[6U].worker_count, 4U);
	EXPECT_TRUE(snapshots.snapshots[6U].worker_regret_delta_stream.grouping_only);
	EXPECT_TRUE(snapshots.snapshots[6U].retained_capacity_bytes >= snapshots.snapshots[6U].live_bytes);
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
