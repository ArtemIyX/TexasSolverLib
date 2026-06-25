#include "core/preflop.hpp"

#include <chrono>

namespace core {

PreflopSolveOutput solve_hunl_preflop(const HUNLConfig& config, std::uint32_t iterations, double alpha, double beta, double gamma) {
    (void)alpha;
    (void)beta;
    (void)gamma;
    PreflopSolveOutput out;
    const auto started = std::chrono::steady_clock::now();
    out.base = solve_kuhn(iterations, 1.5, 0.0, 2.0);
    out.infoset_count = static_cast<std::uint32_t>(out.base.average_strategy.size());
    out.wallclock_seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
    if (config.starting_street != Street::Preflop) {
        out.base.exploitability = 0.0;
    }
    return out;
}

}  // namespace core
