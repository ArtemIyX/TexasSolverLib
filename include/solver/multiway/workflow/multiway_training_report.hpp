#pragma once

#include "solver/multiway/blueprint/multiway_blueprint_trainer.hpp"
#include "solver/multiway/workflow/multiway_evidence.hpp"

#include <filesystem>
#include <array>

namespace texas::solver::multiway {

struct MultiwayTrainingReport {
    MultiwayEvidenceHeader evidence{
        MULTIWAY_TRAINING_REPORT_SCHEMA_VERSION,
        MultiwayWorkflowProfileKind::Acceptance,
        {}};
    std::uint64_t elapsed_wall_nanoseconds = 0U;
    std::uint64_t current_process_rss_bytes = 0U;
    std::uint64_t peak_process_rss_bytes = 0U;
    std::uint64_t process_memory_limit_bytes = 0U;
    bool process_rss_available = false;
    bool started_at_preflop = false;
    std::uint64_t peak_public_states = 0U;
    std::uint64_t peak_sparse_rows = 0U;
    std::uint64_t peak_sparse_values = 0U;
    std::uint64_t peak_worker_delta_entries = 0U;
    std::uint64_t peak_compact_storage_bytes = 0U;
    std::uint64_t configured_max_public_states = 0U;
    std::uint64_t configured_max_sparse_rows = 0U;
    std::uint64_t configured_max_sparse_values = 0U;
    std::uint64_t configured_worker_delta_capacity = 0U;
    std::uint64_t cumulative_worker_delta_entries = 0U;
    std::uint64_t worker_active_nanoseconds = 0U;
    std::uint64_t coordinator_wait_nanoseconds = 0U;
    std::uint64_t delta_sort_nanoseconds = 0U;
    std::uint64_t merge_nanoseconds = 0U;
    std::uint64_t minimum_worker_trajectories = 0U;
    std::uint64_t maximum_worker_trajectories = 0U;
    std::uint32_t requested_worker_count = 0U;
    std::uint32_t effective_worker_count = 0U;
    std::uint64_t trajectories_per_batch = 0U;
    std::uint64_t memory_preflight_estimate_bytes = 0U;
    std::uint64_t checkpoint_bytes = 0U;
    std::uint64_t checkpoint_write_nanoseconds = 0U;
    std::uint64_t blueprint_bytes = 0U;
    std::uint64_t blueprint_export_nanoseconds = 0U;
    MultiwayTrainingCapacityStage capacity_exhaustion_stage =
        MultiwayTrainingCapacityStage::None;
    std::vector<std::uint64_t> admitted_rows_per_batch;
    [[nodiscard]] double trajectories_per_second() const noexcept;
    std::uint64_t batches = 0U;
    std::uint64_t trajectories = 0U;
    std::uint64_t accepted_trajectories = 0U;
    std::uint64_t preflop_rows = 0U;
    std::uint64_t flop_rows = 0U;
    std::uint64_t turn_rows = 0U;
    std::uint64_t river_rows = 0U;
    std::uint64_t terminal_visits = 0U;
    std::uint64_t leaf_visits = 0U;
    std::array<std::uint64_t, 4> street_visits{};
    std::uint64_t discarded_trajectories = 0U;
    std::uint64_t deterministic_merge_fingerprint = 0U;
    bool failed = false;

    void validate() const;
};

[[nodiscard]] MultiwayTrainingReport make_multiway_training_report(
    const MultiwayBlueprintTrainingStatus& status,
    std::uint64_t deterministic_merge_fingerprint,
    MultiwayEvidenceHeader evidence,
    std::uint64_t process_memory_limit_bytes = 0U) noexcept;
void save_multiway_training_report_atomic(
    const std::filesystem::path& path,
    const MultiwayTrainingReport& report);

}  // namespace texas::solver::multiway
