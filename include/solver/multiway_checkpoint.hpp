#pragma once

#include "core/namespaces.hpp"

#include "solver/multiway_export.hpp"

#include <filesystem>

namespace texas::solver::multiway {

class MultiwayCheckpoint {
public:
    static void save_atomic(
        const std::filesystem::path& path,
        const MultiwayBlueprintSnapshot& snapshot);
    [[nodiscard]] static MultiwayBlueprintSnapshot load(const std::filesystem::path& path);
    [[nodiscard]] static MultiwayBlueprintSnapshot load_for_resume(
        const std::filesystem::path& path,
        const MultiwayModelIdentity& expected_identity);
    static void validate_resume_identity(
        const MultiwayBlueprintSnapshot& snapshot,
        const MultiwayModelIdentity& expected_identity);
};

}  // namespace texas::solver::multiway
