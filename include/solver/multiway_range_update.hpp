#pragma once

#include "core/namespaces.hpp"

#include "games/multiway_private.hpp"
#include "solver/multiway_bucket_model.hpp"
#include "solver/multiway_solver.hpp"

#include <cstdint>
#include <vector>

namespace texas::solver::multiway {

// Quantized action-major policy for every bucket at one public state.
struct MultiwayBucketActionPolicy {
    MultiwayModelIdentity identity{};
    MultiwayPublicStateId public_state{};
    std::uint64_t action_menu_id = 0;
    // Optional for legacy policies. RangeBelief observations require this to
    // equal the bound MultiwayBucketTable::table_identity().
    std::uint64_t bucket_table_identity = 0;
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

}  // namespace texas::solver::multiway
