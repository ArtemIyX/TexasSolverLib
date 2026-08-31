#include "solver/multiway/workflow/multiway_sizing_report.hpp"

#include "core/atomic_publish.hpp"

#include <fstream>
#include <stdexcept>

namespace texas::solver::multiway {

void MultiwaySizingReport::validate() const {
    evidence.validate(MULTIWAY_SIZING_REPORT_SCHEMA_VERSION);
    if (evidence.profile_kind != MultiwayWorkflowProfileKind::Sizing ||
        accepted_trajectories + discarded_trajectories < accepted_trajectories ||
        static_cast<unsigned>(capacity_exhaustion_stage) >
            static_cast<unsigned>(MultiwayTrainingCapacityStage::WorkerDeltas)) {
        throw std::invalid_argument("invalid multiway sizing report");
    }
    const bool within_memory_limit = !process_rss_available ||
        process_memory_limit_bytes == 0U ||
        peak_process_rss_bytes <= process_memory_limit_bytes;
    if (target_reached != (accepted_trajectories >= target_trajectories) ||
        passed != (target_reached && capacity_exhaustion_stage ==
            MultiwayTrainingCapacityStage::None && within_memory_limit)) {
        throw std::invalid_argument("inconsistent multiway sizing result");
    }
    if (process_rss_available && peak_process_rss_bytes == 0U) {
        throw std::invalid_argument("available sizing RSS must be non-zero");
    }
}

MultiwaySizingReport make_multiway_sizing_report(
    const MultiwayWorkflowConfig& workflow,
    const MultiwayTrainingReport& training_report) {
    workflow.validate();
    training_report.validate();
    if (workflow.profile_kind != MultiwayWorkflowProfileKind::Sizing ||
        !workflow.capacities_resolved() ||
        training_report.evidence.profile_kind != MultiwayWorkflowProfileKind::Sizing ||
        training_report.evidence.producer.model_identity !=
            make_multiway_model_identity(workflow.model) ||
        training_report.evidence.producer.workflow_config_fingerprint != workflow.fingerprint()) {
        throw std::invalid_argument("sizing workflow and training evidence are incompatible");
    }

    MultiwaySizingReport result;
    result.evidence = {
        MULTIWAY_SIZING_REPORT_SCHEMA_VERSION,
        MultiwayWorkflowProfileKind::Sizing,
        training_report.evidence.producer};
    result.target_trajectories = workflow.target_trajectories;
    result.accepted_trajectories = training_report.accepted_trajectories;
    result.discarded_trajectories = training_report.discarded_trajectories;
    result.peak_public_states = training_report.peak_public_states;
    result.peak_sparse_rows = training_report.peak_sparse_rows;
    result.peak_sparse_values = training_report.peak_sparse_values;
    result.peak_worker_delta_entries = training_report.peak_worker_delta_entries;
    result.peak_compact_storage_bytes = training_report.peak_compact_storage_bytes;
    result.peak_process_rss_bytes = training_report.peak_process_rss_bytes;
    result.process_memory_limit_bytes = workflow.process_memory_limit_bytes;
    result.process_rss_available = training_report.process_rss_available;
    result.elapsed_wall_nanoseconds = training_report.elapsed_wall_nanoseconds;
    result.trajectories_per_second = training_report.trajectories_per_second();
    result.capacity_exhaustion_stage = training_report.capacity_exhaustion_stage;
    result.target_reached = result.accepted_trajectories >= result.target_trajectories;
    result.passed = result.target_reached &&
        result.capacity_exhaustion_stage == MultiwayTrainingCapacityStage::None &&
        (!result.process_rss_available || result.process_memory_limit_bytes == 0U ||
            result.peak_process_rss_bytes <= result.process_memory_limit_bytes);
    result.validate();
    return result;
}

void save_multiway_sizing_report_atomic(
    const std::filesystem::path& path,
    const MultiwaySizingReport& report) {
    report.validate();
    const auto temporary = path.string() + ".tmp";
    std::ofstream output(temporary, std::ios::trunc);
    if (!output) throw std::runtime_error("cannot open sizing report");
    output << "{\n  \"evidence\": "
           << serialize_multiway_evidence_header(report.evidence)
           << ",\n  \"target_trajectories\": " << report.target_trajectories
           << ",\n  \"accepted_trajectories\": " << report.accepted_trajectories
           << ",\n  \"discarded_trajectories\": " << report.discarded_trajectories
           << ",\n  \"peak_public_states\": " << report.peak_public_states
           << ",\n  \"peak_sparse_rows\": " << report.peak_sparse_rows
           << ",\n  \"peak_sparse_values\": " << report.peak_sparse_values
           << ",\n  \"peak_worker_delta_entries\": " << report.peak_worker_delta_entries
           << ",\n  \"peak_compact_storage_bytes\": " << report.peak_compact_storage_bytes
           << ",\n  \"peak_process_rss_bytes\": " << report.peak_process_rss_bytes
           << ",\n  \"process_memory_limit_bytes\": " << report.process_memory_limit_bytes
           << ",\n  \"process_rss_available\": " << (report.process_rss_available ? "true" : "false")
           << ",\n  \"elapsed_wall_nanoseconds\": " << report.elapsed_wall_nanoseconds
           << ",\n  \"trajectories_per_second\": " << report.trajectories_per_second
           << ",\n  \"capacity_exhaustion_stage\": "
           << static_cast<unsigned>(report.capacity_exhaustion_stage)
           << ",\n  \"target_reached\": " << (report.target_reached ? "true" : "false")
           << ",\n  \"passed\": " << (report.passed ? "true" : "false") << "\n}\n";
    output.close();
    core::publish_atomic_replace(temporary, path, "cannot publish sizing report");
}

}  // namespace texas::solver::multiway
