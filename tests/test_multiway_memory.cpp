#include "solver/multiway/engine/multiway_memory.hpp"
#include "solver/multiway/blueprint/multiway_export.hpp"
#include "solver/multiway/continuation/multiway_continuation_selector.hpp"
#include "solver/multiway/engine/multiway_compact_storage.hpp"
#include "solver/multiway/engine/multiway_traversal.hpp"
#include "solver/multiway/engine/multiway_allocation_probe.hpp"
#include "util/tracy.hpp"
#include "test_harness.hpp"

#include <cstddef>
#include <array>
#include <stdexcept>
#include <limits>
#include <new>
#include <string>
#include <thread>
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

TEST_CASE(multiway_external_page_lookup_index_bound_accounts_for_rows_and_value_tails)
{
	using namespace texas::solver::multiway;
	EXPECT_EQ(multiway_external_page_lookup_max_page_id(1000U, 10000U, 16U), 1625U);
	EXPECT_THROW(multiway_external_page_lookup_max_page_id(1U, 1U, 0U), std::invalid_argument);
	EXPECT_THROW(multiway_external_page_lookup_max_page_id(
					 std::numeric_limits<std::uint64_t>::max(), 16U, 16U),
		std::length_error);
}

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

	::operator delete(scalar);
	::operator delete[](array);
	::operator delete(aligned, std::align_val_t(64U));
	::operator delete[](aligned_array, std::align_val_t(64U));
	::operator delete(nothrow);
	::operator delete(nothrow_aligned, std::align_val_t(64U));

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
	EXPECT_TRUE(snapshot.valid());
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
	if (snapshot.total_page_faults_available)
		EXPECT_TRUE(snapshot.total_page_faults > 0U);
#if defined(__linux__)
	EXPECT_TRUE(snapshot.minor_page_faults_available);
	EXPECT_TRUE(snapshot.major_page_faults_available);
	EXPECT_TRUE(snapshot.total_page_faults_available);
	EXPECT_EQ(snapshot.total_page_faults,
		multiway_memory_saturating_add(snapshot.minor_page_faults, snapshot.major_page_faults));
#endif
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
	snapshot.private_committed_bytes = 0U;
	snapshot.total_page_faults = 1U;
	EXPECT_TRUE(!snapshot.valid());
}

TEST_CASE(multiway_process_memory_snapshot_validates_page_fault_order)
{
	using namespace texas::solver::multiway;
	MultiwayProcessMemorySnapshotCollection snapshots;
	MultiwayProcessMemorySnapshot first;
	first.minor_page_faults_available = true;
	first.major_page_faults_available = true;
	first.total_page_faults_available = true;
	first.minor_page_faults = 4U;
	first.major_page_faults = 2U;
	first.total_page_faults = 6U;
	EXPECT_TRUE(snapshots.record(MultiwayMemoryPhase::ProcessStart, first));
	first.minor_page_faults = 3U;
	first.total_page_faults = 5U;
	EXPECT_TRUE(snapshots.record(MultiwayMemoryPhase::BeforeBucketMap, first));
	EXPECT_TRUE(!snapshots.valid());
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
	EXPECT_TRUE(std::string(rejected.rejected_category).find("root") != std::string::npos);
}

TEST_CASE(multiway_memory_budget_rejects_invalid_limits)
{
	const texas::MultiwayMemoryBudget invalid{ 2, 1 };
	EXPECT_THROW(invalid.validate(), std::invalid_argument);
}

TEST_CASE(multiway_export_memory_plan_and_scratch_use_exact_checked_values)
{
	using namespace texas::solver::multiway;
	MultiwayExportPlanMemoryValues plan;
	plan.shape_count = 3U;
	plan.row_count = 12U;
	plan.action_count = 48U;
	plan.serialized_bytes = 4096U;
	plan.order_index_bytes = 12U;
	MultiwayExportProcessGateConfig config;
	config.exporter_headroom_bytes = 64U;
	config.encoder_buffer_bytes = 32U;
	const auto scratch = make_multiway_export_scratch_estimate(plan, config);
	EXPECT_EQ(scratch.order_index_bytes, 12U);
	EXPECT_EQ(scratch.strategy_scratch_bytes,
		static_cast<std::uint64_t>(MULTIWAY_MAX_ABSTRACTED_ACTIONS * sizeof(double)));
	EXPECT_EQ(scratch.action_scratch_bytes,
		static_cast<std::uint64_t>(MULTIWAY_MAX_ABSTRACTED_ACTIONS * sizeof(MultiwayQuantizedRootAction)));
	EXPECT_EQ(scratch.total_bytes,
		scratch.order_index_bytes + scratch.strategy_scratch_bytes + scratch.action_scratch_bytes + 32U + 64U);
}

