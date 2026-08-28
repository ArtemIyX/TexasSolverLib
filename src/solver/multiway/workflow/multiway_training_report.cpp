#include "solver/multiway/workflow/multiway_training_report.hpp"

#include "core/atomic_publish.hpp"

#include <fstream>
#include <stdexcept>

namespace texas::solver::multiway {

void MultiwayTrainingReport::validate() const {
    if (discarded_trajectories > trajectories ||
        (batches != 0U && deterministic_merge_fingerprint == 0U)) {
        throw std::invalid_argument("invalid multiway training report");
    }
}

MultiwayTrainingReport make_multiway_training_report(
    const MultiwayBlueprintTrainingStatus& status, std::uint64_t fingerprint) noexcept {
    MultiwayTrainingReport report;
    report.batches = status.batches;
    report.trajectories = status.trajectories;
    report.discarded_trajectories = status.discarded_trajectories;
    report.preflop_rows = status.preflop_rows;
    report.flop_rows = status.flop_rows;
    report.turn_rows = status.turn_rows;
    report.river_rows = status.river_rows;
    report.terminal_visits = status.terminal_visits;
    report.leaf_visits = status.leaf_visits;
    report.deterministic_merge_fingerprint = fingerprint;
    // The current trainer exposes accepted trajectories only. Discard telemetry
    // must be wired from the sampler before this field can be authoritative.
    return report;
}

void save_multiway_training_report_atomic(const std::filesystem::path& path,
    const MultiwayTrainingReport& report) {
    report.validate();
    const auto temporary = path.string() + ".tmp";
    std::ofstream output(temporary, std::ios::trunc);
    if (!output) throw std::runtime_error("cannot open training report");
    output << "{\n  \"batches\": " << report.batches
           << ",\n  \"trajectories\": " << report.trajectories
           << ",\n  \"preflop_rows\": " << report.preflop_rows
           << ",\n  \"flop_rows\": " << report.flop_rows
           << ",\n  \"turn_rows\": " << report.turn_rows
           << ",\n  \"river_rows\": " << report.river_rows
           << ",\n  \"terminal_visits\": " << report.terminal_visits
           << ",\n  \"leaf_visits\": " << report.leaf_visits
           << ",\n  \"discarded_trajectories\": " << report.discarded_trajectories
           << ",\n  \"deterministic_merge_fingerprint\": " << report.deterministic_merge_fingerprint << "\n}\n";
    output.close();
    core::publish_atomic_replace(temporary, path, "cannot publish training report");
}
}  // namespace texas::solver::multiway
