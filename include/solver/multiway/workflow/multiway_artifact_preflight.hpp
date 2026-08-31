#pragma once

#include "solver/multiway/abstraction/multiway_model_identity.hpp"

#include <cstdint>
#include <filesystem>

namespace texas::solver::multiway {

struct MultiwayArtifactPreflightRequest {
    std::filesystem::path destination;
    std::filesystem::path temporary;
    // Empty means that no resumable temporary state is requested.
    std::filesystem::path progress_sidecar;
    MultiwayModelIdentity identity{};
    std::uint64_t expected_table_count = 0U;
    std::uint64_t estimated_final_bytes = 0U;
    std::uint64_t required_free_bytes = 0U;
    std::uint64_t estimated_process_memory_bytes = 0U;
    std::uint64_t process_memory_limit_bytes = 0U;
};

// Cold-path checks performed before bucket generation or publication. Throws
// on an unsafe or internally inconsistent request.
void preflight_multiway_artifact(const MultiwayArtifactPreflightRequest& request);

}  // namespace texas::solver::multiway