TEST_CASE(multiway_export_memory_gates_saturate_and_fail_closed)
{
	using namespace texas::solver::multiway;
	MultiwayExportPlanMemoryValues plan;
	plan.serialized_bytes = std::numeric_limits<std::uint64_t>::max();
	plan.order_index_bytes = std::numeric_limits<std::uint64_t>::max();
	MultiwayProcessMemorySnapshot process;
	process.current_working_set_available = true;
	process.current_working_set_bytes = 1U;
	MultiwayExportProcessGateConfig process_config;
	process_config.hard_limit_bytes = std::numeric_limits<std::uint64_t>::max();
	process_config.exporter_headroom_bytes = 0U;
	process_config.encoder_buffer_bytes = 0U;
	const auto process_gate = evaluate_multiway_export_process_gate(plan, process, process_config);
	EXPECT_EQ(process_gate.status, MultiwayExportGateStatus::Rejected);
	EXPECT_EQ(process_gate.rejection_reason, MultiwayExportProcessRejectionReason::EstimateOverflow);

	MultiwayExportSystemGateConfig system_config;
	system_config.enabled = true;
	MultiwaySystemMemorySnapshot unavailable;
	const auto system_gate = evaluate_multiway_export_system_gate(
		plan, {}, unavailable, system_config);
	EXPECT_EQ(system_gate.status, MultiwayExportGateStatus::Unavailable);
	EXPECT_EQ(system_gate.rejection_reason, MultiwayExportSystemRejectionReason::AvailablePhysicalMemoryUnavailable);
}

TEST_CASE(multiway_export_disk_gate_reserves_publication_space_and_fails_closed)
{
	using namespace texas::solver::multiway;
	const auto admitted = evaluate_multiway_export_disk_gate(100U, 50U, true, 250U);
	EXPECT_EQ(admitted.status, MultiwayExportDiskGateStatus::Admitted);
	EXPECT_EQ(admitted.required_free_bytes, 250U);
	const auto bounded_shard = evaluate_multiway_export_disk_gate(
		200U, 40U, 10U, true, 250U);
	EXPECT_EQ(bounded_shard.status, MultiwayExportDiskGateStatus::Admitted);
	EXPECT_EQ(bounded_shard.temporary_overlap_bytes, 40U);
	EXPECT_EQ(bounded_shard.required_free_bytes, 250U);
	const auto bounded_shard_insufficient = evaluate_multiway_export_disk_gate(
		200U, 40U, 10U, true, 249U);
	EXPECT_EQ(bounded_shard_insufficient.status, MultiwayExportDiskGateStatus::Rejected);
	EXPECT_EQ(bounded_shard_insufficient.required_free_bytes, 250U);
	const auto insufficient = evaluate_multiway_export_disk_gate(100U, 50U, true, 249U);
	EXPECT_EQ(insufficient.status, MultiwayExportDiskGateStatus::Rejected);
	EXPECT_EQ(insufficient.rejection_reason, MultiwayExportDiskRejectionReason::InsufficientFreeSpace);
	const auto unavailable = evaluate_multiway_export_disk_gate(100U, 0U, false, 0U);
	EXPECT_EQ(unavailable.status, MultiwayExportDiskGateStatus::Unavailable);
	EXPECT_EQ(unavailable.rejection_reason, MultiwayExportDiskRejectionReason::FreeSpaceUnavailable);
	const auto overflow = evaluate_multiway_export_disk_gate(
		std::numeric_limits<std::uint64_t>::max(), 0U, true,
		std::numeric_limits<std::uint64_t>::max());
	EXPECT_EQ(overflow.status, MultiwayExportDiskGateStatus::Rejected);
	EXPECT_EQ(overflow.rejection_reason, MultiwayExportDiskRejectionReason::RequiredSpaceOverflow);
}

