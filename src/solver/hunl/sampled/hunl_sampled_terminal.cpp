#include "solver/hunl/sampled/hunl_sampled_terminal.hpp"

namespace texas::solver::hunl {

double HUNLSampledTerminalEvaluator::evaluate_terminal(
    const HUNLSampledTerminalInput& input,
    const std::array<double, 2>& terminal_utility) const noexcept {
    if (input.traversing_player < 0 || input.traversing_player > 1) {
        return 0.0;
    }
    return terminal_utility[static_cast<std::size_t>(input.traversing_player)];
}

double HUNLSampledTerminalEvaluator::evaluate_fold(
    const HUNLSampledTerminalInput& input,
    PlayerId folding_player) const noexcept {
    const auto winner = folding_player == 0 ? 1 : 0;
    return input.traversing_player == winner
        ? static_cast<double>(input.contributions[folding_player])
        : -static_cast<double>(input.contributions[input.traversing_player]);
}

double HUNLSampledTerminalEvaluator::evaluate_showdown(
    const HUNLSampledTerminalInput& input,
    double showdown_utility) const noexcept {
    const auto sign = input.traversing_player == 0 ? 1.0 : -1.0;
    return sign * showdown_utility;
}

}  // namespace texas::solver::hunl
