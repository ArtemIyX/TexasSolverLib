#pragma once

/** @file
 * @brief Flat-graph average-strategy and terminal expected-value helpers.
 */

#include "games/hunl_flat_graph.hpp"
#include "solver/hunl/flat/hunl_flat_state.hpp"

#include <array>
#include <string>
#include <unordered_map>
#include <vector>

namespace texas::solver::hunl
{

	/** @brief Non-owning row view over an exported flat average-strategy table. */
	struct HUNLFlatAverageStrategyView
	{
		std::vector<const double*> rows_by_infoset; ///< Strategy row pointer indexed by information set.
		const std::vector<HUNLFlatInfosetTableMeta>* meta = nullptr; ///< Row shape metadata owned by the source table.
		HUNLFlatValueLayout layout = HUNLFlatValueLayout::InfosetHandAction; ///< Memory order represented by row pointers.
	};

	/** @brief Owning average-strategy data and metadata for flat evaluation. */
	struct HUNLFlatAverageStrategyTable
	{
		HUNLAlignedVector<double> values; ///< Contiguous strategy values.
		std::vector<HUNLFlatInfosetTableMeta> meta; ///< Per-information-set offsets and shapes.
		HUNLFlatValueLayout layout = HUNLFlatValueLayout::InfosetHandAction; ///< Value layout.

		/** @brief Creates a non-owning view into this table's rows. */
		[[nodiscard]] HUNLFlatAverageStrategyView view() const;
	};

	/** @brief Terminal utility pair indexed by flat graph node. */
	using HUNLFlatTerminalValueTable = std::vector<std::array<double, 2>>;
	/** @brief Player-zero terminal utility indexed by flat graph node. */
	using HUNLFlatTerminalValueTableP0 = std::vector<double>;

	/** @brief Packs a key-based average strategy into a flat table with the requested layout. */
	[[nodiscard]] HUNLFlatAverageStrategyTable build_flat_average_strategy_table(
		const HUNLFlatSolveGraph& graph,
		const std::unordered_map<std::string, std::vector<double>>& average_strategy,
		HUNLFlatValueLayout layout = HUNLFlatValueLayout::InfosetHandAction);

	/** @brief Builds per-node terminal utility pairs from the graph. */
	[[nodiscard]] HUNLFlatTerminalValueTable build_flat_terminal_value_table(const HUNLFlatSolveGraph& graph);
	/** @brief Builds a player-zero-only terminal table for benchmark evaluation. */
	[[nodiscard]] HUNLFlatTerminalValueTableP0 build_flat_terminal_value_table_p0_for_benchmark(
		const HUNLFlatSolveGraph& graph);

	/** @brief Evaluates both players under a flat strategy view. */
	std::array<double, 2> compute_flat_expected_value(
		const HUNLFlatSolveGraph& graph,
		const HUNLFlatAverageStrategyView& average_strategy,
		const HUNLFlatTerminalValueTable* terminal_values = nullptr);

	/** @brief Evaluates player zero with the benchmark terminal-value representation. */
	double compute_flat_expected_value_p0_benchmark(
		const HUNLFlatSolveGraph& graph,
		const HUNLFlatAverageStrategyView& average_strategy,
		const HUNLFlatTerminalValueTableP0& terminal_values_p0);

	/** @brief Evaluates both players using a key-based average strategy. */
	std::array<double, 2> compute_flat_expected_value(
		const HUNLFlatSolveGraph& graph,
		const std::unordered_map<std::string, std::vector<double>>& average_strategy);

} // namespace texas::solver::hunl