TEST_CASE(multiway_export_memory_gates_honor_process_and_system_boundaries)
{
	using namespace texas::solver::multiway;
	MultiwayExportPlanMemoryValues plan;
	plan.serialized_bytes = 100U;
	MultiwayProcessMemorySnapshot process;
	process.current_working_set_available = true;
	process.current_working_set_bytes = 500U;
	MultiwayExportProcessGateConfig process_config;
	process_config.exporter_headroom_bytes = 0U;
	process_config.encoder_buffer_bytes = 0U;
	const auto estimate = make_multiway_export_process_estimate(plan, process, process_config);
	process_config.hard_limit_bytes = estimate.estimated_process_export_peak_bytes;
	EXPECT_EQ(evaluate_multiway_export_process_gate(plan, process, process_config).status,
		MultiwayExportGateStatus::Admitted);
	process_config.hard_limit_bytes -= 1U;
	const auto process_rejected = evaluate_multiway_export_process_gate(plan, process, process_config);
	EXPECT_EQ(process_rejected.status, MultiwayExportGateStatus::Rejected);
	EXPECT_EQ(process_rejected.rejection_reason, MultiwayExportProcessRejectionReason::ProcessLimitExceeded);

	const auto scratch = make_multiway_export_scratch_estimate(plan, process_config);
	MultiwaySystemMemorySnapshot system;
	system.available_physical_available = true;
	MultiwayExportSystemGateConfig system_config;
	system_config.enabled = true;
	system_config.system_reserve_bytes = 7U;
	const auto required = plan.serialized_bytes + scratch.total_bytes + system_config.system_reserve_bytes;
	system.available_physical_bytes = required;
	EXPECT_EQ(evaluate_multiway_export_system_gate(plan, scratch, system, system_config).status,
		MultiwayExportGateStatus::Admitted);
	system.available_physical_bytes = required - 1U;
	const auto system_rejected = evaluate_multiway_export_system_gate(plan, scratch, system, system_config);
	EXPECT_EQ(system_rejected.status, MultiwayExportGateStatus::Rejected);
	EXPECT_EQ(system_rejected.rejection_reason, MultiwayExportSystemRejectionReason::AvailablePhysicalMemoryInsufficient);
	EXPECT_TRUE(std::string(multiway_export_process_rejection_reason_name(process_rejected.rejection_reason)) != std::string(multiway_export_system_rejection_reason_name(system_rejected.rejection_reason)));
}

