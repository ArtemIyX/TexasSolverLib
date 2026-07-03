#include "solver/hunl_sampled_terminal.hpp"

namespace core {

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

}  // namespace core
