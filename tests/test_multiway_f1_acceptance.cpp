#include "solver/multiway/workflow/multiway_f1_acceptance.hpp"
#include "solver/multiway/workflow/multiway_lookup_qualification.hpp"
#include "solver/multiway/workflow/multiway_training_report.hpp"
#include "test_harness.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <string>

namespace {

std::filesystem::path report_path();

void write_bytes(const std::filesystem::path& path, const std::string& bytes) {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    output.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
}

texas::solver::multiway::MultiwayProducerIdentity producer() {
    using namespace texas::solver::multiway;
    MultiwayProducerIdentity value;
    value.git_commit = "test";
    value.build_configuration = "Debug";
    value.compiler_identity = "test";
    value.model_identity = make_multiway_model_identity(MultiwayBlueprintConfig{});
    value.workflow_config_fingerprint = 7U;
    value.artifact_schemas = {4U, 2U, 1U};
    return value;
}

texas::solver::multiway::MultiwayF1AcceptanceEvidence valid_evidence() {
    using namespace texas::solver::multiway;
    const auto identity = producer();
    MultiwayF1AcceptanceEvidence evidence;
    evidence.header = {MULTIWAY_ACCEPTANCE_REPORT_SCHEMA_VERSION,
        MultiwayWorkflowProfileKind::Acceptance, identity};
    evidence.bucket.header = {MULTIWAY_BUCKET_INSPECTION_REPORT_SCHEMA_VERSION,
        MultiwayWorkflowProfileKind::Acceptance, identity};
    evidence.bucket.identity_matches = true;
    evidence.bucket.payload_hash_matches = true;
    evidence.bucket.flop_tables = 22100U;
    evidence.bucket.turn_tables = 270725U;
    evidence.bucket.river_tables = 2598960U;
    evidence.training.header = {MULTIWAY_TRAINING_REPORT_SCHEMA_VERSION,
        MultiwayWorkflowProfileKind::Acceptance, identity};
    evidence.training.started_at_preflop = true;
    evidence.training.preflop_rows = 1U;
    evidence.training.flop_rows = 1U;
    evidence.training.turn_rows = 1U;
    evidence.training.river_rows = 1U;
    evidence.training.terminal_visits = 1U;
    evidence.training.accepted_trajectories = 50'000'000U;
    evidence.training.process_memory_limit_bytes = 100U;
    evidence.training.peak_rss_available = true;
    evidence.training.peak_rss_bytes = 99U;
    evidence.resume.header = {MULTIWAY_CHECKPOINT_EQUIVALENCE_SCHEMA_VERSION,
        MultiwayWorkflowProfileKind::Acceptance, identity};
    evidence.resume.blueprint_payload_equal = true;
    evidence.resume.coverage_equal = true;
    evidence.resume.merge_fingerprint_equal = true;
    evidence.lookup.header = {MULTIWAY_LOOKUP_QUALIFICATION_SCHEMA_VERSION,
        MultiwayWorkflowProfileKind::Acceptance, identity};
    evidence.lookup.first_replay_fingerprint = 11U;
    evidence.lookup.second_replay_fingerprint = 11U;
    evidence.human_run = {"operator", "2026-08-31T00:00:00Z", "2026-08-31T00:01:00Z",
        "qualification --config \"configs/f1_dev_v1.cfg\"", "machine.json", 0};
    return evidence;
}

void write_bucket_report(const std::filesystem::path& path,
                         const texas::solver::multiway::MultiwayProducerIdentity& identity) {
    using namespace texas::solver::multiway;
    const auto header = MultiwayEvidenceHeader{
        MULTIWAY_BUCKET_INSPECTION_REPORT_SCHEMA_VERSION,
        MultiwayWorkflowProfileKind::Acceptance,
        identity};
    std::ofstream output(path);
    output << "{\n  \"identity\": " << identity.model_identity.combined_hash
           << ",\n  \"evidence\": " << serialize_multiway_evidence_header(header)
           << ",\n  \"flop_tables\": 22100"
           << ",\n  \"turn_tables\": 270725"
           << ",\n  \"river_tables\": 2598960"
           << ",\n  \"identity_matches\": true"
           << ",\n  \"payload_hash_matches\": true\n}\n";
}

void write_persisted_reports(const texas::solver::multiway::MultiwayF1AcceptanceInputPaths& paths,
                             bool mismatch) {
    using namespace texas::solver::multiway;
    const auto identity = producer();
    write_bucket_report(paths.bucket_inspection_report, identity);

    MultiwayTrainingReport training;
    training.evidence = {MULTIWAY_TRAINING_REPORT_SCHEMA_VERSION,
        MultiwayWorkflowProfileKind::Acceptance, identity};
    training.batches = 1U;
    training.trajectories = 50'000'000U;
    training.accepted_trajectories = 50'000'000U;
    training.preflop_rows = 1U;
    training.flop_rows = 1U;
    training.turn_rows = 1U;
    training.river_rows = 1U;
    training.terminal_visits = 1U;
    training.deterministic_merge_fingerprint = 1U;
    training.current_process_rss_bytes = 99U;
    training.peak_process_rss_bytes = 99U;
    training.process_memory_limit_bytes = 100U;
    training.process_rss_available = true;
    training.started_at_preflop = true;
    save_multiway_training_report_atomic(paths.training_report, training);

    const auto first = report_path().string() + ".first.bin";
    const auto second = report_path().string() + ".second.bin";
    write_bytes(first, "same-blueprint");
    write_bytes(second, "same-blueprint");
    MultiwayCheckpointEquivalenceRequest request;
    request.continuous_blueprint = first;
    request.resumed_blueprint = second;
    request.continuous.producer = identity;
    request.resumed.producer = identity;
    request.continuous.deterministic_seed = 1U;
    request.resumed.deterministic_seed = 1U;
    request.continuous.schedule_fingerprint = 2U;
    request.resumed.schedule_fingerprint = 2U;
    request.continuous.coverage_fingerprint = 3U;
    request.resumed.coverage_fingerprint = 3U;
    request.continuous.merge_fingerprint = 4U;
    request.resumed.merge_fingerprint = 4U;
    const auto equivalence = compare_multiway_checkpoints(request);
    save_multiway_checkpoint_equivalence_atomic(paths.checkpoint_equivalence_report, equivalence);
    std::filesystem::remove(first);
    std::filesystem::remove(second);

    MultiwayLookupQualificationReport lookup;
    auto lookup_identity = identity;
    lookup.evidence = {MULTIWAY_LOOKUP_QUALIFICATION_SCHEMA_VERSION,
        MultiwayWorkflowProfileKind::Acceptance, lookup_identity};
    lookup.lookup_hits = 1U;
    lookup.replay_fingerprint = 11U;
    save_multiway_lookup_qualification_report_atomic(paths.first_lookup_report, lookup);
    if (mismatch) lookup.evidence.producer.workflow_config_fingerprint = 8U;
    save_multiway_lookup_qualification_report_atomic(paths.second_lookup_report, lookup);
}

texas::solver::multiway::MultiwayF1AcceptanceInputPaths persisted_paths(bool mismatch = false) {
    using namespace texas::solver::multiway;
    const auto base = report_path();
    MultiwayF1AcceptanceInputPaths paths;
    paths.bucket_inspection_report = base.string() + ".bucket";
    paths.training_report = base.string() + ".training";
    paths.checkpoint_equivalence_report = base.string() + ".equivalence";
    paths.first_lookup_report = base.string() + ".lookup1";
    paths.second_lookup_report = base.string() + ".lookup2";
    paths.human_run = {"operator", "2026-08-31T00:00:00Z", "2026-08-31T00:01:00Z",
        "qualification command", "machine.json", 0};
    write_persisted_reports(paths, mismatch);
    return paths;
}

void remove_persisted_paths(const texas::solver::multiway::MultiwayF1AcceptanceInputPaths& paths) {
    std::filesystem::remove(paths.bucket_inspection_report);
    std::filesystem::remove(paths.training_report);
    std::filesystem::remove(paths.checkpoint_equivalence_report);
    std::filesystem::remove(paths.first_lookup_report);
    std::filesystem::remove(paths.second_lookup_report);
}

std::filesystem::path report_path() {
    return std::filesystem::temp_directory_path() /
        (std::string("texas_solver_f1_acceptance_") +
         std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + ".json");
}

}  // namespace

