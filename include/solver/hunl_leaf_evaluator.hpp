#pragma once

#include "games/hunl.hpp"

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace texas::solver::hunl {

enum class HUNLLeafValueUnits : std::uint8_t { Chips, BigBlinds, PotFraction, NormalizedStackFraction };
enum class HUNLLeafEvaluationScope : std::uint8_t { DealConditional };

// Boundary contract for a future batched exact/value-network leaf backend.
// A production solver must use this contract for every depth cutoff or reject
// the cutoff request; it must never substitute a backend-specific heuristic.
struct HUNLLeafEvaluationRequest {
    HUNLState public_state;
    // Exact sampled private deal. public_state deliberately remains private-card free.
    std::array<std::array<std::uint8_t, 2>, 2> private_hole_cards = {};
    std::array<std::vector<double>, 2> bucket_reach;
    HUNLLeafEvaluationScope scope = HUNLLeafEvaluationScope::DealConditional;
    HUNLLeafValueUnits units = HUNLLeafValueUnits::Chips;
    // A timed solve supplies its cooperative cancellation deadline. Backends
    // must return promptly once it is reached.
    std::optional<std::chrono::steady_clock::time_point> deadline = std::nullopt;
    std::string abstraction_version;
    std::string model_version;
};

struct HUNLLeafEvaluationResult {
    std::array<double, 2> values = {0.0, 0.0};
    HUNLLeafValueUnits units = HUNLLeafValueUnits::Chips;
    HUNLLeafEvaluationScope scope = HUNLLeafEvaluationScope::DealConditional;
    // A backend must acknowledge every submitted request. This lets callers
    // reject a successful callback that left part of the output batch unset.
    bool populated = false;
    std::string abstraction_version;
    std::string model_version;
};

// A non-owning, non-virtual batch callback.  Callers submit requests in
// deterministic traversal order and must write one same-unit result per
// request. Timed callbacks must observe each request deadline. Returning false
// rejects the solve at its last completed batch; there is no heuristic fallback
// for a failed depth-limited value lookup.
using HUNLLeafEvaluateBatchFn = bool (*) (
    void* context,
    const HUNLLeafEvaluationRequest* requests,
    HUNLLeafEvaluationResult* results,
    std::size_t count);

struct HUNLLeafEvaluator {
    void* context = nullptr;
    HUNLLeafEvaluateBatchFn evaluate_batch = nullptr;

    [[nodiscard]] bool valid() const noexcept {
        return evaluate_batch != nullptr;
    }
};

}  // namespace texas::solver::hunl
