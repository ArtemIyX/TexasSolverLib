#include "solver/multiway_continuation_selector.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace texas::solver::multiway {

bool MultiwayContinuationSelectionKey::valid() const noexcept {
    return public_state.value != 0U && actor >= 0 && street >= Street::Flop &&
           street <= Street::River && action_abstraction_version != 0U &&
           leaf_model_version != 0U;
}

bool MultiwayFixedContinuationSelector::valid() const noexcept {
    return is_valid_multiway_continuation_policy(policy_);
}

MultiwayContinuationPolicyKind MultiwayFixedContinuationSelector::select(
    const MultiwayContinuationSelectionKey& key) const noexcept {
    if (!valid() || !key.valid()) return MultiwayContinuationPolicyKind::Blueprint;
    const auto found = std::find_if(rows_.begin(), rows_.end(), [&key](const Row& row) {
        return row.key.public_state == key.public_state && row.key.actor == key.actor &&
            row.key.street == key.street && row.key.future_bucket == key.future_bucket &&
            row.key.action_abstraction_version == key.action_abstraction_version &&
            row.key.leaf_model_version == key.leaf_model_version;
    });
    if (found != rows_.end()) {
        double total = 0.0;
        std::size_t best = 0U;
        double best_value = 0.0;
        for (std::size_t index = 0U; index < found->regrets.size(); ++index) {
            const auto regret = std::max(0.0, found->regrets[index]);
            total += regret;
            if (regret > best_value) {
                best_value = regret;
                best = index;
            }
        }
        if (total > 0.0 && std::isfinite(total)) {
            // The selector boundary is deterministic. Sampling/mixing is
            // performed by the traversal using this regret-matched choice.
            return MULTIWAY_FIXED_CONTINUATION_POLICIES[best];
        }
    }
    return policy_;
}

void MultiwayFixedContinuationSelector::set_regrets(
    const MultiwayContinuationSelectionKey& key,
    const std::array<double, MULTIWAY_FIXED_CONTINUATION_POLICIES.size()>& regrets) {
    if (!key.valid()) throw std::invalid_argument("multiway continuation row key is invalid");
    for (const auto regret : regrets) {
        if (!std::isfinite(regret)) throw std::invalid_argument("multiway continuation regret is non-finite");
    }
    const auto found = std::find_if(rows_.begin(), rows_.end(), [&key](const Row& row) {
        return row.key.public_state == key.public_state && row.key.actor == key.actor &&
            row.key.street == key.street && row.key.future_bucket == key.future_bucket &&
            row.key.action_abstraction_version == key.action_abstraction_version &&
            row.key.leaf_model_version == key.leaf_model_version;
    });
    if (found == rows_.end()) rows_.push_back({key, regrets});
    else found->regrets = regrets;
}

}  // namespace texas::solver::multiway
