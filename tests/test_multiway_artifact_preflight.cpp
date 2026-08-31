#include "solver/multiway/workflow/multiway_artifact_preflight.hpp"
#include "test_harness.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>

namespace {

std::filesystem::path unique_path(const char* name) {
    return std::filesystem::temp_directory_path() /
        (std::string("texas_solver_preflight_") + name + "_" +
         std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
}

texas::solver::multiway::MultiwayArtifactPreflightRequest valid_request() {
    using namespace texas::solver::multiway;
    MultiwayArtifactPreflightRequest request;
    const auto base = unique_path("valid");
    request.destination = base / "buckets.bin";
    request.temporary = base / "buckets.tmp";
    request.progress_sidecar = base / "buckets.progress";
    request.identity = make_multiway_model_identity(MultiwayBlueprintConfig{});
    request.expected_table_count = 1U;
    request.estimated_final_bytes = 1U;
    request.required_free_bytes = 1U;
    return request;
}

}  // namespace

TEST_CASE(multiway_artifact_preflight_accepts_empty_new_output) {
    auto request = valid_request();
    std::filesystem::create_directories(request.destination.parent_path());
    texas::solver::multiway::preflight_multiway_artifact(request);
    std::filesystem::remove_all(request.destination.parent_path());
}

TEST_CASE(multiway_artifact_preflight_rejects_verified_overwrite) {
    auto request = valid_request();
    std::filesystem::create_directories(request.destination.parent_path());
    std::ofstream manifest(request.destination.string() + ".manifest");
    manifest << "verified";
    manifest.close();
    EXPECT_THROW(
        texas::solver::multiway::preflight_multiway_artifact(request),
        std::invalid_argument);
    std::filesystem::remove_all(request.destination.parent_path());
}

TEST_CASE(multiway_artifact_preflight_rejects_partial_resume_state) {
    auto request = valid_request();
    std::filesystem::create_directories(request.destination.parent_path());
    std::ofstream temporary(request.temporary);
    temporary << "partial";
    temporary.close();
    EXPECT_THROW(
        texas::solver::multiway::preflight_multiway_artifact(request),
        std::invalid_argument);
    std::filesystem::remove_all(request.destination.parent_path());
}

TEST_CASE(multiway_artifact_preflight_rejects_invalid_resume_sidecar) {
    auto request = valid_request();
    std::filesystem::create_directories(request.destination.parent_path());
    std::ofstream temporary(request.temporary);
    temporary << "partial";
    temporary.close();
    std::ofstream sidecar(request.progress_sidecar);
    sidecar << "invalid";
    sidecar.close();
    EXPECT_THROW(
        texas::solver::multiway::preflight_multiway_artifact(request),
        std::invalid_argument);
    std::filesystem::remove_all(request.destination.parent_path());
}

TEST_CASE(multiway_artifact_preflight_rejects_memory_overcommit) {
    auto request = valid_request();
    request.estimated_process_memory_bytes = 11U;
    request.process_memory_limit_bytes = 10U;
    EXPECT_THROW(
        texas::solver::multiway::preflight_multiway_artifact(request),
        std::length_error);
}
