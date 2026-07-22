#pragma once

#include "games/hunl.hpp"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace core {

enum class HUNLLeafValueUnits : std::uint8_t { Chips, BigBlinds, PotFraction, NormalizedStackFraction };

// Boundary contract for a future batched exact/value-network leaf backend.
// A production solver must use this contract for every depth cutoff or reject
// the cutoff request; it must never substitute a backend-specific heuristic.
struct HUNLLeafEvaluationRequest {
    HUNLState public_state;
    std::array<std::vector<double>, 2> bucket_reach;
    HUNLLeafValueUnits units = HUNLLeafValueUnits::Chips;
    std::string abstraction_version;
    std::string model_version;
};

struct HUNLLeafEvaluationResult {
    std::array<double, 2> values = {0.0, 0.0};
    HUNLLeafValueUnits units = HUNLLeafValueUnits::Chips;
};

}  // namespace core
