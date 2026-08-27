#include "solver/multiway/resolver/multiway_resolver_policy.hpp"

#include <cmath>

namespace texas::solver::multiway {

bool normalize_multiway_resolver_policy(
    std::vector<MultiwayResolverActionProbability>* policy) noexcept {
    if (policy == nullptr || policy->empty()) return false;
    double total = 0.0;
    for (const auto& entry : *policy) {
        if (!std::isfinite(entry.probability) || entry.probability < 0.0) return false;
        total += entry.probability;
    }
    if (!std::isfinite(total) || total <= 0.0) return false;
    for (auto& entry : *policy) entry.probability /= total;
    return true;
}

}  // namespace texas::solver::multiway
