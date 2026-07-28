#pragma once

#include "core/types.hpp"
#include "games/multiway_state.hpp"

#include <cstdint>
#include <vector>

namespace core {

// A narrow non-owning depth-limit boundary. The caller owns model state and
// supplies a plain function pointer so traversal does not use std::function
// or virtual dispatch in its hot path.
struct MultiwayLeafEvaluationRequest {
    const MultiwayBettingSnapshot* betting = nullptr;
    const std::vector<std::uint8_t>* board = nullptr;
    PlayerId traverser = -1;
};

using MultiwayLeafEvaluateFn = Value (*)(
    const MultiwayLeafEvaluationRequest& request,
    const void* context) noexcept;

struct MultiwayLeafEvaluator {
    MultiwayLeafEvaluateFn evaluate = nullptr;
    const void* context = nullptr;

    [[nodiscard]] bool valid() const noexcept { return evaluate != nullptr; }
    [[nodiscard]] Value operator()(const MultiwayLeafEvaluationRequest& request) const noexcept {
        return evaluate(request, context);
    }
};

}  // namespace core
