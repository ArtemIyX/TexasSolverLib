#pragma once

#include "core/core.hpp"
#include "core/hunl.hpp"
#include "core/preflop_equity.hpp"
#include "core/solver.hpp"

#include <unordered_map>

namespace core {

struct PreflopRvrOutput {
    SolveOutput base;
    std::uint32_t decision_node_count = 0;
};

PreflopRvrOutput solve_hunl_preflop_rvr(
    const HUNLConfig& config,
    const PreflopEquityTable& table,
    std::uint32_t iterations,
    double alpha,
    double beta,
    double gamma);

}  // namespace core
