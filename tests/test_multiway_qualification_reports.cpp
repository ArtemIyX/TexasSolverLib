#include "solver/multiway/workflow/multiway_lookup_qualification.hpp"
#include "solver/multiway/workflow/multiway_training_report.hpp"
#include "solver/multiway/workflow/multiway_evidence.hpp"
#include "test_harness.hpp"

#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

TEST_CASE(multiway_lookup_qualification_gate_requires_zero_misses)
{
	texas::solver::multiway::MultiwayLookupQualificationReport report;
	EXPECT_TRUE(report.passed());
	report.missing_buckets = 1U;
	EXPECT_TRUE(!report.passed());
}

TEST_CASE(multiway_qualification_reports_publish_atomically)
{
	using namespace texas::solver::multiway;
	const auto path = std::filesystem::temp_directory_path() / "texas_solver_qualification_report_test.json";
	std::filesystem::remove(path);
	MultiwayLookupQualificationReport lookup;
	lookup.evidence.producer = [] {
		MultiwayProducerIdentity identity;
		identity.git_commit = "test";
		identity.build_configuration = "Debug";
		identity.compiler_identity = "test";
		identity.model_identity = make_multiway_model_identity(MultiwayBlueprintConfig{});
		identity.workflow_config_fingerprint = 1U;
		identity.artifact_schemas = { 4U, 2U, 1U };
		return identity;
	}();
	lookup.lookup_hits = 4U;
	lookup.policy_content_hash = 8U;
	lookup.replay_fingerprint = 9U;
	lookup.second_replay_fingerprint = 9U;
	save_multiway_lookup_qualification_report_atomic(path, lookup);
	std::ifstream input(path);
	const std::string text((std::istreambuf_iterator<char>(input)), {});
	input.close();
	EXPECT_TRUE(text.find("\"passed\": true") != std::string::npos);
	EXPECT_TRUE(text.find("\"second_replay_fingerprint\": 9") != std::string::npos);
	EXPECT_TRUE(text.find("\"policy_content_hash\": 8") != std::string::npos);
	std::filesystem::remove(path);
}

