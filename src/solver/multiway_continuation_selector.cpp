#include "solver/multiway_continuation_selector.hpp"

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
    return policy_;
}

}  // namespace texas::solver::multiway
