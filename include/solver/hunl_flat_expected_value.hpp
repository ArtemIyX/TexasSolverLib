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
using HUNLFlatTerminalValueTableP0 = std::vector<double>;

[[nodiscard]] HUNLFlatAverageStrategyTable build_flat_average_strategy_table(
    const HUNLFlatSolveGraph& graph,
    const std::unordered_map<std::string, std::vector<double>>& average_strategy,
    HUNLFlatValueLayout layout = HUNLFlatValueLayout::InfosetHandAction);

[[nodiscard]] HUNLFlatTerminalValueTable build_flat_terminal_value_table(const HUNLFlatSolveGraph& graph);
[[nodiscard]] HUNLFlatTerminalValueTableP0 build_flat_terminal_value_table_p0_for_benchmark(
    const HUNLFlatSolveGraph& graph);

std::array<double, 2> compute_flat_expected_value(
    const HUNLFlatSolveGraph& graph,
    const HUNLFlatAverageStrategyView& average_strategy,
    const HUNLFlatTerminalValueTable* terminal_values = nullptr);

double compute_flat_expected_value_p0_benchmark(
    const HUNLFlatSolveGraph& graph,
    const HUNLFlatAverageStrategyView& average_strategy,
    const HUNLFlatTerminalValueTableP0& terminal_values_p0);

std::array<double, 2> compute_flat_expected_value(
    const HUNLFlatSolveGraph& graph,
    const std::unordered_map<std::string, std::vector<double>>& average_strategy);

}  // namespace core