TEST_CASE(multiway_training_report_preserves_discard_telemetry)
{
	using namespace texas::solver::multiway;
	texas::solver::multiway::MultiwayBlueprintTrainingStatus status;
	status.trajectories = 20U;
	status.discarded_trajectories = 3U;
	status.scheduler_mode = MultiwaySchedulerMode::Dynamic;
	status.dynamic_chunk_size = 8U;
	status.preflop_rows = 1U;
	status.street_visits = { 10U, 20U, 30U, 40U };
	status.requested_worker_count = 16U;
	status.effective_worker_count = 8U;
	status.trajectories_per_batch = 20U;
	status.peak_worker_delta_entries = 7U;
	status.configured_max_regret_delta_entries_per_worker = 64U;
	status.configured_max_continuation_delta_entries_per_worker = 8U;
	status.configured_max_discovery_public_states_per_worker = 128U;
	status.configured_max_discovery_rows_per_worker = 96U;
	status.cumulative_worker_delta_entries = 55U;
	status.worker_active_nanoseconds = 100U;
	status.worker_scheduler_claim_nanoseconds = 15U;
	status.minimum_worker_active_nanoseconds = 40U;
	status.maximum_worker_active_nanoseconds = 60U;
	status.coordinator_wait_nanoseconds = 60U;
	status.barrier_wait_nanoseconds = 20U;
	status.delta_sort_nanoseconds = 10U;
	status.merge_nanoseconds = 5U;
	status.minimum_worker_trajectories = 2U;
	status.maximum_worker_trajectories = 3U;
	status.worker_trajectory_counts = { 10U, 10U, 0U, 0U, 0U, 0U, 0U, 0U };
	status.worker_active_nanoseconds_by_worker = { 40U, 60U, 0U, 0U, 0U, 0U, 0U, 0U };
	status.worker_scheduler_claim_nanoseconds_by_worker = { 7U, 8U, 0U, 0U, 0U, 0U, 0U, 0U };
	status.memory_preflight_estimate_bytes = 999U;
	status.elapsed_wall_nanoseconds = 100U;
	status.checkpoint_wall_nanoseconds = 200U;
	status.export_wall_nanoseconds = 300U;
	status.memory_ledger.existing_working_set_bytes = 111U;
	status.memory_ledger.existing_private_committed_bytes = 222U;
	status.memory_ledger.additional_private_capacity_bytes = 333U;
	status.memory_ledger.compact_reallocation_peak_bytes = 334U;
	status.memory_ledger.public_catalog_reallocation_peak_bytes = 338U;
	status.memory_ledger.file_cache_allowance_bytes = 335U;
	status.memory_ledger.disk_output_bytes = 336U;
	status.memory_ledger.page_metadata_bytes = 337U;
	status.memory_ledger.mapped_virtual_bytes = 444U;
	status.memory_ledger.mapped_resident_allowance_bytes = 555U;
	status.memory_ledger.checkpoint_transient_bytes = 666U;
	status.memory_ledger.export_transient_bytes = 777U;
	status.memory_ledger.estimated_private_peak_bytes = 888U;
	status.memory_ledger.estimated_working_set_peak_bytes = 999U;
	status.memory_ledger.resident_baseline_bytes = 111U;
	status.memory_ledger.regret_page_bytes = 222U;
	status.memory_ledger.coordinator_allocation_count = 333U;
	status.memory_ledger.coordinator_allocation_bytes = 444U;
	status.working_set_gate_config = { 64U, 5U, 128U };
	MultiwayProcessMemorySnapshot working_set_snapshot;
	working_set_snapshot.current_working_set_available = true;
	working_set_snapshot.current_working_set_bytes = 70U;
	working_set_snapshot.private_committed_available = true;
	working_set_snapshot.private_committed_bytes = 90U;
	status.working_set_gate = evaluate_multiway_working_set_runtime(
		working_set_snapshot, status.working_set_gate_config);
	status.system_memory_guard_config = { true, 1'500U };
	MultiwaySystemMemorySnapshot system_memory_snapshot;
	system_memory_snapshot.total_physical_available = true;
	system_memory_snapshot.available_physical_available = true;
	system_memory_snapshot.total_physical_bytes = 64'000U;
	system_memory_snapshot.available_physical_bytes = 1'500U;
	system_memory_snapshot.used_physical_bytes = 62'500U;
	status.system_memory_guard = evaluate_multiway_system_memory_guard(
		system_memory_snapshot, status.system_memory_guard_config);
	for (std::size_t phase = 0U; phase < MULTIWAY_MEMORY_PHASE_COUNT; ++phase)
		EXPECT_TRUE(status.process_memory_snapshots.record(static_cast<MultiwayMemoryPhase>(phase), {}));
	for (std::size_t phase = 0U; phase < MULTIWAY_MEMORY_PHASE_COUNT; ++phase)
	{
		MultiwayMemorySubsystemSnapshot snapshot;
		snapshot.public_catalog_nested_payloads.add_buffer(2U, 4U, sizeof(MultiwayActionDescriptor));
		snapshot.worker_count = 1U;
		snapshot.workers[0U].regret_delta_stream.add_buffer(1U, 2U, sizeof(MultiwayWorkerDelta));
		snapshot.recompute_totals();
		EXPECT_TRUE(status.memory_subsystem_snapshots.record(static_cast<MultiwayMemoryPhase>(phase), snapshot));
	}
	status.memory_subsystem_snapshots.snapshots[MULTIWAY_MEMORY_PHASE_COUNT - 1U].checkpoint_retained_buffers.add_buffer(0U, 100U, 1U);
	status.memory_subsystem_snapshots.snapshots[MULTIWAY_MEMORY_PHASE_COUNT - 1U].recompute_totals();
	const auto attributed_retained = status.memory_subsystem_snapshots.snapshots[MULTIWAY_MEMORY_PHASE_COUNT - 1U].retained_capacity_bytes;
	status.process_memory_snapshots.snapshots[MULTIWAY_MEMORY_PHASE_COUNT - 1U].private_committed_available = true;
	status.process_memory_snapshots.snapshots[MULTIWAY_MEMORY_PHASE_COUNT - 1U].private_committed_bytes = attributed_retained + 900U;
	status.process_memory_snapshots.snapshots[MULTIWAY_MEMORY_PHASE_COUNT - 1U].peak_private_committed_available = true;
	status.process_memory_snapshots.snapshots[MULTIWAY_MEMORY_PHASE_COUNT - 1U].peak_private_committed_bytes = attributed_retained + 900U;
	MultiwayProducerIdentity identity;
	identity.git_commit = "test";
	identity.build_configuration = "Debug";
	identity.compiler_identity = "test";
	identity.model_identity = make_multiway_model_identity(MultiwayBlueprintConfig{});
	identity.workflow_config_fingerprint = 1U;
	identity.artifact_schemas = { 4U, 2U, 1U };
	MultiwayEvidenceHeader evidence{
		MULTIWAY_TRAINING_REPORT_SCHEMA_VERSION,
		MultiwayWorkflowProfileKind::Acceptance,
		identity
	};
	auto report = make_multiway_training_report(status, 77U, evidence, 0U, 1'000U);
	EXPECT_EQ(report.training_wall_nanoseconds, 100U);
	EXPECT_EQ(report.scheduler_mode, MultiwaySchedulerMode::Dynamic);
	EXPECT_EQ(report.dynamic_chunk_size, 8U);
	EXPECT_EQ(report.checkpoint_wall_nanoseconds, 200U);
	EXPECT_EQ(report.export_wall_nanoseconds, 300U);
	EXPECT_TRUE(report.total_wrapper_wall_available);
	report.checkpoint_bytes = 123U;
	report.checkpoint_planned_bytes = 122U;
	report.checkpoint_scratch_bytes = 121U;
	report.checkpoint_payload_hash = 120U;
	report.checkpoint_publication_mode = texas::core::AtomicPublishMode::Rename;
	report.checkpoint_process_gate_status = texas::MultiwayExportGateStatus::Admitted;
	report.checkpoint_system_gate_status = texas::MultiwayExportGateStatus::Admitted;
	report.checkpoint_write_nanoseconds = 456U;
	report.checkpoint_telemetry.public_catalog_export_nanoseconds = 12U;
	report.checkpoint_telemetry.public_state_bytes = 34U;
	report.blueprint_bytes = 789U;
	report.blueprint_export_nanoseconds = 1011U;
	report.export_telemetry.attempted = true;
	report.export_telemetry.succeeded = true;
	report.export_telemetry.plan.shape_count = 4U;
	report.export_telemetry.plan.row_count = 8U;
	report.export_telemetry.plan.action_count = 16U;
	report.export_telemetry.plan.serialized_bytes = 321U;
	report.export_telemetry.plan.compact_direct_order = true;
	report.export_telemetry.scratch.strategy_scratch_bytes = 10U;
	report.export_telemetry.scratch.action_scratch_bytes = 11U;
	report.export_telemetry.scratch.total_bytes = 21U;
	report.export_telemetry.process_estimate.current_working_set_available = true;
	report.export_telemetry.process_estimate.current_working_set_bytes = 100U;
	report.export_telemetry.process_estimate.estimated_process_export_peak_bytes = 200U;
	report.export_telemetry.process_gate.status = MultiwayExportGateStatus::Admitted;
	report.export_telemetry.process_gate.hard_limit_bytes = 500U;
	report.export_telemetry.process_gate.compared_process_working_set_bytes = 200U;
	report.export_telemetry.system_gate.status = MultiwayExportGateStatus::Disabled;
	report.export_telemetry.disk_gate.status = MultiwayExportDiskGateStatus::Admitted;
	report.export_telemetry.disk_gate.serialized_output_bytes = 321U;
	report.export_telemetry.disk_gate.required_free_bytes = 642U;
	report.export_telemetry.disk_gate.available_free_bytes = 1000U;
	report.export_telemetry.result_bytes = 321U;
	report.export_telemetry.payload_hash = 654U;
	report.export_telemetry.policy_content_hash = 655U;
	report.export_telemetry.duration_nanoseconds = 987U;
	report.export_telemetry.publication_mode = texas::core::AtomicPublishMode::Rename;
	report.export_telemetry.validate();
	report.admission_diagnostics.catalog_menu_key_nanoseconds = 1U;
	report.admission_diagnostics.catalog_menu_index_lookup_nanoseconds = 2U;
	report.admission_diagnostics.catalog_menu_equality_nanoseconds = 3U;
	report.admission_diagnostics.catalog_node_pack_nanoseconds = 4U;
	report.admission_diagnostics.catalog_reserve_nanoseconds = 5U;
	report.admission_diagnostics.catalog_append_nanoseconds = 6U;
	report.admission_diagnostics.catalog_menu_index_probes = 7U;
	report.admission_diagnostics.catalog_menu_equality_checks = 8U;
	report.admission_diagnostics.catalog_existing_menu_hits = 9U;
	report.admission_diagnostics.catalog_batch_menu_reuse_hits = 10U;
	report.admission_diagnostics.catalog_new_menus = 11U;
	report.admission_diagnostics.catalog_menu_id_collision_misses = 12U;
	report.admission_diagnostics.packed_public_state_compatibility_checks = 45U;
	report.admission_diagnostics.catalog_output_capacity_reuse_events = 33U;
	report.admission_diagnostics.catalog_output_capacity_reuse_bytes = 34U;
	report.admission_diagnostics.catalog_public_descriptor_copy_count = 35U;
	report.admission_diagnostics.catalog_public_descriptor_copy_bytes = 36U;
	report.admission_diagnostics.catalog_nested_descriptor_copy_count = 37U;
	report.admission_diagnostics.catalog_nested_descriptor_copy_bytes = 38U;
	report.admission_diagnostics.public_state_betting_board_validation_nanoseconds = 13U;
	report.admission_diagnostics.public_state_menu_validation_nanoseconds = 14U;
	report.admission_diagnostics.public_state_history_identity_validation_nanoseconds = 15U;
	report.admission_diagnostics.discovery_public_state_merge_nanoseconds = 16U;
	report.admission_diagnostics.discovery_row_merge_nanoseconds = 17U;
	report.admission_diagnostics.discovery_public_state_source_elements = 18U;
	report.admission_diagnostics.discovery_row_source_elements = 19U;
	report.admission_diagnostics.discovery_public_descriptor_copy_count = 39U;
	report.admission_diagnostics.discovery_public_descriptor_copy_bytes = 40U;
	report.admission_diagnostics.discovery_nested_descriptor_copy_count = 41U;
	report.admission_diagnostics.discovery_nested_descriptor_copy_bytes = 42U;
	report.admission_diagnostics.discovery_output_capacity_reuse_events = 43U;
	report.admission_diagnostics.discovery_output_capacity_reuse_bytes = 44U;
	report.admission_diagnostics.discovery_public_state_duplicate_candidates = 20U;
	report.admission_diagnostics.discovery_row_duplicate_candidates = 21U;
	report.admission_diagnostics.discovery_single_stream_batches = 22U;
	report.admission_diagnostics.discovery_one_stream_fast_path_uses = 23U;
	report.admission_diagnostics.parent_transition_fixed_state_nanoseconds = 24U;
	report.admission_diagnostics.parent_transition_action_edge_nanoseconds = 25U;
	report.admission_diagnostics.parent_transition_board_chance_edge_nanoseconds = 26U;
	report.admission_diagnostics.parent_transition_street_edge_nanoseconds = 27U;
	report.admission_diagnostics.parent_transition_action_edges = 28U;
	report.admission_diagnostics.parent_transition_board_chance_edges = 29U;
	report.admission_diagnostics.parent_transition_street_edges = 30U;
	report.admission_diagnostics.parent_transition_action_history_prefix_elements = 31U;
	report.admission_diagnostics.parent_transition_action_menu_probes = 32U;
	EXPECT_EQ(report.discarded_trajectories, 3U);
	EXPECT_EQ(report.accepted_trajectories, 17U);
	EXPECT_EQ(report.preflop_rows, 1U);
	EXPECT_EQ(report.deterministic_merge_fingerprint, 77U);
	EXPECT_EQ(report.street_visits[2U], 30U);
	EXPECT_EQ(report.requested_worker_count, 16U);
	EXPECT_EQ(report.effective_worker_count, 8U);
	EXPECT_EQ(report.memory_ledger.resident_baseline_bytes, 111U);
	EXPECT_EQ(report.memory_ledger.regret_page_bytes, 222U);
	EXPECT_EQ(report.memory_ledger.coordinator_allocation_count, 333U);
	EXPECT_EQ(report.memory_ledger.coordinator_allocation_bytes, 444U);
	EXPECT_EQ(report.memory_ledger.schema_version, MULTIWAY_MEMORY_ESTIMATE_SCHEMA_VERSION);
	EXPECT_EQ(report.memory_ledger.estimated_private_peak_bytes, 888U);
	EXPECT_EQ(report.memory_ledger.estimated_working_set_peak_bytes, 999U);
	EXPECT_EQ(report.working_set_gate_config.hard_limit_bytes, 64U);
	EXPECT_EQ(report.working_set_gate.resident_allowance_bytes, 5U);
	EXPECT_EQ(report.working_set_gate_config.private_commit_limit_bytes, 128U);
	EXPECT_EQ(report.working_set_gate.observed_private_commit_bytes, 90U);
	EXPECT_EQ(report.working_set_gate.compared_working_set_bytes, 75U);
	EXPECT_EQ(report.working_set_gate.status, MultiwayWorkingSetGateStatus::Rejected);
	EXPECT_EQ(report.working_set_gate.phase, MultiwayWorkingSetGatePhase::Runtime);
	EXPECT_TRUE(report.system_memory_guard_config.enabled);
	EXPECT_EQ(report.system_memory_guard_config.minimum_available_physical_bytes, 1'500U);
	EXPECT_EQ(report.system_memory_guard.status, MultiwaySystemMemoryGuardStatus::Admitted);
	EXPECT_EQ(report.system_memory_guard.maximum_used_physical_bytes, 62'500U);
	EXPECT_EQ(report.peak_worker_delta_entries, 7U);
	EXPECT_EQ(report.configured_max_regret_delta_entries_per_worker, 64U);
	EXPECT_EQ(report.configured_max_continuation_delta_entries_per_worker, 8U);
	EXPECT_EQ(report.configured_max_discovery_public_states_per_worker, 128U);
	EXPECT_EQ(report.configured_max_discovery_rows_per_worker, 96U);
	EXPECT_EQ(report.cumulative_worker_delta_entries, 55U);
	EXPECT_EQ(report.minimum_worker_active_nanoseconds, 40U);
	EXPECT_EQ(report.maximum_worker_active_nanoseconds, 60U);
	EXPECT_EQ(report.worker_scheduler_claim_nanoseconds, 15U);
	EXPECT_EQ(report.barrier_wait_nanoseconds, 20U);
	EXPECT_EQ(report.worker_trajectory_counts[1U], 10U);
	EXPECT_EQ(report.worker_active_nanoseconds_by_worker[1U], 60U);
	EXPECT_EQ(report.instrumentation_kind, std::string("none"));
	EXPECT_TRUE(report.unattributed_private_bytes_available);
	EXPECT_EQ(report.unattributed_private_bytes, 900U);
	EXPECT_TRUE(report.tracy_version.empty());
	EXPECT_TRUE(report.trace_capture_id.empty());
	EXPECT_TRUE(report.allocation_probe_valid);
	EXPECT_TRUE(!report.tracy_memory_events_enabled);
	const auto path = std::filesystem::temp_directory_path() / "texas_solver_training_report_telemetry_test.json";
	save_multiway_training_report_atomic(path, report);
	std::ifstream input(path);
	const std::string text((std::istreambuf_iterator<char>(input)), {});
	input.close();
	EXPECT_TRUE(text.find("\"checkpoint_bytes\": 123") != std::string::npos);
	EXPECT_TRUE(text.find("\"training_wall_nanoseconds\": 100") != std::string::npos);
	EXPECT_TRUE(text.find("\"checkpoint_wall_nanoseconds\": 200") != std::string::npos);
	EXPECT_TRUE(text.find("\"export_wall_nanoseconds\": 300") != std::string::npos);
	EXPECT_TRUE(text.find("\"total_wrapper_wall_nanoseconds\": 1000") != std::string::npos);
	EXPECT_TRUE(text.find("\"checkpoint_planned_bytes\": 122") != std::string::npos);
	EXPECT_TRUE(text.find("\"checkpoint_payload_hash\": 120") != std::string::npos);
	EXPECT_TRUE(text.find("\"tracy_memory_events_enabled\": false") != std::string::npos);
	EXPECT_TRUE(text.find("\"public_catalog_export_nanoseconds\": 12") != std::string::npos);
	EXPECT_TRUE(text.find("\"public_state_bytes\": 34") != std::string::npos);
	EXPECT_TRUE(text.find("\"blueprint_bytes\": 789") != std::string::npos);
	EXPECT_TRUE(text.find("\"export_disk_gate_status\": 1") != std::string::npos);
	EXPECT_TRUE(text.find("\"export_disk_gate_output_bytes\": 321") != std::string::npos);
	EXPECT_TRUE(text.find("\"export_disk_gate_temporary_overlap_bytes\": 0") != std::string::npos);
	EXPECT_TRUE(text.find("\"export_sharded_output\": false") != std::string::npos);
	EXPECT_TRUE(text.find("\"export_shard_count\": 0") != std::string::npos);
	EXPECT_TRUE(text.find("\"export_disk_gate_required_free_bytes\": 642") != std::string::npos);
	EXPECT_TRUE(text.find("\"export_disk_gate_available_free_bytes\": 1000") != std::string::npos);
	EXPECT_TRUE(text.find("\"requested_worker_count\": 16") != std::string::npos);
	EXPECT_TRUE(text.find("\"memory_preflight_estimate_bytes\": 999") != std::string::npos);
	EXPECT_TRUE(text.find("\"memory_estimate_schema_version\": 3") != std::string::npos);
	EXPECT_TRUE(text.find("\"memory_estimated_private_peak_bytes\": 888") != std::string::npos);
	EXPECT_TRUE(text.find("\"memory_estimated_working_set_peak_bytes\": 999") != std::string::npos);
	EXPECT_TRUE(text.find("\"compact_reallocation_peak_bytes\": 334") != std::string::npos);
	EXPECT_TRUE(text.find("\"public_catalog_reallocation_peak_bytes\": 338") != std::string::npos);
	EXPECT_TRUE(text.find("\"file_cache_allowance_bytes\": 335") != std::string::npos);
	EXPECT_TRUE(text.find("\"disk_output_bytes\": 336") != std::string::npos);
	EXPECT_TRUE(text.find("\"page_metadata_bytes\": 337") != std::string::npos);
	EXPECT_TRUE(text.find("\"process_working_set_limit_bytes\": 64") != std::string::npos);
	EXPECT_TRUE(text.find("\"working_set_gate_abort_phase\": 2") != std::string::npos);
	EXPECT_TRUE(text.find("\"working_set_gate_rejected_category\": \"process working set\"") != std::string::npos);
	EXPECT_TRUE(text.find("\"process_private_commit_limit_bytes\": 128") != std::string::npos);
	EXPECT_TRUE(text.find("\"working_set_gate_observed_private_commit_bytes\": 90") != std::string::npos);
	EXPECT_TRUE(text.find("\"system_memory_guard_enabled\": true") != std::string::npos);
	EXPECT_TRUE(text.find("\"system_memory_guard_status\": 1") != std::string::npos);
	EXPECT_TRUE(text.find("\"system_memory_maximum_used_physical_bytes\": 62500") != std::string::npos);
	EXPECT_TRUE(text.find("\"export_telemetry_schema_version\": 1") != std::string::npos);
	EXPECT_TRUE(text.find("\"export_row_count\": 8") != std::string::npos);
	EXPECT_TRUE(text.find("\"export_result_bytes\": 321") != std::string::npos);
	EXPECT_TRUE(text.find("\"export_result_payload_hash\": 654") != std::string::npos);
	EXPECT_TRUE(text.find("\"export_policy_content_hash\": 655") != std::string::npos);
	EXPECT_TRUE(text.find("\"export_duration_nanoseconds\": 987") != std::string::npos);
	EXPECT_TRUE(text.find("\"export_publication_mode\": \"rename\"") != std::string::npos);
	EXPECT_TRUE(text.find("\"regret_page_bytes\": 222") != std::string::npos);
	EXPECT_TRUE(text.find("\"barrier_wait_nanoseconds\": 20") != std::string::npos);
	EXPECT_TRUE(text.find("\"worker_scheduler_claim_nanoseconds\": 15") != std::string::npos);
	EXPECT_TRUE(text.find("\"scheduler_mode\": \"dynamic\"") != std::string::npos);
	EXPECT_TRUE(text.find("\"dynamic_chunk_size\": 8") != std::string::npos);
	EXPECT_TRUE(text.find("\"configured_max_regret_delta_entries_per_worker\": 64") != std::string::npos);
	EXPECT_TRUE(text.find("\"configured_max_continuation_delta_entries_per_worker\": 8") != std::string::npos);
	EXPECT_TRUE(text.find("\"configured_max_discovery_public_states_per_worker\": 128") != std::string::npos);
	EXPECT_TRUE(text.find("\"configured_max_discovery_rows_per_worker\": 96") != std::string::npos);
	EXPECT_TRUE(text.find("\"worker_scheduler_claim_nanoseconds_by_worker\": [7,8,0,0,0,0,0,0]") != std::string::npos);
	EXPECT_TRUE(text.find("\"worker_trajectory_counts\": [10,10,0,0,0,0,0,0]") != std::string::npos);
	EXPECT_TRUE(text.find("\"memory_schema_version\": 2") != std::string::npos);
	EXPECT_TRUE(text.find("\"memory_process_snapshots\": [") != std::string::npos);
	EXPECT_TRUE(text.find("\"phase\": \"process_start\"") != std::string::npos);
	EXPECT_TRUE(text.find("\"phase\": \"after_export\"") != std::string::npos);
	EXPECT_TRUE(text.find("\"private_committed\": false") != std::string::npos);
	EXPECT_TRUE(text.find("\"memory_subsystems_schema_version\": 1") != std::string::npos);
	EXPECT_TRUE(text.find("\"memory_subsystems\": {\"enabled\": true") != std::string::npos);
	EXPECT_TRUE(text.find("\"public_catalog_nested_payloads\":") != std::string::npos);
	EXPECT_TRUE(text.find("\"workers\": [{") != std::string::npos);
	EXPECT_TRUE(text.find("\"unattributed_private_bytes\": 900") != std::string::npos);
	EXPECT_TRUE(text.find("\"catalog_menu_key_nanoseconds\": 1") != std::string::npos);
	EXPECT_TRUE(text.find("\"catalog_menu_index_lookup_nanoseconds\": 2") != std::string::npos);
	EXPECT_TRUE(text.find("\"catalog_menu_equality_nanoseconds\": 3") != std::string::npos);
	EXPECT_TRUE(text.find("\"catalog_node_pack_nanoseconds\": 4") != std::string::npos);
	EXPECT_TRUE(text.find("\"catalog_reserve_nanoseconds\": 5") != std::string::npos);
	EXPECT_TRUE(text.find("\"catalog_append_nanoseconds\": 6") != std::string::npos);
	EXPECT_TRUE(text.find("\"catalog_menu_index_probes\": 7") != std::string::npos);
	EXPECT_TRUE(text.find("\"catalog_menu_equality_checks\": 8") != std::string::npos);
	EXPECT_TRUE(text.find("\"catalog_existing_menu_hits\": 9") != std::string::npos);
	EXPECT_TRUE(text.find("\"catalog_batch_menu_reuse_hits\": 10") != std::string::npos);
	EXPECT_TRUE(text.find("\"catalog_new_menus\": 11") != std::string::npos);
	EXPECT_TRUE(text.find("\"catalog_menu_id_collision_misses\": 12") != std::string::npos);
	EXPECT_TRUE(text.find("\"packed_public_state_compatibility_checks\": 45") != std::string::npos);
	EXPECT_TRUE(text.find("\"catalog_output_capacity_reuse_events\": 33") != std::string::npos);
	EXPECT_TRUE(text.find("\"catalog_output_capacity_reuse_bytes\": 34") != std::string::npos);
	EXPECT_TRUE(text.find("\"catalog_public_descriptor_copy_count\": 35") != std::string::npos);
	EXPECT_TRUE(text.find("\"catalog_public_descriptor_copy_bytes\": 36") != std::string::npos);
	EXPECT_TRUE(text.find("\"catalog_nested_descriptor_copy_count\": 37") != std::string::npos);
	EXPECT_TRUE(text.find("\"catalog_nested_descriptor_copy_bytes\": 38") != std::string::npos);
	EXPECT_TRUE(text.find("\"coordinator_allocation_count\": 333") != std::string::npos);
	EXPECT_TRUE(text.find("\"coordinator_allocation_bytes\": 444") != std::string::npos);
	EXPECT_TRUE(text.find("\"public_state_betting_board_validation_nanoseconds\": 13") != std::string::npos);
	EXPECT_TRUE(text.find("\"public_state_menu_validation_nanoseconds\": 14") != std::string::npos);
	EXPECT_TRUE(text.find("\"public_state_history_identity_validation_nanoseconds\": 15") != std::string::npos);
	EXPECT_TRUE(text.find("\"discovery_public_state_merge_nanoseconds\": 16") != std::string::npos);
	EXPECT_TRUE(text.find("\"discovery_row_merge_nanoseconds\": 17") != std::string::npos);
	EXPECT_TRUE(text.find("\"discovery_public_state_source_elements\": 18") != std::string::npos);
	EXPECT_TRUE(text.find("\"discovery_row_source_elements\": 19") != std::string::npos);
	EXPECT_TRUE(text.find("\"discovery_public_descriptor_copy_count\": 39") != std::string::npos);
	EXPECT_TRUE(text.find("\"discovery_public_descriptor_copy_bytes\": 40") != std::string::npos);
	EXPECT_TRUE(text.find("\"discovery_nested_descriptor_copy_count\": 41") != std::string::npos);
	EXPECT_TRUE(text.find("\"discovery_nested_descriptor_copy_bytes\": 42") != std::string::npos);
	EXPECT_TRUE(text.find("\"discovery_output_capacity_reuse_events\": 43") != std::string::npos);
	EXPECT_TRUE(text.find("\"discovery_output_capacity_reuse_bytes\": 44") != std::string::npos);
	EXPECT_TRUE(text.find("\"discovery_public_state_duplicate_candidates\": 20") != std::string::npos);
	EXPECT_TRUE(text.find("\"discovery_row_duplicate_candidates\": 21") != std::string::npos);
	EXPECT_TRUE(text.find("\"discovery_single_stream_batches\": 22") != std::string::npos);
	EXPECT_TRUE(text.find("\"discovery_one_stream_fast_path_uses\": 23") != std::string::npos);
	EXPECT_TRUE(text.find("\"parent_transition_fixed_state_nanoseconds\": 24") != std::string::npos);
	EXPECT_TRUE(text.find("\"parent_transition_action_edge_nanoseconds\": 25") != std::string::npos);
	EXPECT_TRUE(text.find("\"parent_transition_board_chance_edge_nanoseconds\": 26") != std::string::npos);
	EXPECT_TRUE(text.find("\"parent_transition_street_edge_nanoseconds\": 27") != std::string::npos);
	EXPECT_TRUE(text.find("\"parent_transition_action_edges\": 28") != std::string::npos);
	EXPECT_TRUE(text.find("\"parent_transition_board_chance_edges\": 29") != std::string::npos);
	EXPECT_TRUE(text.find("\"parent_transition_street_edges\": 30") != std::string::npos);
	EXPECT_TRUE(text.find("\"parent_transition_action_history_prefix_elements\": 31") != std::string::npos);
	EXPECT_TRUE(text.find("\"parent_transition_action_menu_probes\": 32") != std::string::npos);
	std::filesystem::remove(path);
}

TEST_CASE(multiway_export_telemetry_validation_rejects_bad_schema_and_success_fields)
{
	using namespace texas::solver::multiway;
	MultiwayExportTelemetry telemetry;
	telemetry.schema_version = MULTIWAY_EXPORT_TELEMETRY_SCHEMA_VERSION + 1U;
	EXPECT_THROW(telemetry.validate(), std::invalid_argument);
	telemetry.schema_version = MULTIWAY_EXPORT_TELEMETRY_SCHEMA_VERSION;
	telemetry.attempted = true;
	telemetry.succeeded = true;
	telemetry.plan.serialized_bytes = 10U;
	telemetry.process_gate.status = MultiwayExportGateStatus::Admitted;
	telemetry.result_bytes = 9U;
	EXPECT_THROW(telemetry.validate(), std::invalid_argument);
}

TEST_CASE(multiway_training_report_separates_tracy_provenance)
{
	using namespace texas::solver::multiway;
	MultiwayTrainingReport report;
	MultiwayProducerIdentity identity;
	identity.git_commit = "test";
	identity.build_configuration = "RelWithDebInfo";
	identity.compiler_identity = "test";
	identity.model_identity = make_multiway_model_identity(MultiwayBlueprintConfig{});
	identity.workflow_config_fingerprint = 1U;
	identity.artifact_schemas = { 4U, 2U, 1U };
	report.evidence = { MULTIWAY_TRAINING_REPORT_SCHEMA_VERSION,
		MultiwayWorkflowProfileKind::Acceptance, identity };
	report.instrumentation_kind = "tracy";
	report.tracy_version = "0.14.1";
	report.trace_capture_id = "capture-test";
	report.allocation_probe_valid = false;
	report.tracy_memory_events_enabled = true;
	report.validate();

	report.allocation_probe_valid = true;
	EXPECT_THROW(report.validate(), std::invalid_argument);
	report.allocation_probe_valid = false;
	report.instrumentation_kind = "none";
	report.tracy_version.clear();
	report.trace_capture_id.clear();
	EXPECT_THROW(report.validate(), std::invalid_argument);
}

TEST_CASE(multiway_training_report_calculates_throughput_and_rejects_bad_rss)
{
	using namespace texas::solver::multiway;
	MultiwayTrainingReport report;
	MultiwayProducerIdentity identity;
	identity.git_commit = "test";
	identity.build_configuration = "Debug";
	identity.compiler_identity = "test";
	identity.model_identity = make_multiway_model_identity(MultiwayBlueprintConfig{});
	identity.workflow_config_fingerprint = 1U;
	identity.artifact_schemas = { 4U, 2U, 1U };
	report.evidence = {
		MULTIWAY_TRAINING_REPORT_SCHEMA_VERSION,
		MultiwayWorkflowProfileKind::Acceptance,
		identity
	};
	report.trajectories = 100U;
	report.elapsed_wall_nanoseconds = 2'000'000'000U;
	EXPECT_NEAR(report.trajectories_per_second(), 50.0, 1e-12);
	report.current_process_rss_bytes = 8U;
	report.peak_process_rss_bytes = 7U;
	EXPECT_THROW(report.validate(), std::invalid_argument);
	report.process_rss_available = true;
	report.current_process_rss_bytes = 0U;
	report.peak_process_rss_bytes = 8U;
	EXPECT_THROW(report.validate(), std::invalid_argument);
	report.process_rss_available = false;
	report.admitted_rows_per_batch = { 4U, 3U };
	EXPECT_THROW(report.validate(), std::invalid_argument);
	report.admitted_rows_per_batch = { 4U, 6U };
	report.validate();
	report.process_memory_snapshots.enabled = true;
	report.process_memory_snapshots.count = 2U;
	report.process_memory_snapshots.snapshots[0U].phase_id = MultiwayMemoryPhase::ProcessStart;
	report.process_memory_snapshots.snapshots[1U].phase_id = MultiwayMemoryPhase::AfterBucketMapMetadata;
	EXPECT_THROW(report.validate(), std::invalid_argument);
}

TEST_CASE(multiway_training_report_rejects_impossible_counters)
{
	texas::solver::multiway::MultiwayTrainingReport report;
	report.trajectories = 2U;
	report.discarded_trajectories = 3U;
	EXPECT_THROW(report.validate(), std::invalid_argument);
	report.discarded_trajectories = 0U;
	report.batches = 1U;
	EXPECT_THROW(report.validate(), std::invalid_argument);
}

TEST_CASE(multiway_training_report_preserves_failed_partial_telemetry)
{
	using namespace texas::solver::multiway;
	MultiwayTrainingReport report;
	MultiwayProducerIdentity identity;
	identity.git_commit = "test";
	identity.build_configuration = "Debug";
	identity.compiler_identity = "test";
	identity.model_identity = make_multiway_model_identity(MultiwayBlueprintConfig{});
	identity.workflow_config_fingerprint = 1U;
	identity.artifact_schemas = { 4U, 2U, 1U };
	report.evidence = { MULTIWAY_TRAINING_REPORT_SCHEMA_VERSION,
		MultiwayWorkflowProfileKind::Acceptance, identity };
	report.batches = 1U;
	report.trajectories = 10U;
	report.failed = true;
	report.failed_batch_available = true;
	report.failed_batch.trajectories_attempted = 32U;
	report.failed_batch.trajectories_accepted = 6U;
	report.failed_batch.trajectories_discarded = 26U;
	report.failed_batch.delta_entries_merged = 87U;
	report.failed_batch.worker_trajectory_counts = { 32U };
	report.failed_batch.worker_active_nanoseconds_by_worker = { 100U };
	report.validate();
}
