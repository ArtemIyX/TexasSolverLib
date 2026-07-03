#pragma once

#include "core/types.hpp"

#include <array>

namespace core {

struct HUNLSampledTerminalInput {
    std::array<int, 2> contributions = {0, 0};
    PlayerId traversing_player = 0;
};

class HUNLSampledTerminalEvaluator {
public:
    [[nodiscard]] double evaluate_fold(
        const HUNLSampledTerminalInput& input,
        PlayerId folding_player) const noexcept;
    [[nodiscard]] double evaluate_showdown(
        const HUNLSampledTerminalInput& input,
        double showdown_utility) const noexcept;
};

}  // namespace core
