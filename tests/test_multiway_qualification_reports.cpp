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
	lookup.replay_fingerprint = 9U;
	lookup.second_replay_fingerprint = 9U;
	save_multiway_lookup_qualification_report_atomic(path, lookup);
	std::ifstream input(path);
	const std::string text((std::istreambuf_iterator<char>(input)), {});
	input.close();
	EXPECT_TRUE(text.find("\"passed\": true") != std::string::npos);
	EXPECT_TRUE(text.find("\"second_replay_fingerprint\": 9") != std::string::npos);
	std::filesystem::remove(path);
}

TEST_CASE(multiway_training_report_preserves_discard_telemetry)
{
	using namespace texas::solver::multiway;
	texas::solver::multiway::MultiwayBlueprintTrainingStatus status;
	status.trajectories = 20U;
	status.discarded_trajectories = 3U;
	status.preflop_rows = 1U;
	status.street_visits = { 10U, 20U, 30U, 40U };
	status.requested_worker_count = 16U;
	status.effective_worker_count = 8U;
	status.trajectories_per_batch = 20U;
	status.peak_worker_delta_entries = 7U;
	status.cumulative_worker_delta_entries = 55U;
	status.worker_active_nanoseconds = 100U;
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
	status.memory_preflight_estimate_bytes = 999U;
	status.memory_ledger.resident_baseline_bytes = 111U;
	status.memory_ledger.regret_page_bytes = 222U;
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
	auto report = make_multiway_training_report(status, 77U, evidence);
	report.checkpoint_bytes = 123U;
	report.checkpoint_write_nanoseconds = 456U;
	report.checkpoint_telemetry.public_catalog_export_nanoseconds = 12U;
	report.checkpoint_telemetry.public_state_bytes = 34U;
	report.blueprint_bytes = 789U;
	report.blueprint_export_nanoseconds = 1011U;
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
	report.admission_diagnostics.public_state_betting_board_validation_nanoseconds = 13U;
	report.admission_diagnostics.public_state_menu_validation_nanoseconds = 14U;
	report.admission_diagnostics.public_state_history_identity_validation_nanoseconds = 15U;
	report.admission_diagnostics.discovery_public_state_merge_nanoseconds = 16U;
	report.admission_diagnostics.discovery_row_merge_nanoseconds = 17U;
	report.admission_diagnostics.discovery_public_state_source_elements = 18U;
	report.admission_diagnostics.discovery_row_source_elements = 19U;
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
	EXPECT_EQ(report.peak_worker_delta_entries, 7U);
	EXPECT_EQ(report.cumulative_worker_delta_entries, 55U);
	EXPECT_EQ(report.minimum_worker_active_nanoseconds, 40U);
	EXPECT_EQ(report.maximum_worker_active_nanoseconds, 60U);
	EXPECT_EQ(report.barrier_wait_nanoseconds, 20U);
	EXPECT_EQ(report.worker_trajectory_counts[1U], 10U);
	EXPECT_EQ(report.worker_active_nanoseconds_by_worker[1U], 60U);
	EXPECT_EQ(report.instrumentation_kind, std::string("none"));
	EXPECT_TRUE(report.tracy_version.empty());
	EXPECT_TRUE(report.trace_capture_id.empty());
	EXPECT_TRUE(report.allocation_probe_valid);
	const auto path = std::filesystem::temp_directory_path() / "texas_solver_training_report_telemetry_test.json";
	save_multiway_training_report_atomic(path, report);
	std::ifstream input(path);
	const std::string text((std::istreambuf_iterator<char>(input)), {});
	input.close();
	EXPECT_TRUE(text.find("\"checkpoint_bytes\": 123") != std::string::npos);
	EXPECT_TRUE(text.find("\"public_catalog_export_nanoseconds\": 12") != std::string::npos);
	EXPECT_TRUE(text.find("\"public_state_bytes\": 34") != std::string::npos);
	EXPECT_TRUE(text.find("\"blueprint_bytes\": 789") != std::string::npos);
	EXPECT_TRUE(text.find("\"requested_worker_count\": 16") != std::string::npos);
	EXPECT_TRUE(text.find("\"memory_preflight_estimate_bytes\": 999") != std::string::npos);
	EXPECT_TRUE(text.find("\"regret_page_bytes\": 222") != std::string::npos);
	EXPECT_TRUE(text.find("\"barrier_wait_nanoseconds\": 20") != std::string::npos);
	EXPECT_TRUE(text.find("\"worker_trajectory_counts\": [10,10,0,0,0,0,0,0]") != std::string::npos);
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
	EXPECT_TRUE(text.find("\"public_state_betting_board_validation_nanoseconds\": 13") != std::string::npos);
	EXPECT_TRUE(text.find("\"public_state_menu_validation_nanoseconds\": 14") != std::string::npos);
	EXPECT_TRUE(text.find("\"public_state_history_identity_validation_nanoseconds\": 15") != std::string::npos);
	EXPECT_TRUE(text.find("\"discovery_public_state_merge_nanoseconds\": 16") != std::string::npos);
	EXPECT_TRUE(text.find("\"discovery_row_merge_nanoseconds\": 17") != std::string::npos);
	EXPECT_TRUE(text.find("\"discovery_public_state_source_elements\": 18") != std::string::npos);
	EXPECT_TRUE(text.find("\"discovery_row_source_elements\": 19") != std::string::npos);
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
	report.validate();

	report.allocation_probe_valid = true;
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
