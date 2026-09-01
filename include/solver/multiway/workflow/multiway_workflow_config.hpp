#pragma once

#include "solver/multiway/blueprint/multiway_blueprint_config.hpp"
#include "solver/multiway/workflow/multiway_evidence.hpp"

#include <cstdint>
#include <filesystem>
#include <string>

namespace texas::solver::multiway {

struct MultiwayWorkflowConfig {
    std::uint32_t schema_version = 0U;
    MultiwayWorkflowProfileKind profile_kind = MultiwayWorkflowProfileKind::Acceptance;
    std::string profile_id;
    MultiwayBlueprintConfig model;
    std::uint32_t preflop_classes = 0U;
    std::string storage_backend;
    std::uint32_t max_decision_depth = 0U;
    std::uint32_t max_public_chance_depth = 0U;
    std::uint64_t deterministic_seed = 0U;
    std::uint32_t training_worker_count = 0U;
    std::uint64_t target_trajectories = 0U;
    std::uint64_t maximum_public_states = 0U;
    std::uint64_t maximum_sparse_rows = 0U;
    std::uint64_t maximum_sparse_values = 0U;
    std::uint64_t worker_delta_capacity = 0U;
    std::uint64_t trajectories_per_batch = 0U;
    std::uint64_t checkpoint_interval = 0U;
    std::uint64_t disk_space_requirement_bytes = 0U;
    std::uint64_t process_memory_limit_bytes = 0U;

    void validate() const;
    [[nodiscard]] bool capacities_resolved() const noexcept;
    [[nodiscard]] std::uint64_t fingerprint() const noexcept;
};

struct MultiwayTrainingWorkerResolution {
    std::uint32_t requested = 0U;
    std::uint32_t effective = 0U;
};

[[nodiscard]] MultiwayTrainingWorkerResolution resolve_multiway_training_workers(
    std::uint32_t configured_workers,
    std::uint32_t override_workers,
    std::uint64_t trajectories_per_batch);

[[nodiscard]] MultiwayWorkflowConfig parse_multiway_workflow_config(const std::string& text);
[[nodiscard]] MultiwayWorkflowConfig load_multiway_workflow_config(const std::filesystem::path& path);

}  // namespace texas::solver::multiway
