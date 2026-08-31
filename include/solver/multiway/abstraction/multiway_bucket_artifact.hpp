#pragma once

#include "solver/multiway/abstraction/multiway_bucket_model.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <vector>

namespace texas::solver::multiway {

inline constexpr std::uint32_t MULTIWAY_BUCKET_ARTIFACT_SCHEMA_VERSION = 4U;

// Versioned deterministic baseline used until trained clustering artifacts are
// introduced. The counts are part of the model identity through blueprint config.
struct MultiwayBucketBaselineProfile {
    std::uint32_t schema_version = MULTIWAY_BUCKET_ARTIFACT_SCHEMA_VERSION;
    std::uint64_t feature_version = 1U;
    std::uint32_t flop_bucket_count = 96U;
    std::uint32_t turn_bucket_count = 128U;
    std::uint32_t river_bucket_count = 192U;

    [[nodiscard]] static MultiwayBucketBaselineProfile standard() noexcept;
    void validate() const;
    [[nodiscard]] std::uint32_t bucket_count(core::Street street) const;
};

// Compact, allocation-free features calculated for a live hole pair on a
// canonical sorted board. They are stable artifact inputs, not a learned model.
struct MultiwayBucketFeatures {
    std::uint16_t board_rank_mask = 0U;
    std::uint8_t board_suit_mask = 0U;
    std::uint8_t board_pair_count = 0U;
    std::uint8_t board_size = 0U;
    std::uint8_t hole_high_rank = 0U;
    std::uint8_t hole_low_rank = 0U;
    std::uint8_t hole_suited = 0U;
    std::uint8_t hole_pairs_board = 0U;
    std::uint8_t hole_suit_matches_board = 0U;
};

struct MultiwayBucketBoardRequest {
    core::Street street = core::Street::Preflop;
    std::vector<std::uint8_t> canonical_board;
};

[[nodiscard]] bool is_multiway_canonical_board(
    core::Street street,
    const std::vector<std::uint8_t>& board) noexcept;
[[nodiscard]] MultiwayBucketFeatures make_multiway_bucket_features(
    core::Street street,
    const std::vector<std::uint8_t>& canonical_board,
    const std::array<std::uint8_t, 2>& hole);
[[nodiscard]] std::uint32_t assign_multiway_baseline_bucket(
    const MultiwayBucketFeatures& features,
    const MultiwayBucketBaselineProfile& profile,
    core::Street street);

[[nodiscard]] MultiwayBucketTable build_multiway_baseline_bucket_table(
    const MultiwayModelIdentity& identity,
    core::Street street,
    std::vector<std::uint8_t> canonical_board,
    const MultiwayBucketBaselineProfile& profile = MultiwayBucketBaselineProfile::standard());
// Fixed-board variant for catalog generation. The caller supplies a validated
// sorted board of the street's exact length.
[[nodiscard]] MultiwayBucketTable build_multiway_baseline_bucket_table_fixed_board(
    const MultiwayModelIdentity& identity,
    core::Street street,
    const std::array<std::uint8_t, 5U>& canonical_board,
    const MultiwayBucketBaselineProfile& profile = MultiwayBucketBaselineProfile::standard());
[[nodiscard]] MultiwayBucketRegistry build_multiway_baseline_bucket_registry(
    const MultiwayModelIdentity& identity,
    const std::vector<MultiwayBucketBoardRequest>& boards,
    const MultiwayBucketBaselineProfile& profile = MultiwayBucketBaselineProfile::standard());

// Verifies an artifact contains every requested canonical board exactly once.
// It is an offline load/build check and is never used by traversal.
void validate_multiway_bucket_coverage(
    const MultiwayBucketRegistry& registry,
    const std::vector<MultiwayBucketBoardRequest>& required_boards);

[[nodiscard]] std::vector<std::uint8_t> serialize_multiway_bucket_registry(
    const MultiwayBucketRegistry& registry);
[[nodiscard]] MultiwayBucketRegistry deserialize_multiway_bucket_registry(
    const std::vector<std::uint8_t>& bytes);
// Streaming load avoids materializing a second serialized copy alongside the
// immutable registry required by traversal.
[[nodiscard]] MultiwayBucketRegistry load_multiway_bucket_registry(
    const std::filesystem::path& path);

struct MultiwayBucketArtifactInspection {
    std::uint64_t flop_tables = 0U;
    std::uint64_t turn_tables = 0U;
    std::uint64_t river_tables = 0U;
    std::uint64_t live_assignments = 0U;
    std::uint64_t payload_hash = 0U;
    MultiwayModelIdentity identity{};
};

[[nodiscard]] MultiwayBucketArtifactInspection inspect_multiway_bucket_artifact(
    const std::filesystem::path& path,
    const MultiwayModelIdentity& expected_identity);

}  // namespace texas::solver::multiway
