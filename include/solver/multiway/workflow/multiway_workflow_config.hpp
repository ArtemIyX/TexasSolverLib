#pragma once

#include "solver/multiway/blueprint/multiway_blueprint_config.hpp"

#include <cstdint>
#include <filesystem>
#include <string>

namespace texas::solver::multiway {

struct MultiwayWorkflowConfig {
    std::uint32_t schema_version = 0U;
    std::string profile_id;
    MultiwayBlueprintConfig model;
    std::uint32_t preflop_classes = 0U;
    std::string storage_backend;
    std::uint32_t max_decision_depth = 0U;
    std::uint32_t max_public_chance_depth = 0U;
    std::uint64_t deterministic_seed = 0U;
    std::uint32_t reference_worker_count = 0U;
    std::uint64_t target_trajectories = 0U;

    void validate() const;
    [[nodiscard]] std::uint64_t fingerprint() const noexcept;
};

[[nodiscard]] MultiwayWorkflowConfig parse_multiway_workflow_config(const std::string& text);
[[nodiscard]] MultiwayWorkflowConfig load_multiway_workflow_config(const std::filesystem::path& path);

}  // namespace texas::solver::multiway
