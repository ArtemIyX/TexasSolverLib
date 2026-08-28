#include "solver/multiway/workflow/multiway_lookup_qualification.hpp"
#include "solver/multiway/workflow/multiway_training_report.hpp"
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
    lookup.lookup_hits = 4U;
    lookup.replay_fingerprint = 9U;
    save_multiway_lookup_qualification_report_atomic(path, lookup);
    std::ifstream input(path);
    const std::string text((std::istreambuf_iterator<char>(input)), {});
    input.close();
    EXPECT_TRUE(text.find("\"passed\": true") != std::string::npos);
    std::filesystem::remove(path);
}

TEST_CASE(multiway_training_report_preserves_discard_telemetry) {
    texas::solver::multiway::MultiwayBlueprintTrainingStatus status;
    status.trajectories = 20U;
    status.discarded_trajectories = 3U;
    status.preflop_rows = 1U;
    const auto report = texas::solver::multiway::make_multiway_training_report(status, 77U);
    EXPECT_EQ(report.discarded_trajectories, 3U);
    EXPECT_EQ(report.preflop_rows, 1U);
    EXPECT_EQ(report.deterministic_merge_fingerprint, 77U);
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
