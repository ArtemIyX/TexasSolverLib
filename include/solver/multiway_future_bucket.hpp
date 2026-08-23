#pragma once

#include "core/legacy_namespace_compat.hpp"

#include "solver/multiway_bucket_artifact.hpp"

#include <array>
#include <cstdint>

namespace texas::solver::multiway {

inline constexpr std::uint32_t MULTIWAY_FUTURE_BUCKET_ARTIFACT_SCHEMA_VERSION = 1U;

// Stable offline-only feature surface. Values intentionally contain no
// opponent private cards or runtime policy state.
struct MultiwayFutureBucketFeatures {
    std::array<double, 10U> values{};
    std::uint64_t feature_version = 1U;
};

struct MultiwayFutureBucketProfile {
    std::uint64_t feature_version = 1U;
    std::uint64_t clustering_version = 1U;
    std::uint64_t seed = 1U;
    std::uint32_t lloyd_iterations = 4U;
    std::uint32_t flop_bucket_count = 96U;
    std::uint32_t turn_bucket_count = 128U;
    std::uint32_t river_bucket_count = 192U;

    void validate() const;
    [[nodiscard]] std::uint32_t bucket_count(Street street) const;
};

[[nodiscard]] MultiwayFutureBucketFeatures make_multiway_future_bucket_features(
    Street street,
    const std::vector<std::uint8_t>& canonical_board,
    const std::array<std::uint8_t, 2>& compact_hole,
    std::uint64_t feature_version = 1U);

// Immutable consumer with explicit producer provenance. Cluster construction
// is offline; live callers only use registry lookup.
class MultiwayFutureBucketArtifact {
public:
    MultiwayFutureBucketArtifact(MultiwayFutureBucketProfile profile, MultiwayBucketRegistry registry);

    [[nodiscard]] const MultiwayFutureBucketProfile& profile() const noexcept { return profile_; }
    [[nodiscard]] const MultiwayBucketRegistry& registry() const noexcept { return registry_; }
    [[nodiscard]] std::uint32_t lookup(
        Street street, const std::vector<std::uint8_t>& board,
        const std::array<std::uint8_t, 2>& hole) const;

private:
    MultiwayFutureBucketProfile profile_{};
    MultiwayBucketRegistry registry_;
};

[[nodiscard]] MultiwayFutureBucketArtifact build_multiway_future_bucket_artifact(
    const MultiwayModelIdentity& identity,
    const std::vector<MultiwayBucketBoardRequest>& boards,
    const MultiwayFutureBucketProfile& profile = {});
[[nodiscard]] std::vector<std::uint8_t> serialize_multiway_future_bucket_artifact(
    const MultiwayFutureBucketArtifact& artifact);
[[nodiscard]] MultiwayFutureBucketArtifact deserialize_multiway_future_bucket_artifact(
    const std::vector<std::uint8_t>& bytes);

}  // namespace texas::solver::multiway
