#pragma once

#include "core/namespaces.hpp"

#include "core/types.hpp"

#include <array>

namespace texas::solver::hunl {

struct HUNLSampledTerminalInput {
    std::array<int, 2> contributions = {0, 0};
    PlayerId traversing_player = 0;
};

class HUNLSampledTerminalEvaluator {
public:
    [[nodiscard]] double evaluate_terminal(
        const HUNLSampledTerminalInput& input,
        const std::array<double, 2>& terminal_utility) const noexcept;
    [[nodiscard]] double evaluate_fold(
        const HUNLSampledTerminalInput& input,
        PlayerId folding_player) const noexcept;
    [[nodiscard]] double evaluate_showdown(
        const HUNLSampledTerminalInput& input,
        double showdown_utility) const noexcept;
};

}  // namespace texas::solver::hunl
