#include "solver/multiway/workflow/multiway_lookup_qualification.hpp"
#include "solver/multiway/workflow/multiway_training_report.hpp"
#include "solver/multiway/workflow/multiway_evidence.hpp"
#include "test_harness.hpp"

#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

TEST_CASE(multiway_lookup_qualification_gate_requires_zero_misses) {
    texas::solver::multiway::MultiwayLookupQualificationReport report;
    EXPECT_TRUE(report.passed());
    report.missing_buckets = 1U;
    EXPECT_TRUE(!report.passed());
}

TEST_CASE(multiway_qualification_reports_publish_atomically) {
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
        identity.artifact_schemas = {4U, 2U, 1U};
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

TEST_CASE(multiway_training_report_preserves_discard_telemetry) {
    using namespace texas::solver::multiway;
    texas::solver::multiway::MultiwayBlueprintTrainingStatus status;
    status.trajectories = 20U;
    status.discarded_trajectories = 3U;
    status.preflop_rows = 1U;
    status.street_visits = {10U, 20U, 30U, 40U};
    MultiwayProducerIdentity identity;
    identity.git_commit = "test";
    identity.build_configuration = "Debug";
    identity.compiler_identity = "test";
    identity.model_identity = make_multiway_model_identity(MultiwayBlueprintConfig{});
    identity.workflow_config_fingerprint = 1U;
    identity.artifact_schemas = {4U, 2U, 1U};
    MultiwayEvidenceHeader evidence{
        MULTIWAY_TRAINING_REPORT_SCHEMA_VERSION,
        MultiwayWorkflowProfileKind::Acceptance,
        identity};
    auto report = make_multiway_training_report(status, 77U, evidence);
    report.checkpoint_bytes = 123U;
    report.checkpoint_write_nanoseconds = 456U;
    report.blueprint_bytes = 789U;
    report.blueprint_export_nanoseconds = 1011U;
    EXPECT_EQ(report.discarded_trajectories, 3U);
    EXPECT_EQ(report.accepted_trajectories, 17U);
    EXPECT_EQ(report.preflop_rows, 1U);
    EXPECT_EQ(report.deterministic_merge_fingerprint, 77U);
    EXPECT_EQ(report.street_visits[2U], 30U);
    const auto path = std::filesystem::temp_directory_path() /
        "texas_solver_training_report_telemetry_test.json";
    save_multiway_training_report_atomic(path, report);
    std::ifstream input(path);
    const std::string text((std::istreambuf_iterator<char>(input)), {});
    input.close();
    EXPECT_TRUE(text.find("\"checkpoint_bytes\": 123") != std::string::npos);
    EXPECT_TRUE(text.find("\"blueprint_bytes\": 789") != std::string::npos);
    std::filesystem::remove(path);
}

TEST_CASE(multiway_training_report_calculates_throughput_and_rejects_bad_rss) {
    using namespace texas::solver::multiway;
    MultiwayTrainingReport report;
    MultiwayProducerIdentity identity;
    identity.git_commit = "test";
    identity.build_configuration = "Debug";
    identity.compiler_identity = "test";
    identity.model_identity = make_multiway_model_identity(MultiwayBlueprintConfig{});
    identity.workflow_config_fingerprint = 1U;
    identity.artifact_schemas = {4U, 2U, 1U};
    report.evidence = {
        MULTIWAY_TRAINING_REPORT_SCHEMA_VERSION,
        MultiwayWorkflowProfileKind::Acceptance,
        identity};
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
    report.admitted_rows_per_batch = {4U, 3U};
    EXPECT_THROW(report.validate(), std::invalid_argument);
    report.admitted_rows_per_batch = {4U, 6U};
    report.validate();
}

TEST_CASE(multiway_training_report_rejects_impossible_counters) {
    texas::solver::multiway::MultiwayTrainingReport report;
    report.trajectories = 2U;
    report.discarded_trajectories = 3U;
    EXPECT_THROW(report.validate(), std::invalid_argument);
    report.discarded_trajectories = 0U;
    report.batches = 1U;
    EXPECT_THROW(report.validate(), std::invalid_argument);
}

TEST_CASE(multiway_training_report_preserves_failed_partial_telemetry) {
    using namespace texas::solver::multiway;
    MultiwayTrainingReport report;
    MultiwayProducerIdentity identity;
    identity.git_commit = "test";
    identity.build_configuration = "Debug";
    identity.compiler_identity = "test";
    identity.model_identity = make_multiway_model_identity(MultiwayBlueprintConfig{});
    identity.workflow_config_fingerprint = 1U;
    identity.artifact_schemas = {4U, 2U, 1U};
    report.evidence = {MULTIWAY_TRAINING_REPORT_SCHEMA_VERSION,
        MultiwayWorkflowProfileKind::Acceptance, identity};
    report.batches = 1U;
    report.trajectories = 10U;
    report.failed = true;
    report.validate();
}