TEST_CASE(multiway_f1_acceptance_positive_fixture_passes_and_publishes) {
    using namespace texas::solver::multiway;
    auto report = evaluate_multiway_f1_acceptance(valid_evidence());
    report.source_reports = {"bucket.json", "training.json", "equivalence.json",
        "lookup.first.json", "lookup.second.json"};
    EXPECT_TRUE(report.passed);
    const auto path = report_path();
    save_multiway_f1_acceptance_atomic(path, report);
    std::ifstream input(path);
    const std::string text((std::istreambuf_iterator<char>(input)), {});
    input.close();
    EXPECT_TRUE(text.find("\"passed\": true") != std::string::npos);
    EXPECT_TRUE(text.find("\"evidence\":") != std::string::npos);
    EXPECT_TRUE(text.find("\\\"configs/f1_dev_v1.cfg\\\"") != std::string::npos);
    std::filesystem::remove(path);
}

TEST_CASE(multiway_f1_acceptance_rejects_bucket_predicate_failure) {
    auto evidence = valid_evidence();
    evidence.bucket.river_tables = 1U;
    EXPECT_THROW(
        texas::solver::multiway::evaluate_multiway_f1_acceptance(evidence),
        std::invalid_argument);
}

TEST_CASE(multiway_f1_acceptance_rejects_training_predicate_failure) {
    auto evidence = valid_evidence();
    evidence.training.discarded_trajectories = 1U;
    EXPECT_THROW(
        texas::solver::multiway::evaluate_multiway_f1_acceptance(evidence),
        std::invalid_argument);
}

