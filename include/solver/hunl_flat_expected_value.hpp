#pragma once

#include "games/hunl_flat_graph.hpp"

#include <array>
#include <string>
#include <unordered_map>
#include <vector>

namespace core {

std::array<double, 2> compute_flat_expected_value(
    const HUNLFlatSolveGraph& graph,
    const std::unordered_map<std::string, std::vector<double>>& average_strategy);

}  // namespace core
