#pragma once

#include "core/types.hpp"
#include "games/hunl.hpp"
#include "preflop/preflop_equity.hpp"
#include "solver/solver.hpp"

namespace core {

struct PreflopSolveOutput {
    SolveOutput base;
    std::uint32_t infoset_count = 0;
    double wallclock_seconds = 0.0;
};

PreflopSolveOutput solve_hunl_preflop(const HUNLConfig& config, std::uint32_t iterations, double alpha, double beta, double gamma);

}  // namespace core


