#include "solver/multiway_continuation_policy.hpp"

#include <cmath>
#include <limits>

namespace core {
namespace {

bool is_target(
    MultiwayContinuationPolicyKind kind,
    MultiwayAction action) noexcept {
    switch (kind) {
        case MultiwayContinuationPolicyKind::Blueprint:
            return false;
        case MultiwayContinuationPolicyKind::FoldBiased:
            return action == MultiwayAction::Fold;
        case MultiwayContinuationPolicyKind::CallBiased:
            return action == MultiwayAction::Check || action == MultiwayAction::Call;
        case MultiwayContinuationPolicyKind::RaiseBiased:
            return action == MultiwayAction::Bet || action == MultiwayAction::Raise ||
                   action == MultiwayAction::AllIn;
    }
    return false;
}

bool valid_kind(MultiwayContinuationPolicyKind kind) noexcept {
    return kind == MultiwayContinuationPolicyKind::Blueprint ||
           kind == MultiwayContinuationPolicyKind::FoldBiased ||
           kind == MultiwayContinuationPolicyKind::CallBiased ||
           kind == MultiwayContinuationPolicyKind::RaiseBiased;
}

}  // namespace

bool MultiwayFixedContinuationPolicy::apply(
    MultiwayContinuationPolicyKind kind,
    const MultiwayActionDescriptor* actions,
    const Probability* blueprint,
    std::size_t action_count,
    Probability* output,
    Probability bias_factor) noexcept {
    if (!valid_kind(kind) || actions == nullptr || blueprint == nullptr || output == nullptr ||
        action_count == 0U || !std::isfinite(bias_factor) || bias_factor <= 0.0) {
        return false;
    }
    Probability total = 0.0;
    for (std::size_t action = 0; action < action_count; ++action) {
        const auto base = blueprint[action];
        if (!std::isfinite(base) || base < 0.0 || base > 1.0) return false;
        const auto weight = is_target(kind, actions[action].action) ? bias_factor : 1.0;
        output[action] = base * weight;
        total += output[action];
    }
    if (!std::isfinite(total) || total <= 0.0) return false;
    const auto inverse = 1.0 / total;
    Probability assigned = 0.0;
    for (std::size_t action = 0; action + 1U < action_count; ++action) {
        output[action] *= inverse;
        assigned += output[action];
    }
    output[action_count - 1U] = 1.0 - assigned;
    return std::isfinite(output[action_count - 1U]) && output[action_count - 1U] >= 0.0;
}

bool MultiwayFixedContinuationPolicy::evaluate_leaf(
    MultiwayContinuationPolicyKind kind,
    const MultiwayActionDescriptor* actions,
    const Probability* blueprint,
    const Value* action_values,
    std::size_t action_count,
    Value* output,
    Probability bias_factor) noexcept {
    if (!valid_kind(kind) || actions == nullptr || blueprint == nullptr ||
        action_values == nullptr || output == nullptr || action_count == 0U ||
        !std::isfinite(bias_factor) || bias_factor <= 0.0) {
        return false;
    }
    Probability total = 0.0;
    Value weighted_value = 0.0;
    for (std::size_t action = 0; action < action_count; ++action) {
        if (!std::isfinite(blueprint[action]) || blueprint[action] < 0.0 ||
            blueprint[action] > 1.0 || !std::isfinite(action_values[action])) {
            return false;
        }
        const auto weight = blueprint[action] *
            (is_target(kind, actions[action].action) ? bias_factor : 1.0);
        total += weight;
        weighted_value += weight * action_values[action];
    }
    if (!std::isfinite(total) || total <= 0.0 || !std::isfinite(weighted_value)) return false;
    *output = weighted_value / total;
    return std::isfinite(*output);
}

Value evaluate_multiway_fixed_continuation_leaf(
    const MultiwayLeafEvaluationRequest& request,
    const void* context) noexcept {
    const auto invalid = std::numeric_limits<Value>::quiet_NaN();
    if (context == nullptr) return invalid;
    const auto& leaf = *static_cast<const MultiwayContinuationLeafContext*>(context);
    if (leaf.provide == nullptr) return invalid;
    MultiwayContinuationLeafData data;
    if (!leaf.provide(request, &data, leaf.provider_context)) return invalid;
    Value value = invalid;
    if (!MultiwayFixedContinuationPolicy::evaluate_leaf(
            leaf.policy,
            data.actions,
            data.blueprint,
            data.action_values,
            data.action_count,
            &value,
            leaf.bias_factor)) {
        return invalid;
    }
    return value;
}

MultiwayLeafEvaluator make_multiway_fixed_continuation_leaf_evaluator(
    const MultiwayContinuationLeafContext* context) noexcept {
    if (context == nullptr) return {};
    return {evaluate_multiway_fixed_continuation_leaf, context};
}

}  // namespace core
