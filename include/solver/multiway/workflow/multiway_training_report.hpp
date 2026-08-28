#pragma once

#include "solver/multiway/blueprint/multiway_blueprint_trainer.hpp"

#include <filesystem>

namespace texas::solver::multiway {

struct MultiwayTrainingReport {
    std::uint64_t batches = 0U;
    std::uint64_t trajectories = 0U;
    std::uint64_t preflop_rows = 0U;
    std::uint64_t flop_rows = 0U;
    std::uint64_t turn_rows = 0U;
    std::uint64_t river_rows = 0U;
    std::uint64_t terminal_visits = 0U;
    std::uint64_t leaf_visits = 0U;
    std::uint64_t discarded_trajectories = 0U;
    std::uint64_t deterministic_merge_fingerprint = 0U;

    void validate() const;
};

[[nodiscard]] MultiwayTrainingReport make_multiway_training_report(
    const MultiwayBlueprintTrainingStatus& status,
    std::uint64_t deterministic_merge_fingerprint = 0U) noexcept;
void save_multiway_training_report_atomic(
    const std::filesystem::path& path,
    const MultiwayTrainingReport& report);

}  // namespace texas::solver::multiway
