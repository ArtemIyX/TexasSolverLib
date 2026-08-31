#include "solver/multiway/workflow/multiway_checkpoint_equivalence.hpp"
#include "test_harness.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <string>

namespace {

std::filesystem::path unique_path(const char* suffix) {
    return std::filesystem::temp_directory_path() /
        (std::string("texas_solver_equivalence_") + suffix + "_" +
         std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
}

texas::solver::multiway::MultiwayEquivalenceRunEvidence run_evidence() {
    using namespace texas::solver::multiway;
    MultiwayEquivalenceRunEvidence result;
    result.producer.git_commit = "test";
    result.producer.build_configuration = "Debug";
    result.producer.compiler_identity = "test";
    result.producer.model_identity = make_multiway_model_identity(MultiwayBlueprintConfig{});
    result.producer.workflow_config_fingerprint = 7U;
    result.producer.artifact_schemas = {4U, 2U, 1U};
    result.deterministic_seed = 1U;
    result.schedule_fingerprint = 2U;
    result.coverage_fingerprint = 3U;
    result.street_rows = {4U, 5U, 6U, 7U};
    result.terminal_visits = 8U;
    result.leaf_visits = 9U;
    result.merge_fingerprint = 10U;
    result.completed_trajectories = 11U;
    return result;
}

void write_bytes(const std::filesystem::path& path, const std::string& bytes) {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    output.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
}

texas::solver::multiway::MultiwayCheckpointEquivalenceRequest request_for(
    const std::filesystem::path& first,
    const std::filesystem::path& second) {
    texas::solver::multiway::MultiwayCheckpointEquivalenceRequest request;
    request.continuous_blueprint = first;
    request.resumed_blueprint = second;
    request.continuous = run_evidence();
    request.resumed = request.continuous;
    return request;
}

}  // namespace

TEST_CASE(multiway_checkpoint_equivalence_streams_identical_blueprints) {
    const auto first = unique_path("first.bin");
    const auto second = unique_path("second.bin");
    write_bytes(first, "deterministic-blueprint");
    write_bytes(second, "deterministic-blueprint");
    const auto report = texas::solver::multiway::compare_multiway_checkpoints(
        request_for(first, second));
    EXPECT_TRUE(report.passed());
    const auto output = unique_path("report.json");
    texas::solver::multiway::save_multiway_checkpoint_equivalence_atomic(output, report);
    std::ifstream input(output);
    const std::string text((std::istreambuf_iterator<char>(input)), {});
    input.close();
    EXPECT_TRUE(text.find("\"passed\": true") != std::string::npos);
    std::filesystem::remove(first);
    std::filesystem::remove(second);
    std::filesystem::remove(output);
}

TEST_CASE(multiway_checkpoint_equivalence_rejects_payload_difference) {
    const auto first = unique_path("difference_first.bin");
    const auto second = unique_path("difference_second.bin");
    write_bytes(first, "same-prefix-a");
    write_bytes(second, "same-prefix-b");
    const auto report = texas::solver::multiway::compare_multiway_checkpoints(
        request_for(first, second));
    EXPECT_TRUE(!report.passed());
    EXPECT_TRUE(!report.blueprint_bytes_equal);
    EXPECT_THROW(
        texas::solver::multiway::save_multiway_checkpoint_equivalence_atomic(
            unique_path("failed.json"), report),
        std::invalid_argument);
    std::filesystem::remove(first);
    std::filesystem::remove(second);
}

TEST_CASE(multiway_checkpoint_equivalence_rejects_metadata_difference) {
    const auto first = unique_path("metadata_first.bin");
    const auto second = unique_path("metadata_second.bin");
    write_bytes(first, "identical");
    write_bytes(second, "identical");
    auto request = request_for(first, second);
    request.resumed.coverage_fingerprint = 99U;
    const auto report = texas::solver::multiway::compare_multiway_checkpoints(request);
    EXPECT_TRUE(!report.passed());
    EXPECT_TRUE(!report.coverage_equal);
    std::filesystem::remove(first);
    std::filesystem::remove(second);
}

TEST_CASE(multiway_checkpoint_equivalence_rejects_truncated_input) {
    const auto first = unique_path("truncated_first.bin");
    const auto second = unique_path("truncated_second.bin");
    write_bytes(first, "complete");
    write_bytes(second, "");
    const auto report = texas::solver::multiway::compare_multiway_checkpoints(
        request_for(first, second));
    EXPECT_TRUE(!report.passed());
    EXPECT_EQ(report.resumed_blueprint_bytes, 0U);
    std::filesystem::remove(first);
    std::filesystem::remove(second);
}
