#include "solver/multiway/workflow/multiway_checkpoint_equivalence.hpp"

#include "core/atomic_publish.hpp"
#include "core/fingerprint.hpp"

#include <array>
#include <fstream>
#include <stdexcept>

namespace texas::solver::multiway {
namespace {

struct FileFingerprint {
    std::uint64_t bytes = 0U;
    std::uint64_t hash = 0U;
};

FileFingerprint fingerprint_file(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) throw std::runtime_error("cannot open checkpoint-equivalence blueprint");
    std::array<char, 64U * 1024U> buffer{};
    FileFingerprint result;
    auto hash = core::fingerprint::FNV1A_OFFSET;
    while (input) {
        input.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
        const auto count = input.gcount();
        if (count <= 0) break;
        result.bytes += static_cast<std::uint64_t>(count);
        for (std::streamsize index = 0; index < count; ++index) {
            core::fingerprint::append_u8(hash,
                static_cast<std::uint8_t>(static_cast<unsigned char>(buffer[static_cast<std::size_t>(index)])));
        }
    }
    if (!input.eof()) throw std::runtime_error("cannot read checkpoint-equivalence blueprint");
    result.hash = core::fingerprint::finish(hash);
    return result;
}

bool equal_run_configuration(
    const MultiwayEquivalenceRunEvidence& first,
    const MultiwayEquivalenceRunEvidence& second) noexcept {
    return first.deterministic_seed == second.deterministic_seed &&
        first.schedule_fingerprint == second.schedule_fingerprint;
}

}  // namespace

void MultiwayEquivalenceRunEvidence::validate() const {
    producer.validate();
    if (deterministic_seed == 0U || schedule_fingerprint == 0U ||
        coverage_fingerprint == 0U || merge_fingerprint == 0U) {
        throw std::invalid_argument("checkpoint-equivalence run evidence is incomplete");
    }
}

bool MultiwayCheckpointEquivalenceReport::passed() const noexcept {
    return schema_version == MULTIWAY_CHECKPOINT_EQUIVALENCE_SCHEMA_VERSION &&
        model_identity_equal && configuration_equal && seed_equal && schedule_equal &&
        blueprint_bytes_equal && blueprint_payload_hash_equal && coverage_equal &&
        street_rows_equal && terminal_leaf_discard_equal && merge_fingerprint_equal &&
        completed_trajectories_equal;
}

void MultiwayCheckpointEquivalenceReport::validate() const {
    if (schema_version != MULTIWAY_CHECKPOINT_EQUIVALENCE_SCHEMA_VERSION ||
        continuous_blueprint_bytes == 0U || resumed_blueprint_bytes == 0U ||
        continuous_blueprint_hash == 0U || resumed_blueprint_hash == 0U) {
        throw std::invalid_argument("invalid checkpoint-equivalence report");
    }
    evidence.validate(MULTIWAY_CHECKPOINT_EQUIVALENCE_SCHEMA_VERSION);
}

MultiwayCheckpointEquivalenceReport compare_multiway_checkpoints(
    const MultiwayCheckpointEquivalenceRequest& request) {
    request.continuous.validate();
    request.resumed.validate();
    validate_matching_producer_identity(request.continuous.producer, request.resumed.producer);
    const auto continuous = fingerprint_file(request.continuous_blueprint);
    const auto resumed = fingerprint_file(request.resumed_blueprint);

    MultiwayCheckpointEquivalenceReport report;
    report.model_identity_equal = request.continuous.producer.model_identity ==
        request.resumed.producer.model_identity;
    report.configuration_equal = request.continuous.producer.workflow_config_fingerprint ==
        request.resumed.producer.workflow_config_fingerprint;
    report.seed_equal = request.continuous.deterministic_seed == request.resumed.deterministic_seed;
    report.schedule_equal = request.continuous.schedule_fingerprint == request.resumed.schedule_fingerprint;
    report.blueprint_bytes_equal = continuous.bytes == resumed.bytes && continuous.hash == resumed.hash;
    report.blueprint_payload_hash_equal = continuous.hash == resumed.hash;
    report.evidence = {
        MULTIWAY_CHECKPOINT_EQUIVALENCE_SCHEMA_VERSION,
        MultiwayWorkflowProfileKind::Acceptance,
        request.continuous.producer};
    report.coverage_equal = request.continuous.coverage_fingerprint == request.resumed.coverage_fingerprint;
    report.street_rows_equal = request.continuous.street_rows == request.resumed.street_rows;
    report.terminal_leaf_discard_equal =
        request.continuous.terminal_visits == request.resumed.terminal_visits &&
        request.continuous.leaf_visits == request.resumed.leaf_visits &&
        request.continuous.discarded_trajectories == request.resumed.discarded_trajectories;
    report.merge_fingerprint_equal = request.continuous.merge_fingerprint == request.resumed.merge_fingerprint;
    report.completed_trajectories_equal =
        request.continuous.completed_trajectories == request.resumed.completed_trajectories;
    report.continuous_blueprint_bytes = continuous.bytes;
    report.resumed_blueprint_bytes = resumed.bytes;
    report.continuous_blueprint_hash = continuous.hash;
    report.resumed_blueprint_hash = resumed.hash;
    return report;
}

void save_multiway_checkpoint_equivalence_atomic(
    const std::filesystem::path& path,
    const MultiwayCheckpointEquivalenceReport& report) {
    report.validate();
    if (!report.passed()) {
        throw std::invalid_argument("failed checkpoint equivalence cannot be published");
    }
    const auto temporary = path.string() + ".tmp";
    std::ofstream output(temporary, std::ios::trunc);
    if (!output) throw std::runtime_error("cannot open checkpoint-equivalence report");
    output << "{\n  \"schema_version\": " << report.schema_version
           << ",\n  \"evidence\": "
           << serialize_multiway_evidence_header(report.evidence)
           << ",\n  \"passed\": true"
           << ",\n  \"model_identity_equal\": true"
           << ",\n  \"configuration_equal\": true"
           << ",\n  \"seed_equal\": true"
           << ",\n  \"schedule_equal\": true"
           << ",\n  \"blueprint_bytes_equal\": true"
           << ",\n  \"blueprint_payload_hash_equal\": true"
           << ",\n  \"coverage_equal\": true"
           << ",\n  \"street_rows_equal\": true"
           << ",\n  \"terminal_leaf_discard_equal\": true"
           << ",\n  \"merge_fingerprint_equal\": true"
           << ",\n  \"completed_trajectories_equal\": true"
           << ",\n  \"continuous_blueprint_bytes\": " << report.continuous_blueprint_bytes
           << ",\n  \"resumed_blueprint_bytes\": " << report.resumed_blueprint_bytes
           << ",\n  \"continuous_blueprint_hash\": " << report.continuous_blueprint_hash
           << ",\n  \"resumed_blueprint_hash\": " << report.resumed_blueprint_hash << "\n}\n";
    output.close();
    core::publish_atomic_replace(temporary, path, "cannot publish checkpoint-equivalence report");
}

}  // namespace texas::solver::multiway
