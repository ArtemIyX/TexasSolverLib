#pragma once

/** @file
 * @brief Declares multiway side-pot construction and terminal settlement results.
 */

#include "core/legacy_namespace_compat.hpp"

#include "games/hunl_eval.hpp"
#include "games/multiway_rake.hpp"

#include <cstdint>
#include <vector>

namespace texas::solver::multiway
{
	enum class MultiwayValueUnits : std::uint8_t;
}

namespace texas::games::multiway
{

	struct MultiwayGameRules;

	/** @brief Contributions, fold status and hand strengths for a terminal hand. */
	struct MultiwayTerminalInput
	{
		std::vector<int> contributions;										  ///< Total chips committed by each seat, including folded seats.
		std::vector<bool> folded;											  ///< Whether each seat folded before settlement.
		std::vector<Strength> strengths;									  ///< Evaluated hand strength by seat.
		PlayerId odd_chip_first_seat = 0;									  ///< First seat in cyclic order to receive odd split chips.
		MultiwayRakePolicy rake_policy = MultiwayRakePolicy::explicit_zero(); ///< Policy applied to contested pots.
		bool flop_seen = true;												  ///< Whether the flop was dealt for no-flop-no-drop rake.

		/** @brief Validates matching two-through-six-seat arrays and settlement inputs.
		 * @throws std::invalid_argument If seat counts, contributions, rake or odd-chip seat are invalid.
		 */
		void validate() const;
	};

	/** @brief A contribution layer with its eligible non-folded seats. */
	struct MultiwaySidePot
	{
		int amount = 0;							///< Chips in this layer; settlement result pots have rake removed.
		int contribution_cap = 0;				///< Highest per-seat contribution included in this layer.
		std::vector<PlayerId> eligible_players; ///< Non-folded seats that may win this pot.
	};

	/** @brief Side-pot layers and uncalled-contribution refunds before settlement. */
	struct MultiwayPotLayout
	{
		std::vector<MultiwaySidePot> pots; ///< Contested side pots ordered by contribution cap.
		std::vector<int> refunds;		   ///< Uncalled contributions returned to each seat.
	};

	/** @brief Payout, refund, utility and rake results for a settled hand. */
	struct MultiwayTerminalResult
	{
		std::vector<MultiwaySidePot> pots;							 ///< Contested pots after rake.
		std::vector<int> refunds;									 ///< Uncalled chips returned to each seat.
		std::vector<int> payouts;									 ///< Chips awarded to each seat from contested pots.
		std::vector<Value> utilities;								 ///< Net values after contributions, in utility_units.
		int rake_taken = 0;											 ///< Chips removed from contested pots exactly once by the rake policy.
		texas::solver::multiway::MultiwayValueUnits utility_units{}; ///< Unit tag for utilities; payouts and refunds remain chips.
	};

	/** @brief Builds side pots and uncalled-bet refunds from contributions and fold status.
	 * @param contributions Total chips committed by each seat.
	 * @param folded Fold status for each seat.
	 * @return Contribution-ordered pots and per-seat refunds.
	 * @throws std::invalid_argument If seat vectors or chip totals are invalid.
	 */
	MultiwayPotLayout build_multiway_pot_layout(
		const std::vector<int>& contributions,
		const std::vector<bool>& folded);

	/** @brief Builds side pots and settles them by hand strength.
	 *
	 * Folded seats contribute but cannot win. Tied-pot odd chips use the cyclic
	 * seat order beginning at odd_chip_first_seat. A layer with one contributor
	 * is returned as an uncalled-bet refund.
	 * @param input Validated contributions, hand strengths and settlement policy.
	 * @return Settlement result with pots, refunds, payouts, utilities and rake.
	 * @throws std::invalid_argument If the input is invalid.
	 */
	MultiwayTerminalResult settle_multiway_terminal(const MultiwayTerminalInput& input);
	/** @brief Verifies a supplied pot layout, then settles using the terminal input.
	 * @param input Contributions, fold status, strengths and settlement policy.
	 * @param layout Layout to check against the input's contribution and fold data.
	 * @return Settlement result.
	 * @throws std::invalid_argument If layout does not match input or input is invalid.
	 */
	MultiwayTerminalResult settle_multiway_terminal(
		const MultiwayTerminalInput& input,
		const MultiwayPotLayout& layout);
	/** @brief Settles using the validated rules profile's rake policy.
	 * @param input Contributions, fold status and hand strengths.
	 * @param rules Supported profile with a matching seat count.
	 * @return Settlement result using rules.rake_policy.
	 * @throws std::invalid_argument If rules or seat count is invalid.
	 */
	MultiwayTerminalResult settle_multiway_terminal(
		const MultiwayTerminalInput& input,
		const MultiwayGameRules& rules);

} // namespace texas::games::multiway
