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

struct Class169Combos {
    std::array<std::vector<std::array<std::uint8_t, 2>>, PREFLOP_NUM_CLASSES> combos;

    static Class169Combos build();
};

std::size_t classify_suit_variant(const std::array<std::uint8_t, 2>& hero, const std::array<std::uint8_t, 2>& villain);
std::array<std::vector<double>, 2> build_class169_blocker_mass(const Class169Combos& combos);

PreflopRvrOutput solve_hunl_preflop_rvr(
    const HUNLConfig& config,
    const PreflopEquityTable& table,
    std::uint32_t iterations,
    double alpha,
    double beta,
    double gamma);

}  // namespace core
