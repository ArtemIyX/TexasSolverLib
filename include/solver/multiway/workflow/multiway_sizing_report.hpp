#pragma once

#include "solver/multiway/workflow/multiway_training_report.hpp"
#include "solver/multiway/workflow/multiway_workflow_config.hpp"

#include <cstdint>
#include <filesystem>

namespace texas::solver::multiway {

struct MultiwaySizingReport {
    MultiwayEvidenceHeader evidence{
        MULTIWAY_SIZING_REPORT_SCHEMA_VERSION,
        MultiwayWorkflowProfileKind::Sizing,
        {}};
    std::uint64_t target_trajectories = 0U;
    std::uint64_t accepted_trajectories = 0U;
    std::uint64_t discarded_trajectories = 0U;
    std::uint64_t peak_public_states = 0U;
    std::uint64_t peak_sparse_rows = 0U;
    std::uint64_t peak_sparse_values = 0U;
    std::uint64_t peak_worker_delta_entries = 0U;
    std::uint64_t peak_compact_storage_bytes = 0U;
    std::uint64_t peak_process_rss_bytes = 0U;
    std::uint64_t process_memory_limit_bytes = 0U;
    bool process_rss_available = false;
    std::uint64_t elapsed_wall_nanoseconds = 0U;
    double trajectories_per_second = 0.0;
    MultiwayTrainingCapacityStage capacity_exhaustion_stage =
        MultiwayTrainingCapacityStage::None;
    bool target_reached = false;
    bool passed = false;

    void validate() const;
};

[[nodiscard]] MultiwaySizingReport make_multiway_sizing_report(
    const MultiwayWorkflowConfig& workflow,
    const MultiwayTrainingReport& training_report);

void save_multiway_sizing_report_atomic(
    const std::filesystem::path& path,
    const MultiwaySizingReport& report);

}  // namespace texas::solver::multiway
