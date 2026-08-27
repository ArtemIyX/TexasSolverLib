#pragma once

#include "core/legacy_namespace_compat.hpp"

#include "core/types.hpp"
#include "games/hunl.hpp"
#include "preflop/preflop_equity.hpp"
#include "solver/generic/solver.hpp"

namespace texas::preflop {

/**
 * @brief Preflop solver output with runtime metadata.
 */
struct PreflopSolveOutput {
    SolveOutput base;
    std::uint32_t infoset_count = 0;
    double wallclock_seconds = 0.0;
};

/**
 * @brief Solve the HUNL preflop subgame.
 */
PreflopSolveOutput solve_hunl_preflop(const HUNLConfig& config, std::uint32_t iterations, double alpha, double beta, double gamma);

}  // namespace texas::preflop


