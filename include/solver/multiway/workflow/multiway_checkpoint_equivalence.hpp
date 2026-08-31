#pragma once

#include "solver/multiway/workflow/multiway_evidence.hpp"

#include <array>
#include <cstdint>
#include <filesystem>

namespace texas::solver::multiway {

struct MultiwayEquivalenceRunEvidence {
    MultiwayProducerIdentity producer{};
    std::uint64_t deterministic_seed = 0U;
    std::uint64_t schedule_fingerprint = 0U;
    std::uint64_t coverage_fingerprint = 0U;
    std::array<std::uint64_t, 4> street_rows{};
    std::uint64_t terminal_visits = 0U;
    std::uint64_t leaf_visits = 0U;
    std::uint64_t discarded_trajectories = 0U;
    std::uint64_t merge_fingerprint = 0U;
    std::uint64_t completed_trajectories = 0U;

    void validate() const;
};

struct MultiwayCheckpointEquivalenceRequest {
    std::filesystem::path continuous_blueprint;
    std::filesystem::path resumed_blueprint;
    MultiwayEquivalenceRunEvidence continuous{};
    MultiwayEquivalenceRunEvidence resumed{};
};

struct MultiwayCheckpointEquivalenceReport {
    std::uint32_t schema_version = MULTIWAY_CHECKPOINT_EQUIVALENCE_SCHEMA_VERSION;
    MultiwayEvidenceHeader evidence{
        MULTIWAY_CHECKPOINT_EQUIVALENCE_SCHEMA_VERSION,
        MultiwayWorkflowProfileKind::Acceptance,
        {}};
    bool model_identity_equal = false;
    bool configuration_equal = false;
    bool seed_equal = false;
    bool schedule_equal = false;
    bool blueprint_bytes_equal = false;
    bool blueprint_payload_hash_equal = false;
    bool coverage_equal = false;
    bool street_rows_equal = false;
    bool terminal_leaf_discard_equal = false;
    bool merge_fingerprint_equal = false;
    bool completed_trajectories_equal = false;
    std::uint64_t continuous_blueprint_bytes = 0U;
    std::uint64_t resumed_blueprint_bytes = 0U;
    std::uint64_t continuous_blueprint_hash = 0U;
    std::uint64_t resumed_blueprint_hash = 0U;

    [[nodiscard]] bool passed() const noexcept;
    void validate() const;
};

[[nodiscard]] MultiwayCheckpointEquivalenceReport compare_multiway_checkpoints(
    const MultiwayCheckpointEquivalenceRequest& request);

void save_multiway_checkpoint_equivalence_atomic(
    const std::filesystem::path& path,
    const MultiwayCheckpointEquivalenceReport& report);

}  // namespace texas::solver::multiway
