#pragma once

#include "solver/multiway_resolver.hpp"

#include <cstdint>
#include <vector>

namespace texas::solver::multiway {

// Legacy-only deterministic adjustment. It is not runtime search.
void apply_legacy_deterministic_adjustment(
    std::vector<MultiwayResolverActionProbability>* policy,
    std::uint64_t sampling_seed,
    std::uint64_t range_hash,
    std::uint64_t public_state_id,
    std::uint32_t bucket,
    std::uint32_t batch);

}  // namespace texas::solver::multiway