TEST_CASE(multiway_f1_acceptance_rejects_resume_and_lookup_failures) {
    auto evidence = valid_evidence();
    evidence.resume.coverage_equal = false;
    EXPECT_THROW(
        texas::solver::multiway::evaluate_multiway_f1_acceptance(evidence),
        std::invalid_argument);
    evidence = valid_evidence();
    evidence.lookup.second_replay_fingerprint = 12U;
    EXPECT_THROW(
        texas::solver::multiway::evaluate_multiway_f1_acceptance(evidence),
        std::invalid_argument);
}

TEST_CASE(multiway_f1_acceptance_rejects_sizing_and_mismatched_provenance) {
    using namespace texas::solver::multiway;
    auto evidence = valid_evidence();
    evidence.training.header.profile_kind = MultiwayWorkflowProfileKind::Sizing;
    EXPECT_THROW(evaluate_multiway_f1_acceptance(evidence), std::invalid_argument);
    evidence = valid_evidence();
    evidence.lookup.header.producer.workflow_config_fingerprint = 8U;
    EXPECT_THROW(evaluate_multiway_f1_acceptance(evidence), std::invalid_argument);
}

TEST_CASE(multiway_f1_acceptance_does_not_publish_failed_report) {
    using namespace texas::solver::multiway;
    MultiwayF1AcceptanceReport report;
    const auto path = report_path();
    EXPECT_THROW(save_multiway_f1_acceptance_atomic(path, report), std::invalid_argument);
    EXPECT_TRUE(!std::filesystem::exists(path));
}

TEST_CASE(multiway_f1_acceptance_rejects_pass_without_evidence) {
    using namespace texas::solver::multiway;
    MultiwayF1AcceptanceReport report;
    report.passed = true;
    report.human_run = {"operator", "start", "end", "command", "machine.json", 0};
    const auto path = report_path();
    EXPECT_THROW(save_multiway_f1_acceptance_atomic(path, report), std::invalid_argument);
    EXPECT_TRUE(!std::filesystem::exists(path));
}

TEST_CASE(multiway_f1_acceptance_rejects_pass_without_source_reports) {
    using namespace texas::solver::multiway;
    const auto report = evaluate_multiway_f1_acceptance(valid_evidence());
    const auto path = report_path();
    EXPECT_THROW(save_multiway_f1_acceptance_atomic(path, report), std::invalid_argument);
    EXPECT_TRUE(!std::filesystem::exists(path));
}

TEST_CASE(multiway_f1_acceptance_loads_persisted_reports_and_publishes) {
    using namespace texas::solver::multiway;
    const auto paths = persisted_paths();
    const auto report = finalize_multiway_f1_acceptance(paths);
    EXPECT_TRUE(report.passed);
    const auto output = report_path();
    finalize_multiway_f1_acceptance_atomic(output, paths);
    EXPECT_TRUE(std::filesystem::exists(output));
    std::ifstream input(output);
    const std::string text((std::istreambuf_iterator<char>(input)), {});
    input.close();
    const auto training_report_name = paths.training_report.filename().string();
    EXPECT_TRUE(text.find(training_report_name) != std::string::npos);
    EXPECT_TRUE(text.find("source_reports") != std::string::npos);
    std::filesystem::remove(output);
    remove_persisted_paths(paths);
}

TEST_CASE(multiway_f1_acceptance_rejects_missing_persisted_report) {
    using namespace texas::solver::multiway;
    auto paths = persisted_paths();
    std::filesystem::remove(paths.training_report);
    EXPECT_THROW(finalize_multiway_f1_acceptance(paths), std::runtime_error);
    remove_persisted_paths(paths);
}

TEST_CASE(multiway_f1_acceptance_rejects_persisted_provenance_mismatch) {
    using namespace texas::solver::multiway;
    const auto paths = persisted_paths(true);
    EXPECT_THROW(finalize_multiway_f1_acceptance(paths), std::invalid_argument);
    remove_persisted_paths(paths);
}
