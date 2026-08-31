#include "solver/multiway/workflow/multiway_artifact_preflight.hpp"
#include "solver/multiway/abstraction/multiway_bucket_artifact_writer.hpp"

#include <stdexcept>

namespace texas::solver::multiway {

void preflight_multiway_artifact(const MultiwayArtifactPreflightRequest& request) {
    request.identity.validate();
    if (request.destination.empty() || request.temporary.empty() ||
        request.expected_table_count == 0U ||
        request.estimated_final_bytes == 0U || request.required_free_bytes == 0U) {
        throw std::invalid_argument("multiway artifact preflight request is incomplete");
    }
    if (request.process_memory_limit_bytes != 0U &&
        request.estimated_process_memory_bytes > request.process_memory_limit_bytes) {
        throw std::length_error("multiway artifact process memory estimate exceeds limit");
    }

    std::error_code error;
    const auto parent = request.destination.parent_path().empty()
        ? std::filesystem::current_path(error) : request.destination.parent_path();
    if (error || !std::filesystem::exists(parent, error) || error ||
        !std::filesystem::is_directory(parent, error) || error) {
        throw std::runtime_error("multiway artifact output directory is unavailable");
    }
    const auto available = std::filesystem::space(parent, error).available;
    if (error || available < request.required_free_bytes || available < request.estimated_final_bytes) {
        throw std::length_error("multiway artifact output disk space is insufficient");
    }

    auto manifest = request.destination;
    manifest += ".manifest";
    auto extension_manifest = request.destination;
    extension_manifest.replace_extension(".manifest");
    if (std::filesystem::exists(manifest, error) || error ||
        std::filesystem::exists(extension_manifest, error) || error) {
        throw std::invalid_argument(
            "refusing to overwrite a verified multiway artifact");
    }

    const bool temporary_exists = std::filesystem::exists(request.temporary, error);
    if (error) throw std::runtime_error("cannot inspect multiway artifact temporary file");
    const bool sidecar_exists = !request.progress_sidecar.empty() &&
        std::filesystem::exists(request.progress_sidecar, error);
    if (error) throw std::runtime_error("cannot inspect multiway artifact progress sidecar");
    if (temporary_exists != sidecar_exists) {
        throw std::invalid_argument("multiway artifact temporary and sidecar state mismatch");
    }
    if (temporary_exists && sidecar_exists) {
        const auto progress = load_multiway_bucket_progress(
            request.progress_sidecar, request.identity, request.expected_table_count);
        const auto temporary_bytes = std::filesystem::file_size(request.temporary, error);
        if (error || temporary_bytes < progress.byte_length || progress.byte_length == 0U) {
            throw std::invalid_argument("multiway artifact resume state is inconsistent");
        }
    }
}

}  // namespace texas::solver::multiway
