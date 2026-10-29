#pragma once

/** @file
 * @brief Converts fold and showdown utilities into traversing-player values.
 */

#include "core/legacy_namespace_compat.hpp"

#include "core/types.hpp"

#include <array>

namespace texas::solver::hunl
{

	/** @brief Contribution and traversal context for one sampled terminal. */
	struct HUNLSampledTerminalInput
	{
		std::array<int, 2> contributions = { 0, 0 }; ///< Total contributions by player.
		PlayerId traversing_player = 0;				 ///< Player whose utility is returned.
	};

	/** @brief Evaluates terminal utility from the sampled traversing player's perspective. */
	class HUNLSampledTerminalEvaluator
	{
	public:
		/** @brief Converts a two-player terminal utility to traversing-player value. */
		[[nodiscard]] double evaluate_terminal(
			const HUNLSampledTerminalInput& input,
			const std::array<double, 2>& terminal_utility) const noexcept;
		/** @brief Computes traversing-player value for a fold terminal. */
		[[nodiscard]] double evaluate_fold(
			const HUNLSampledTerminalInput& input,
			PlayerId folding_player) const noexcept;
		/** @brief Selects traversing player's value from a showdown utility. */
		[[nodiscard]] double evaluate_showdown(
			const HUNLSampledTerminalInput& input,
			double showdown_utility) const noexcept;
	};

} // namespace texas::solver::hunl
