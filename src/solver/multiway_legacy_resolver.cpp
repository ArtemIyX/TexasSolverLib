#include "solver/multiway_legacy_resolver.hpp"
#include "solver/multiway_resolver_policy.hpp"

#include <cstddef>
#include <stdexcept>

namespace texas::solver::multiway {
namespace {

std::uint64_t mix_seed(std::uint64_t value) noexcept {
    value += 0x9e3779b97f4a7c15ULL;
    value = (value ^ (value >> 30U)) * 0xbf58476d1ce4e5b9ULL;
    value = (value ^ (value >> 27U)) * 0x94d049bb133111ebULL;
    return value ^ (value >> 31U);
}

double unit_random(std::uint64_t seed) noexcept {
    return static_cast<double>(mix_seed(seed) >> 11U) * (1.0 / 9007199254740992.0);
}

}  // namespace

void apply_legacy_deterministic_adjustment(
    std::vector<MultiwayResolverActionProbability>* policy,
    std::uint64_t sampling_seed,
    std::uint64_t range_hash,
    std::uint64_t public_state_id,
    std::uint32_t bucket,
    std::uint32_t batch) {
    for (std::size_t action = 0U; action < policy->size(); ++action) {
        const auto adjustment = 0.75 + 0.5 * unit_random(
            sampling_seed ^ range_hash ^ public_state_id ^
            (static_cast<std::uint64_t>(batch) << 32U) ^ action ^ bucket);
        (*policy)[action].probability = 0.9 * (*policy)[action].probability + 0.1 * adjustment;
    }
    if (!normalize_multiway_resolver_policy(policy)) {
        throw std::logic_error("legacy deterministic resolver adjustment produced a non-finite policy");
    }
}

}  // namespace texas::solver::multiway