TEST_CASE(multiway_system_memory_guard_enforces_total_memory_minus_reserve)
{
	using namespace texas::solver::multiway;
	MultiwaySystemMemoryGuardConfig config;
	config.enabled = true;
	config.minimum_available_physical_bytes = 1'500U;
	config.validate();

	MultiwaySystemMemorySnapshot snapshot;
	snapshot.total_physical_available = true;
	snapshot.available_physical_available = true;
	snapshot.total_physical_bytes = 64'000U;
	snapshot.available_physical_bytes = config.minimum_available_physical_bytes;
	snapshot.used_physical_bytes = snapshot.total_physical_bytes - snapshot.available_physical_bytes;
	const auto admitted = evaluate_multiway_system_memory_guard(snapshot, config);
	EXPECT_EQ(admitted.status, MultiwaySystemMemoryGuardStatus::Admitted);
	EXPECT_EQ(admitted.maximum_used_physical_bytes, 62'500U);
	EXPECT_EQ(admitted.used_physical_bytes, 62'500U);

	snapshot.available_physical_bytes = config.minimum_available_physical_bytes - 1U;
	snapshot.used_physical_bytes = snapshot.total_physical_bytes - snapshot.available_physical_bytes;
	const auto rejected = evaluate_multiway_system_memory_guard(snapshot, config);
	EXPECT_EQ(rejected.status, MultiwaySystemMemoryGuardStatus::Rejected);
	EXPECT_EQ(std::string(rejected.rejected_category), "system physical memory ceiling");
	EXPECT_TRUE(rejected.used_physical_bytes > rejected.maximum_used_physical_bytes);

	MultiwaySystemMemorySnapshot unavailable;
	const auto unavailable_result = evaluate_multiway_system_memory_guard(unavailable, config);
	EXPECT_EQ(unavailable_result.status, MultiwaySystemMemoryGuardStatus::Unavailable);
}

TEST_CASE(multiway_export_memory_delta_preserves_process_and_private_categories)
{
	using namespace texas::solver::multiway;
	MultiwayProcessMemorySnapshot before;
	before.current_working_set_available = true;
	before.current_working_set_bytes = 100U;
	before.private_committed_available = true;
	before.private_committed_bytes = 80U;
	MultiwayProcessMemorySnapshot after = before;
	after.current_working_set_bytes = 135U;
	after.private_committed_bytes = 92U;
	const auto delta = observe_multiway_export_memory_delta(before, after);
	EXPECT_EQ(delta.working_set_delta_bytes, 35U);
	EXPECT_EQ(delta.private_committed_delta_bytes, 12U);
	EXPECT_EQ(delta.before_working_set_bytes, 100U);
	EXPECT_EQ(delta.after_private_committed_bytes, 92U);
	EXPECT_TRUE(delta.valid());
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
	EXPECT_EQ(result.estimate.export_bytes, 0U);
	EXPECT_EQ(result.estimate.continuation_cache_bytes, 23U);
	EXPECT_EQ(result.estimate.admitted_bytes, result.estimate.total_bytes);
	EXPECT_EQ(
		result.highest_admitted_stage,
		texas::MultiwayMemoryAdmissionStage::OptionalContinuationCache);
}

TEST_CASE(multiway_memory_preflight_uses_each_explicit_worker_stream_capacity)
{
	auto explicit_limits = limits();
	explicit_limits.max_regret_delta_entries_per_worker = 2U;
	explicit_limits.max_continuation_delta_entries_per_worker = 3U;
	explicit_limits.max_discovery_public_states_per_worker = 5U;
	explicit_limits.max_discovery_rows_per_worker = 7U;
	const auto result = texas::preflight_multiway_memory(
		explicit_limits, texas::MultiwayMemoryBudget{ 1U, 10'000'000U, 9'000'000U });
	const auto workers = static_cast<std::uint64_t>(explicit_limits.worker_count);
	const auto expected_worker_bytes =
		workers * 2U * sizeof(texas::MultiwayWorkerDelta) + workers * 3U * sizeof(texas::MultiwayContinuationDelta) + workers * (sizeof(texas::MultiwayPrivateWorkerScratch) + sizeof(texas::MultiwayWorkerDeltaStream) + sizeof(texas::MultiwayContinuationDeltaStream) + sizeof(std::thread));
	const auto expected_discovery_bytes = workers * (5U * sizeof(texas::MultiwayPublicStateDescriptor) + 7U * sizeof(texas::MultiwaySparseRowShape));
	const auto expected_merge_bytes = workers * (2U * (sizeof(texas::MultiwayWorkerDelta) + 3U * sizeof(std::uint64_t) + sizeof(void*)) + 3U * sizeof(texas::MultiwayContinuationDelta));
	EXPECT_EQ(result.estimate.worker_delta_bytes, expected_worker_bytes);
	EXPECT_EQ(result.estimate.discovery_stream_bytes, expected_discovery_bytes);
	EXPECT_EQ(result.estimate.merge_scratch_bytes,
		expected_merge_bytes + explicit_limits.max_sparse_values * texas::MultiwaySolverCoordinator::pending_merge_cell_size());
}

TEST_CASE(multiway_memory_preflight_uses_packed_catalog_and_separates_output_cache)
{
	auto large = limits();
	large.max_public_states = 4'000'000U;
	large.max_sparse_rows = 5'000'000U;
	large.max_sparse_values = 100'000'000U;
	large.storage_backend = texas::MultiwaySolverLimits::StorageBackend::CompactInt32;
	texas::MultiwayMemoryInputs inputs;
	inputs.public_catalog_menu_capacity = 4'000'000U;
	inputs.public_catalog_action_capacity = 4'000'000U * texas::MULTIWAY_MAX_ABSTRACTED_ACTIONS;
	inputs.checkpoint_peak_bytes = 4096U;
	inputs.export_peak_bytes = 2048U;
	inputs.disk_output_bytes = 9'000'000U;
	inputs.file_cache_allowance_bytes = 7'000'000U;
	const auto result = texas::preflight_multiway_memory(
		large, texas::MultiwayMemoryBudget{ 1U, std::numeric_limits<std::uint64_t>::max(), std::numeric_limits<std::uint64_t>::max() - 1U }, inputs);
	const auto expected = texas::estimate_multiway_catalog_capacity(
		4'000'000U, 4'000'000U, 4'000'000U * texas::MULTIWAY_MAX_ABSTRACTED_ACTIONS);
	EXPECT_EQ(result.estimate.public_state_bytes, expected.total_bytes);
	EXPECT_EQ(result.estimate.export_bytes, 0U);
	EXPECT_EQ(result.estimate.disk_output_bytes, 9'000'000U);
	EXPECT_EQ(result.estimate.file_cache_allowance_bytes, 7'000'000U);
	EXPECT_EQ(result.estimate.checkpoint_transient_bytes, 4096U);
	EXPECT_EQ(result.estimate.export_transient_bytes, 2048U);
	EXPECT_EQ(result.estimate.estimated_working_set_peak_bytes,
		result.estimate.additional_private_capacity_bytes + result.estimate.public_catalog_reallocation_peak_bytes + result.estimate.compact_reallocation_peak_bytes + 4096U);
}

TEST_CASE(multiway_memory_preflight_34m_catalog_fixture_has_no_kibibyte_multiplier)
{
	auto large = limits();
	large.max_public_states = 34'000'000U;
	large.max_sparse_rows = 5'000'000U;
	large.max_sparse_values = 100'000'000U;
	large.storage_backend = texas::MultiwaySolverLimits::StorageBackend::CompactInt32;
	large.worker_count = 8U;
	large.max_regret_delta_entries_per_worker = 64'000U;
	large.max_continuation_delta_entries_per_worker = 1'024U;
	large.max_discovery_public_states_per_worker = 200'000U;
	large.max_discovery_rows_per_worker = 100'000U;
	texas::MultiwayMemoryInputs inputs;
	inputs.public_catalog_menu_capacity = 34'000'000U;
	inputs.public_catalog_action_capacity = 34'000'000U * texas::MULTIWAY_MAX_ABSTRACTED_ACTIONS;
	inputs.existing_working_set_bytes = 15'950'000'000ULL;
	inputs.existing_private_committed_bytes = 546'232'440ULL;
	inputs.mapped_virtual_bytes = 15'369'522'466ULL;
	inputs.checkpoint_peak_bytes = 34'000'000ULL * 2U * sizeof(std::uint32_t) + 5'000'000ULL * sizeof(std::uint32_t) + texas::MULTIWAY_DEFAULT_CHECKPOINT_WRITER_BUFFER_BYTES;
	texas::MultiwayExportPlanMemoryValues export_plan;
	texas::MultiwayExportProcessGateConfig export_config;
	inputs.export_peak_bytes = texas::make_multiway_export_scratch_estimate(export_plan, export_config).total_bytes;
	const auto result = texas::preflight_multiway_memory(
		large, texas::MultiwayMemoryBudget{ 1U, std::numeric_limits<std::uint64_t>::max(), std::numeric_limits<std::uint64_t>::max() - 1U }, inputs);
	const auto expected = texas::estimate_multiway_catalog_capacity(
		34'000'000U, 34'000'000U, 34'000'000U * texas::MULTIWAY_MAX_ABSTRACTED_ACTIONS);
	EXPECT_EQ(result.estimate.public_state_bytes, expected.total_bytes);
	EXPECT_EQ(result.estimate.sparse_value_bytes, 100'000'000ULL * (sizeof(std::int32_t) + sizeof(std::uint64_t)));
	EXPECT_EQ(result.estimate.compact_page_bytes,
		100'000'000ULL * texas::MultiwayCompactStorage::page_metadata_size());
	EXPECT_TRUE(result.estimate.merge_scratch_bytes >= 100'000'000ULL * texas::MultiwaySolverCoordinator::pending_merge_cell_size());
	EXPECT_TRUE(result.estimate.public_state_bytes < 34'000'000ULL * 1024ULL);
	const auto working_set_gate = texas::evaluate_multiway_working_set_preflight(
		result, { 51'539'607'552ULL, 0U });
	EXPECT_TRUE(working_set_gate.rejected());
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

TEST_CASE(multiway_working_set_runtime_gate_checks_private_commit_and_fails_closed)
{
	using namespace texas::solver::multiway;
	MultiwayWorkingSetGateConfig config{ 48U, 0U, 64U };
	MultiwayProcessMemorySnapshot snapshot;
	snapshot.current_working_set_available = true;
	snapshot.current_working_set_bytes = 40U;
	const auto missing = evaluate_multiway_working_set_runtime(snapshot, config);
	EXPECT_EQ(missing.status, MultiwayWorkingSetGateStatus::Unavailable);
	EXPECT_EQ(std::string(missing.rejected_category), std::string("private commit telemetry"));

	snapshot.private_committed_available = true;
	snapshot.private_committed_bytes = 65U;
	const auto over_limit = evaluate_multiway_working_set_runtime(snapshot, config);
	EXPECT_EQ(over_limit.status, MultiwayWorkingSetGateStatus::Rejected);
	EXPECT_EQ(std::string(over_limit.rejected_category), std::string("process private commit"));

	snapshot.private_committed_bytes = 60U;
	const auto admitted = evaluate_multiway_working_set_runtime(snapshot, config);
	EXPECT_EQ(admitted.status, MultiwayWorkingSetGateStatus::Admitted);
	EXPECT_EQ(admitted.observed_private_commit_bytes, 60U);
}

TEST_CASE(multiway_memory_preflight_stops_before_rejected_row_allocation)
{
	const auto roomy = texas::preflight_multiway_memory(
		limits(), texas::MultiwayMemoryBudget{ 1U, 10'000'000U, 9'000'000U });
	const auto root_bytes = roomy.estimate.public_state_bytes + roomy.estimate.public_catalog_reallocation_peak_bytes + roomy.estimate.blueprint_index_bytes + roomy.estimate.range_row_bytes + roomy.estimate.future_bucket_cache_bytes + roomy.estimate.off_tree_menu_bytes + roomy.estimate.export_bytes;
	const auto rejected = texas::preflight_multiway_memory(
		limits(),
		texas::MultiwayMemoryBudget{ 1U, roomy.estimate.total_bytes + 1U, root_bytes });

	EXPECT_EQ(rejected.status, texas::MultiwayMemoryStatus::Rejected);
	EXPECT_EQ(rejected.highest_admitted_stage, texas::MultiwayMemoryAdmissionStage::Root);
	EXPECT_EQ(rejected.estimate.admitted_bytes, root_bytes);
	EXPECT_TRUE(std::string(rejected.rejected_category).find("sparse rows") != std::string::npos);
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

TEST_CASE(multiway_memory_estimate_separates_zero_mapping_from_baselines)
{
	texas::MultiwayMemoryInputs inputs;
	inputs.existing_working_set_bytes = 100U;
	inputs.existing_private_committed_bytes = 80U;
	const auto result = texas::preflight_multiway_memory(
		limits(), texas::MultiwayMemoryBudget{ 1U, 10'000'000U, 9'000'000U }, inputs);
	EXPECT_EQ(result.estimate.mapped_virtual_bytes, 0U);
	EXPECT_EQ(result.estimate.mapped_resident_allowance_bytes, 0U);
	EXPECT_EQ(result.estimate.existing_working_set_bytes, 100U);
	EXPECT_EQ(result.estimate.existing_private_committed_bytes, 80U);
	EXPECT_EQ(result.estimate.estimated_private_peak_bytes,
		80U + result.estimate.additional_private_capacity_bytes + result.estimate.public_catalog_reallocation_peak_bytes + result.estimate.compact_reallocation_peak_bytes);
	EXPECT_EQ(result.estimate.estimated_working_set_peak_bytes,
		100U + result.estimate.additional_private_capacity_bytes + result.estimate.public_catalog_reallocation_peak_bytes + result.estimate.compact_reallocation_peak_bytes);
}

TEST_CASE(multiway_memory_estimate_does_not_add_cold_mapping_to_private_peak)
{
	texas::MultiwayMemoryInputs inputs;
	inputs.existing_working_set_bytes = 100U;
	inputs.existing_private_committed_bytes = 80U;
	inputs.mapped_virtual_bytes = 1'000U;
	inputs.mapped_resident_allowance_bytes = 1'000U;
	const auto result = texas::preflight_multiway_memory(
		limits(), texas::MultiwayMemoryBudget{ 1U, 10'000'000U, 9'000'000U }, inputs);
	EXPECT_EQ(result.estimate.mapped_virtual_bytes, 1'000U);
	EXPECT_EQ(result.estimate.mapped_resident_allowance_bytes, 1'000U);
	EXPECT_EQ(result.estimate.estimated_private_peak_bytes,
		80U + result.estimate.additional_private_capacity_bytes + result.estimate.public_catalog_reallocation_peak_bytes + result.estimate.compact_reallocation_peak_bytes);
	EXPECT_EQ(result.estimate.estimated_working_set_peak_bytes,
		100U + 1'000U + result.estimate.additional_private_capacity_bytes + result.estimate.public_catalog_reallocation_peak_bytes + result.estimate.compact_reallocation_peak_bytes);
}

TEST_CASE(multiway_memory_estimate_excludes_fully_resident_mapping_from_allowance)
{
	texas::MultiwayMemoryInputs inputs;
	inputs.existing_working_set_bytes = 1'100U;
	inputs.existing_private_committed_bytes = 100U;
	inputs.mapped_virtual_bytes = 1'000U;
	const auto result = texas::preflight_multiway_memory(
		limits(), texas::MultiwayMemoryBudget{ 1U, 10'000'000U, 9'000'000U }, inputs);
	EXPECT_EQ(result.estimate.mapped_resident_allowance_bytes, 0U);
	EXPECT_EQ(result.estimate.estimated_working_set_peak_bytes,
		1'100U + result.estimate.additional_private_capacity_bytes + result.estimate.public_catalog_reallocation_peak_bytes + result.estimate.compact_reallocation_peak_bytes);
}

TEST_CASE(multiway_memory_estimate_counts_only_partial_mapping_residency_allowance)
{
	texas::MultiwayMemoryInputs inputs;
	inputs.existing_working_set_bytes = 1'100U;
	inputs.existing_private_committed_bytes = 100U;
	inputs.mapped_virtual_bytes = 1'000U;
	inputs.mapped_resident_allowance_bytes = 400U;
	const auto result = texas::preflight_multiway_memory(
		limits(), texas::MultiwayMemoryBudget{ 1U, 10'000'000U, 9'000'000U }, inputs);
	EXPECT_EQ(result.estimate.estimated_working_set_peak_bytes,
		1'100U + 400U + result.estimate.additional_private_capacity_bytes + result.estimate.public_catalog_reallocation_peak_bytes + result.estimate.compact_reallocation_peak_bytes);
	EXPECT_EQ(result.estimate.estimated_private_peak_bytes,
		100U + result.estimate.additional_private_capacity_bytes + result.estimate.public_catalog_reallocation_peak_bytes + result.estimate.compact_reallocation_peak_bytes);
}

TEST_CASE(multiway_memory_estimate_prefers_known_retained_capacity_over_configured_growth)
{
	texas::MultiwayMemoryInputs inputs;
	inputs.existing_working_set_bytes = 1'000U;
	inputs.existing_private_committed_bytes = 500U;
	inputs.retained_capacity_bytes = 2'500U;
	inputs.retained_capacity_known = true;
	const auto result = texas::preflight_multiway_memory(
		limits(), texas::MultiwayMemoryBudget{ 1U, 10'000'000U, 9'000'000U }, inputs);
	EXPECT_EQ(result.estimate.additional_private_capacity_bytes, 2'500U);
	EXPECT_EQ(result.estimate.actual_retained_capacity_bytes, 2'500U);
	EXPECT_TRUE(result.estimate.configured_capacity_growth_bytes != 2'500U);
	EXPECT_EQ(result.estimate.estimated_private_peak_bytes,
		3'000U + result.estimate.public_catalog_reallocation_peak_bytes + result.estimate.compact_reallocation_peak_bytes);
}

TEST_CASE(multiway_memory_estimate_counts_checkpoint_and_export_maximum_once)
{
	texas::MultiwayMemoryInputs baseline_inputs;
	baseline_inputs.existing_working_set_bytes = 100U;
	baseline_inputs.existing_private_committed_bytes = 100U;
	const auto baseline = texas::preflight_multiway_memory(
		limits(), texas::MultiwayMemoryBudget{ 1U, 10'000'000U, 9'000'000U }, baseline_inputs);
	texas::MultiwayMemoryInputs inputs = baseline_inputs;
	inputs.checkpoint_peak_bytes = 700U;
	inputs.export_peak_bytes = 900U;
	const auto result = texas::preflight_multiway_memory(
		limits(), texas::MultiwayMemoryBudget{ 1U, 10'000'000U, 9'000'000U }, inputs);
	EXPECT_EQ(result.estimate.checkpoint_transient_bytes, 700U);
	EXPECT_EQ(result.estimate.export_transient_bytes, 900U);
	EXPECT_EQ(
		result.estimate.estimated_private_peak_bytes - baseline.estimate.estimated_private_peak_bytes,
		900U);
	EXPECT_EQ(
		result.estimate.estimated_working_set_peak_bytes - baseline.estimate.estimated_working_set_peak_bytes,
		900U);
}

TEST_CASE(multiway_memory_estimate_accounts_for_allocator_overhead_in_both_peaks)
{
	texas::MultiwayMemoryInputs inputs;
	inputs.existing_working_set_bytes = 100U;
	inputs.existing_private_committed_bytes = 80U;
	inputs.allocator_overhead_bytes = 33U;
	const auto result = texas::preflight_multiway_memory(
		limits(), texas::MultiwayMemoryBudget{ 1U, 10'000'000U, 9'000'000U }, inputs);
	EXPECT_EQ(result.estimate.allocator_overhead_bytes, 33U);
	EXPECT_EQ(result.estimate.estimated_private_peak_bytes,
		80U + result.estimate.additional_private_capacity_bytes + result.estimate.public_catalog_reallocation_peak_bytes + result.estimate.compact_reallocation_peak_bytes + 33U);
	EXPECT_EQ(result.estimate.estimated_working_set_peak_bytes,
		100U + result.estimate.additional_private_capacity_bytes + result.estimate.public_catalog_reallocation_peak_bytes + result.estimate.compact_reallocation_peak_bytes + 33U);
}

TEST_CASE(multiway_memory_estimate_saturates_each_peak_on_overflow)
{
	texas::MultiwayMemoryInputs inputs;
	inputs.existing_working_set_bytes = std::numeric_limits<std::uint64_t>::max();
	inputs.existing_private_committed_bytes = std::numeric_limits<std::uint64_t>::max();
	const auto result = texas::preflight_multiway_memory(
		limits(), texas::MultiwayMemoryBudget{ 1U, std::numeric_limits<std::uint64_t>::max(), std::numeric_limits<std::uint64_t>::max() - 1U }, inputs);
	EXPECT_EQ(result.estimate.estimated_private_peak_bytes, std::numeric_limits<std::uint64_t>::max());
	EXPECT_EQ(result.estimate.estimated_working_set_peak_bytes, std::numeric_limits<std::uint64_t>::max());
	EXPECT_EQ(result.estimate.total_bytes, std::numeric_limits<std::uint64_t>::max());
}

TEST_CASE(multiway_memory_estimate_matches_task_d_f1_a1_and_a3_baselines)
{
#pragma pack(push, 8)
	struct Fixture
	{
		std::uint32_t workers;
		std::uint64_t working_set;
		std::uint64_t private_committed;
	};
#pragma pack(pop)
	const std::array<Fixture, 2> fixtures = { {
		{ 1U, 16'575'430'656ULL, 3'441'930'240ULL },
		{ 4U, 16'586'776'576ULL, 9'948'704'768ULL },
	} };
	for (const auto& fixture : fixtures)
	{
		auto f1_limits = limits();
		f1_limits.worker_count = fixture.workers;
		f1_limits.max_public_states = 2'000'000U;
		f1_limits.max_sparse_rows = 5'000'000U;
		f1_limits.max_sparse_values = 100'000'000U;
		f1_limits.max_regret_delta_entries_per_worker = 64'000U;
		f1_limits.max_continuation_delta_entries_per_worker = 1'024U;
		f1_limits.max_discovery_public_states_per_worker = 200'000U;
		f1_limits.max_discovery_rows_per_worker = 100'000U;
		f1_limits.storage_backend = texas::MultiwaySolverLimits::StorageBackend::CompactInt32;
		texas::MultiwayMemoryInputs inputs;
		inputs.existing_working_set_bytes = fixture.working_set;
		inputs.existing_private_committed_bytes = fixture.private_committed;
		inputs.mapped_virtual_bytes = 15'369'522'466ULL;
		const auto result = texas::preflight_multiway_memory(
			f1_limits, texas::MultiwayMemoryBudget{ 1U, std::numeric_limits<std::uint64_t>::max(), std::numeric_limits<std::uint64_t>::max() - 1U }, inputs);
		EXPECT_EQ(result.estimate.mapped_virtual_bytes, 15'369'522'466ULL);
		EXPECT_EQ(result.estimate.estimated_private_peak_bytes,
			fixture.private_committed + result.estimate.configured_capacity_growth_bytes + result.estimate.public_catalog_reallocation_peak_bytes + result.estimate.compact_reallocation_peak_bytes);
		EXPECT_EQ(result.estimate.estimated_working_set_peak_bytes,
			fixture.working_set + result.estimate.configured_capacity_growth_bytes + result.estimate.public_catalog_reallocation_peak_bytes + result.estimate.compact_reallocation_peak_bytes);
	}
}
