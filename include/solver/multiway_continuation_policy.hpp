#pragma once

#include "solver/multiway_continuation_policy_kind.hpp"
#include "solver/multiway_leaf_evaluator.hpp"
#include "solver/multiway_solver.hpp"

#include <array>
#include <cstddef>
#include <cstdint>

namespace core {

// Allocation-free fixed-policy kernel. Input and output may alias. The base
// probabilities are the anonymous blueprint continuation for this infoset.
class MultiwayFixedContinuationPolicy {
public:
    [[nodiscard]] static bool apply(
        MultiwayContinuationPolicyKind kind,
        const MultiwayActionDescriptor* actions,
        const Probability* blueprint,
        std::size_t action_count,
        Probability* output,
        Probability bias_factor = 5.0) noexcept;

    [[nodiscard]] static bool evaluate_leaf(
        MultiwayContinuationPolicyKind kind,
        const MultiwayActionDescriptor* actions,
        const Probability* blueprint,
        const Value* action_values,
        std::size_t action_count,
        Value* output,
        Probability bias_factor = 5.0) noexcept;
};

// Caller-owned non-owning leaf inputs. The provider may point these views at
// immutable blueprint/model storage or preallocated worker scratch.
struct MultiwayContinuationLeafData {
    const MultiwayActionDescriptor* actions = nullptr;
    const Probability* blueprint = nullptr;
    const Value* action_values = nullptr;
    std::size_t action_count = 0;
};

using MultiwayContinuationLeafProviderFn = bool (*)(
    const MultiwayLeafEvaluationRequest& request,
    MultiwayContinuationLeafData* output,
    const void* context) noexcept;

struct MultiwayContinuationLeafContext {
    // Used by direct callers without traversal provenance. Traversal supplies
    // the information-set-selected mode in MultiwayLeafEvaluationRequest.
    MultiwayContinuationPolicyKind policy = MultiwayContinuationPolicyKind::Blueprint;
    MultiwayContinuationLeafProviderFn provide = nullptr;
    const void* provider_context = nullptr;
    Probability bias_factor = 5.0;
};

// Adapter for the production MultiwayLeafEvaluator boundary. Invalid provider
// output becomes NaN and is rejected by traversal's finite-value check.
[[nodiscard]] Value evaluate_multiway_fixed_continuation_leaf(
    const MultiwayLeafEvaluationRequest& request,
    const void* context) noexcept;

// The caller owns the context and all provider-returned views. The context
// must outlive every evaluator invocation; the provider views need only remain
// valid for the synchronous invocation that requested them.
[[nodiscard]] MultiwayLeafEvaluator make_multiway_fixed_continuation_leaf_evaluator(
    const MultiwayContinuationLeafContext* context) noexcept;

}  // namespace core
