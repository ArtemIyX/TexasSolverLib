#include "solver/multiway/workflow/multiway_sizing_report.hpp"

#include "test_harness.hpp"

#include <filesystem>
#include <fstream>
#include <string>

namespace {

texas::solver::multiway::MultiwayWorkflowConfig sizing_workflow() {
    using namespace texas::solver::multiway;
    return parse_multiway_workflow_config(
        "schema_version=1\nprofile_kind=sizing\nprofile_id=F1-SIZING-12-v1\n"
        "players=6\ninitial_stack_chips=10000\nsmall_blind_chips=50\n"
        "big_blind_chips=100\nante_chips=0\nrake=0\npreflop_classes=169\n"
        "flop_buckets=12\nturn_buckets=12\nriver_buckets=12\n"
        "storage_backend=CompactInt32\nmax_decision_depth=64\n"
        "max_public_chance_depth=3\ndeterministic_seed=1\n"
        "reference_worker_count=1\ntarget_trajectories=100000\n"
        "maximum_public_states=100000\nmaximum_sparse_rows=500000\n"
        "maximum_sparse_values=2000000\nworker_delta_capacity=100000\n"
        "trajectories_per_batch=1000\ncheckpoint_interval=10000\n"
        "disk_space_requirement_bytes=1073741824\n"
        "process_memory_limit_bytes=4294967296\n");
}

texas::solver::multiway::MultiwayTrainingReport training_fixture(
    const texas::solver::multiway::MultiwayWorkflowConfig& workflow) {
    using namespace texas::solver::multiway;
    MultiwayTrainingReport report;
    report.evidence.schema_version = MULTIWAY_TRAINING_REPORT_SCHEMA_VERSION;
    report.evidence.profile_kind = MultiwayWorkflowProfileKind::Sizing;
    report.evidence.producer.model_identity = make_multiway_model_identity(workflow.model);
    report.evidence.producer.workflow_config_fingerprint = workflow.fingerprint();
    report.evidence.producer.git_commit = "fixture-commit";
    report.evidence.producer.build_configuration = "Debug";
    report.evidence.producer.compiler_identity = "fixture-compiler";
    report.evidence.producer.artifact_schemas = {4U, 2U, 1U};
    report.trajectories = workflow.target_trajectories;
    report.accepted_trajectories = workflow.target_trajectories;
    report.preflop_rows = 1U;
    report.flop_rows = 1U;
    report.turn_rows = 1U;
    report.river_rows = 1U;
    report.terminal_visits = 1U;
    report.deterministic_merge_fingerprint = 1U;
    report.peak_compact_storage_bytes = 100U;
    report.current_process_rss_bytes = 100U;
    report.peak_process_rss_bytes = 200U;
    report.process_rss_available = true;
    return report;
}

}  // namespace

TEST_CASE(multiway_sizing_report_records_a_successful_pilot) {
    const auto workflow = sizing_workflow();
    const auto report = texas::solver::multiway::make_multiway_sizing_report(
        workflow, training_fixture(workflow));
    EXPECT_TRUE(report.target_reached);
    EXPECT_TRUE(report.passed);
    EXPECT_EQ(report.accepted_trajectories, workflow.target_trajectories);
}

TEST_CASE(multiway_sizing_report_preserves_capacity_failure_as_a_failure_report) {
    using namespace texas::solver::multiway;
    const auto workflow = sizing_workflow();
    auto training = training_fixture(workflow);
    training.trajectories = 10U;
    training.accepted_trajectories = 10U;
    training.capacity_exhaustion_stage = MultiwayTrainingCapacityStage::SparseRows;
    const auto report = make_multiway_sizing_report(workflow, training);
    EXPECT_TRUE(!report.target_reached);
    EXPECT_TRUE(!report.passed);
}

TEST_CASE(multiway_sizing_report_rejects_rss_over_the_declared_limit) {
    using namespace texas::solver::multiway;
    const auto workflow = sizing_workflow();
    auto training = training_fixture(workflow);
    training.peak_process_rss_bytes = workflow.process_memory_limit_bytes + 1U;
    const auto report = make_multiway_sizing_report(workflow, training);
    EXPECT_TRUE(!report.passed);
}

TEST_CASE(multiway_sizing_report_rejects_acceptance_workflow) {
    using namespace texas::solver::multiway;
    auto workflow = sizing_workflow();
    workflow.profile_kind = MultiwayWorkflowProfileKind::Acceptance;
    auto training = training_fixture(sizing_workflow());
    EXPECT_THROW(make_multiway_sizing_report(workflow, training), std::invalid_argument);
}

TEST_CASE(multiway_sizing_report_publishes_atomically) {
    using namespace texas::solver::multiway;
    const auto workflow = sizing_workflow();
    const auto report = make_multiway_sizing_report(workflow, training_fixture(workflow));
    const auto path = std::filesystem::temp_directory_path() / "multiway_sizing_report.json";
    save_multiway_sizing_report_atomic(path, report);
    std::string contents;
    {
        std::ifstream input(path);
        contents.assign((std::istreambuf_iterator<char>(input)), {});
    }
    EXPECT_TRUE(contents.find("\"passed\": true") != std::string::npos);
    std::filesystem::remove(path);
}
