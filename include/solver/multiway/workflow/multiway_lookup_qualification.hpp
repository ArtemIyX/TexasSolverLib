#pragma once

#include "solver/multiway/blueprint/multiway_artifact.hpp"
#include "solver/multiway/blueprint/multiway_blueprint_trainer.hpp"

#include <cstdint>
#include <filesystem>

namespace texas::solver::multiway {

struct MultiwayLookupQualificationReport {
    std::uint64_t lookup_hits = 0U;
    std::uint64_t missing_infosets = 0U;
    std::uint64_t missing_buckets = 0U;
    std::uint64_t action_menu_mismatches = 0U;
    std::uint64_t replay_fingerprint = 0U;
    std::uint64_t second_replay_fingerprint = 0U;

    [[nodiscard]] bool passed() const noexcept {
        return missing_infosets == 0U && missing_buckets == 0U && action_menu_mismatches == 0U &&
            (second_replay_fingerprint == 0U || replay_fingerprint == second_replay_fingerprint);
    }
};

// Replays deterministic sampled trajectories from the supplied initial root.
// Opponent policy requests must resolve through the immutable full blueprint.
[[nodiscard]] MultiwayLookupQualificationReport qualify_multiway_required_lookups(
    const MultiwayBlueprintTrainingConfig& config,
    const MultiwayRootSnapshot& root,
    const MultiwayBucketRegistry& buckets,
    const MultiwayFullBlueprintArtifact& blueprint,
    std::uint64_t trajectory_count);

void save_multiway_lookup_qualification_report_atomic(
    const std::filesystem::path& path,
    const MultiwayLookupQualificationReport& report);

}  // namespace texas::solver::multiway
