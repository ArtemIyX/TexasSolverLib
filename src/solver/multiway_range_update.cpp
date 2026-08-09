#include "solver/multiway_range_update.hpp"

#include <cmath>
#include <limits>
#include <stdexcept>

namespace core {

void MultiwayBucketActionPolicy::validate() const {
    identity.validate();
    if (public_state.value == 0U || action_menu_id == 0U || bucket_count == 0U || action_count == 0U ||
        probabilities.size() != static_cast<std::size_t>(bucket_count) * action_count) {
        throw std::invalid_argument("multiway bucket action policy has invalid metadata");
    }
    for (std::uint32_t bucket = 0; bucket < bucket_count; ++bucket) {
        std::uint64_t total = 0;
        for (std::uint8_t action = 0; action < action_count; ++action) {
            total += probabilities[static_cast<std::size_t>(action) * bucket_count + bucket];
        }
        if (total != std::numeric_limits<std::uint16_t>::max()) {
            throw std::invalid_argument("multiway bucket action policy is not normalized");
        }
    }
}

Probability MultiwayBucketActionPolicy::likelihood(std::uint32_t bucket, std::uint8_t action) const {
    if (bucket >= bucket_count || action >= action_count) {
        throw std::out_of_range("multiway bucket action policy index is unavailable");
    }
    return static_cast<Probability>(probabilities[static_cast<std::size_t>(action) * bucket_count + bucket]) /
        std::numeric_limits<std::uint16_t>::max();
}

std::vector<MultiwayWeightedHole> update_anonymous_multiway_range(
    const std::vector<MultiwayWeightedHole>& prior,
    const MultiwayBucketTable& table,
    const MultiwayBucketActionPolicy& policy,
    std::uint8_t observed_action,
    Probability likelihood_floor) {
    policy.validate();
    if (policy.identity != table.identity() || policy.bucket_count != table.bucket_count() ||
        observed_action >= policy.action_count || !std::isfinite(likelihood_floor) ||
        likelihood_floor <= 0.0 || likelihood_floor > 1.0) {
        throw std::invalid_argument("multiway anonymous range update has incompatible policy data");
    }
    std::vector<MultiwayWeightedHole> result;
    result.reserve(prior.size());
    double total = 0.0;
    for (const auto& weighted : prior) {
        if (!std::isfinite(weighted.weight) || weighted.weight < 0.0) {
            throw std::invalid_argument("multiway anonymous range has invalid prior weight");
        }
        const auto likelihood = std::max(likelihood_floor,
            policy.likelihood(table.lookup_hunl(weighted.hole), observed_action));
        const auto posterior = weighted.weight * likelihood;
        if (!std::isfinite(posterior)) throw std::overflow_error("multiway anonymous range update overflowed");
        result.push_back({weighted.hole, posterior});
        total += posterior;
    }
    if (!std::isfinite(total) || total <= 0.0) {
        throw std::invalid_argument("multiway anonymous range has no compatible posterior mass");
    }
    for (auto& weighted : result) weighted.weight /= total;
    return result;
}

}  // namespace core
