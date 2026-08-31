#include "solver/multiway/workflow/multiway_training_report.hpp"

#include "core/atomic_publish.hpp"

#include <fstream>
#include <cmath>
#include <stdexcept>

namespace texas::solver::multiway {

void MultiwayTrainingReport::validate() const {
    if (accepted_trajectories > trajectories || discarded_trajectories > trajectories ||
        (batches != 0U && deterministic_merge_fingerprint == 0U && !failed)) {
        throw std::invalid_argument("invalid multiway training report");
    }
    evidence.validate(MULTIWAY_TRAINING_REPORT_SCHEMA_VERSION);
    if (process_rss_available && current_process_rss_bytes == 0U) {
        throw std::invalid_argument("available process RSS must be non-zero");
    }
    if (peak_process_rss_bytes < current_process_rss_bytes ||
        !std::isfinite(trajectories_per_second()) ||
        static_cast<unsigned>(capacity_exhaustion_stage) >
            static_cast<unsigned>(MultiwayTrainingCapacityStage::WorkerDeltas)) {
        throw std::invalid_argument("invalid multiway training telemetry");
    }
    for (std::size_t index = 1U; index < admitted_rows_per_batch.size(); ++index) {
        if (admitted_rows_per_batch[index] < admitted_rows_per_batch[index - 1U]) {
            throw std::invalid_argument("training row-growth series is not monotonic");
        }
    }
}

double MultiwayTrainingReport::trajectories_per_second() const noexcept {
    if (elapsed_wall_nanoseconds == 0U) return 0.0;
    return static_cast<double>(trajectories) * 1'000'000'000.0 /
        static_cast<double>(elapsed_wall_nanoseconds);
}

MultiwayTrainingReport make_multiway_training_report(
    const MultiwayBlueprintTrainingStatus& status, std::uint64_t fingerprint,
    MultiwayEvidenceHeader evidence, std::uint64_t process_memory_limit_bytes) noexcept {
    MultiwayTrainingReport report;
    report.batches = status.batches;
    report.trajectories = status.trajectories;
    report.discarded_trajectories = status.discarded_trajectories;
    report.accepted_trajectories = status.trajectories >= status.discarded_trajectories
        ? status.trajectories - status.discarded_trajectories : 0U;
    report.preflop_rows = status.preflop_rows;
    report.flop_rows = status.flop_rows;
    report.turn_rows = status.turn_rows;
    report.river_rows = status.river_rows;
    report.terminal_visits = status.terminal_visits;
    report.leaf_visits = status.leaf_visits;
    report.street_visits = status.street_visits;
    report.deterministic_merge_fingerprint = fingerprint;
    report.evidence = evidence;
    report.peak_public_states = status.peak_public_states;
    report.peak_sparse_rows = status.peak_sparse_rows;
    report.peak_sparse_values = status.peak_sparse_values;
    report.peak_worker_delta_entries = status.peak_worker_delta_entries;
    report.peak_compact_storage_bytes = status.peak_compact_storage_bytes;
    report.configured_max_public_states = status.configured_max_public_states;
    report.configured_max_sparse_rows = status.configured_max_sparse_rows;
    report.configured_max_sparse_values = status.configured_max_sparse_values;
    report.configured_worker_delta_capacity = status.configured_worker_delta_capacity;
    report.capacity_exhaustion_stage = status.capacity_exhaustion_stage;
    report.admitted_rows_per_batch = status.admitted_rows_per_batch;
    report.current_process_rss_bytes = status.current_process_rss_bytes;
    report.peak_process_rss_bytes = status.peak_process_rss_bytes;
    report.process_memory_limit_bytes = process_memory_limit_bytes;
    report.process_rss_available = status.process_rss_available;
    report.started_at_preflop = status.preflop_rows > 0U;
    report.elapsed_wall_nanoseconds = status.elapsed_wall_nanoseconds;
    return report;
}

void save_multiway_training_report_atomic(const std::filesystem::path& path,
    const MultiwayTrainingReport& report) {
    report.validate();
    const auto temporary = path.string() + ".tmp";
    std::ofstream output(temporary, std::ios::trunc);
    if (!output) throw std::runtime_error("cannot open training report");
    output << "{\n  \"evidence\": " << serialize_multiway_evidence_header(report.evidence)
           << ",\n  \"batches\": " << report.batches
           << ",\n  \"trajectories\": " << report.trajectories
           << ",\n  \"accepted_trajectories\": " << report.accepted_trajectories
           << ",\n  \"preflop_rows\": " << report.preflop_rows
           << ",\n  \"flop_rows\": " << report.flop_rows
           << ",\n  \"turn_rows\": " << report.turn_rows
           << ",\n  \"river_rows\": " << report.river_rows
           << ",\n  \"terminal_visits\": " << report.terminal_visits
           << ",\n  \"leaf_visits\": " << report.leaf_visits
           << ",\n  \"street_visits\": [" << report.street_visits[0U] << ','
           << report.street_visits[1U] << ',' << report.street_visits[2U] << ','
           << report.street_visits[3U] << ']'
           << ",\n  \"discarded_trajectories\": " << report.discarded_trajectories
           << ",\n  \"deterministic_merge_fingerprint\": " << report.deterministic_merge_fingerprint
           << ",\n  \"failed\": " << (report.failed ? "true" : "false")
           << ",\n  \"elapsed_wall_nanoseconds\": " << report.elapsed_wall_nanoseconds
           << ",\n  \"trajectories_per_second\": " << report.trajectories_per_second()
           << ",\n  \"current_process_rss_bytes\": " << report.current_process_rss_bytes
           << ",\n  \"peak_process_rss_bytes\": " << report.peak_process_rss_bytes
           << ",\n  \"process_memory_limit_bytes\": " << report.process_memory_limit_bytes
           << ",\n  \"process_rss_available\": " << (report.process_rss_available ? "true" : "false")
           << ",\n  \"started_at_preflop\": " << (report.started_at_preflop ? "true" : "false")
           << ",\n  \"peak_public_states\": " << report.peak_public_states
           << ",\n  \"peak_sparse_rows\": " << report.peak_sparse_rows
           << ",\n  \"peak_sparse_values\": " << report.peak_sparse_values
           << ",\n  \"peak_worker_delta_entries\": " << report.peak_worker_delta_entries
           << ",\n  \"peak_compact_storage_bytes\": " << report.peak_compact_storage_bytes
           << ",\n  \"configured_max_public_states\": " << report.configured_max_public_states
           << ",\n  \"configured_max_sparse_rows\": " << report.configured_max_sparse_rows
           << ",\n  \"configured_max_sparse_values\": " << report.configured_max_sparse_values
           << ",\n  \"configured_worker_delta_capacity\": " << report.configured_worker_delta_capacity
           << ",\n  \"checkpoint_bytes\": " << report.checkpoint_bytes
           << ",\n  \"checkpoint_write_nanoseconds\": " << report.checkpoint_write_nanoseconds
           << ",\n  \"blueprint_bytes\": " << report.blueprint_bytes
           << ",\n  \"blueprint_export_nanoseconds\": " << report.blueprint_export_nanoseconds
           << ",\n  \"capacity_exhaustion_stage\": "
           << static_cast<unsigned>(report.capacity_exhaustion_stage)
           << ",\n  \"admitted_rows_per_batch\": [";
    for (std::size_t index = 0U; index < report.admitted_rows_per_batch.size(); ++index) {
        if (index != 0U) output << ',';
        output << report.admitted_rows_per_batch[index];
    }
    output << "]\n}\n";
    output.close();
    core::publish_atomic_replace(temporary, path, "cannot publish training report");
}
}  // namespace texas::solver::multiway
