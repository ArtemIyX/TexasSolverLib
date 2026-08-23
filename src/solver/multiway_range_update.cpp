#include "solver/multiway_range_update.hpp"

#include <limits>
#include <stdexcept>

namespace texas::solver::multiway {

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

}  // namespace texas::solver::multiway
