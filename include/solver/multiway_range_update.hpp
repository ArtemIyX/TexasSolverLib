#pragma once

#include "games/multiway_private.hpp"
#include "solver/multiway_bucket_model.hpp"

#include <cstdint>
#include <vector>

namespace core {

// Quantized action-major policy for every bucket at one public state.
struct MultiwayBucketActionPolicy {
    MultiwayModelIdentity identity{};
    MultiwayPublicStateId public_state{};
    std::uint64_t action_menu_id = 0;
    std::uint32_t bucket_count = 0;
    std::uint8_t action_count = 0;
    std::vector<std::uint16_t> probabilities;

    void validate() const;
    [[nodiscard]] Probability likelihood(std::uint32_t bucket, std::uint8_t action) const;
};

// Uses no player IDs or cross-hand data. Zero-likelihood actions receive a
// caller-provided floor so unusual off-tree observations do not collapse all
// compatible private hands.
[[nodiscard]] std::vector<MultiwayWeightedHole> update_anonymous_multiway_range(
    const std::vector<MultiwayWeightedHole>& prior,
    const MultiwayBucketTable& table,
    const MultiwayBucketActionPolicy& policy,
    std::uint8_t observed_action,
    Probability likelihood_floor = 1e-6);

}  // namespace core
