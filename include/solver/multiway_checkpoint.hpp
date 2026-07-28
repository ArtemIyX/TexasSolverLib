#pragma once

#include "solver/multiway_export.hpp"

#include <filesystem>

namespace core {

class MultiwayCheckpoint {
public:
    static void save_atomic(
        const std::filesystem::path& path,
        const MultiwayBlueprintSnapshot& snapshot);
    [[nodiscard]] static MultiwayBlueprintSnapshot load(const std::filesystem::path& path);
};

}  // namespace core
