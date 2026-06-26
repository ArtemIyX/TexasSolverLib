#include "preflop/preflop.hpp"

#include "solver/dcfr.hpp"

#include <chrono>
#include <memory>
#include <stdexcept>

namespace core {

PreflopSolveOutput solve_hunl_preflop(
    const HUNLConfig& config,
    std::uint32_t iterations,
    double alpha,
    double beta,
    double gamma) {
    if (config.starting_street != Street::Preflop) {
        throw std::invalid_argument("solve_hunl_preflop requires starting_street = Preflop");
    }
    if (!config.initial_hole_cards.has_value()) {
        throw std::invalid_argument("solve_hunl_preflop requires initial_hole_cards to be set");
    }
    if (config.rake_rate != 0.0 || config.rake_cap != 0) {
        throw std::invalid_argument("solve_hunl_preflop does not support rake");
    }

    auto shared = std::make_shared<const HUNLConfig>(config);
    const auto root = HUNLState::initial(shared);

    const auto started = std::chrono::steady_clock::now();
    DCFRSolver<HUNLState> solver(DCFRConfig{alpha, beta, gamma}, root);
    const auto solve_output = solver.solve(iterations);
    const auto elapsed = std::chrono::steady_clock::now() - started;

    PreflopSolveOutput out;
    out.base = std::move(solve_output);
    out.infoset_count = static_cast<std::uint32_t>(out.base.average_strategy.size());
    out.wallclock_seconds = std::chrono::duration<double>(elapsed).count();
    return out;
}

}  // namespace core


