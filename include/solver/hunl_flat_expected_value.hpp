#pragma once

#include "games/hunl_flat_graph.hpp"
#include "solver/hunl_flat_state.hpp"

#include <array>
#include <string>
#include <unordered_map>
#include <vector>

namespace core {

struct HUNLFlatAverageStrategyView {
    std::vector<const double*> rows_by_infoset;
    const std::vector<HUNLFlatInfosetTableMeta>* meta = nullptr;
    HUNLFlatValueLayout layout = HUNLFlatValueLayout::InfosetHandAction;
};

struct HUNLFlatAverageStrategyTable {
    HUNLAlignedVector<double> values;
    std::vector<HUNLFlatInfosetTableMeta> meta;
    HUNLFlatValueLayout layout = HUNLFlatValueLayout::InfosetHandAction;

    [[nodiscard]] HUNLFlatAverageStrategyView view() const;
};

using HUNLFlatTerminalValueTable = std::vector<std::array<double, 2>>;

[[nodiscard]] HUNLFlatAverageStrategyTable build_flat_average_strategy_table(
    const HUNLFlatSolveGraph& graph,
    const std::unordered_map<std::string, std::vector<double>>& average_strategy,
    HUNLFlatValueLayout layout = HUNLFlatValueLayout::InfosetHandAction);

[[nodiscard]] HUNLFlatTerminalValueTable build_flat_terminal_value_table(const HUNLFlatSolveGraph& graph);

std::array<double, 2> compute_flat_expected_value(
    const HUNLFlatSolveGraph& graph,
    const HUNLFlatAverageStrategyView& average_strategy,
    const HUNLFlatTerminalValueTable* terminal_values = nullptr);

std::array<double, 2> compute_flat_expected_value(
    const HUNLFlatSolveGraph& graph,
    const std::unordered_map<std::string, std::vector<double>>& average_strategy);

}  // namespace core
