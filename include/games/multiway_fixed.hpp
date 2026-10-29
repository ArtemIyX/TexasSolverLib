#pragma once

/** @file
 * @brief Declares fixed-capacity multiway betting and terminal-settlement data.
 */

#include "core/legacy_namespace_compat.hpp"

#include "games/multiway_state.hpp"
#include "games/multiway_terminal.hpp"

#include <array>
#include <cstddef>
#include <cstdint>

namespace texas::games::multiway
{

	struct MultiwayGameRules;

	constexpr std::size_t kMultiwayFixedMaxSeats = 6U; ///< Maximum supported seat count.
	constexpr std::size_t kMultiwayFixedMaxActions = 4U; ///< Capacity of a fixed legal-action menu.

	/** @brief Fixed-capacity list of actions legal in one betting state. */
	struct MultiwayFixedActionMenu
	{
		std::array<MultiwayAction, kMultiwayFixedMaxActions> actions = {}; ///< Legal actions in menu order.
		std::uint8_t count = 0; ///< Number of valid entries at the start of actions.

		/** @brief Reports whether action appears among the first count entries.
		 * @param action Action ID to search for.
		 * @return True if action is present in the menu.
		 */
		[[nodiscard]] bool contains(MultiwayAction action) const noexcept;
	};

	/** @brief Fixed-capacity, value-owned view of a multiway betting state.
	 *
	 * Per-seat arrays use entries [0, seat_count). The type owns no heap memory
	 * and copies its state by value.
	 */
	struct MultiwayFixedState
	{
		std::uint8_t seat_count = 0; ///< Active table size, from two through kMultiwayFixedMaxSeats.
		std::array<int, kMultiwayFixedMaxSeats> stacks = {}; ///< Remaining chips by seat.
		std::array<int, kMultiwayFixedMaxSeats> contributions = {}; ///< Total chips committed this hand by seat.
		std::array<int, kMultiwayFixedMaxSeats> street_contributions = {}; ///< Chips committed on the current street.
		std::array<bool, kMultiwayFixedMaxSeats> folded = {}; ///< Whether each seat has folded.
		std::array<bool, kMultiwayFixedMaxSeats> all_in = {}; ///< Whether each seat has no chips remaining.
		std::array<bool, kMultiwayFixedMaxSeats> may_raise = {}; ///< Whether each seat retains raise rights.
		std::array<bool, kMultiwayFixedMaxSeats> pending = {}; ///< Whether each actionable seat still owes a response.
		std::array<bool, kMultiwayFixedMaxSeats> has_acted = {}; ///< Whether each seat has acted on this street.
		std::array<int, kMultiwayFixedMaxSeats> bet_faced_when_acted = {}; ///< Current bet faced at each seat's last action.
		PlayerId current_player = -1; ///< Acting seat, or -1 when no betting decision is pending.
		PlayerId last_aggressor = -1; ///< Seat that made the last bet or raise, or -1 if none.
		int current_bet = 0; ///< Highest street contribution that other seats must match.
		int last_full_raise_size = 0; ///< Minimum increment for a full raise.
		int big_blind = 0; ///< Big-blind amount used as the opening full-raise size.
		Street street = Street::Flop; ///< Current betting street.

		/** @brief Validates dimensions and per-seat betting-state invariants.
		 * @throws std::invalid_argument If seat counts, values or flags are inconsistent.
		 */
		void validate() const;
		/** @brief Classifies the next decision, board runout, transition or terminal state.
		 * @return FoldTerminal when at most one player remains; BettingDecision when
		 * current_player is set; BoardRunout when at most one seat can act before
		 * the river; otherwise StreetTransition or ShowdownTerminal.
		 */
		[[nodiscard]] MultiwayNextNodeKind next_node_kind() const noexcept;
		/** @brief Returns the fixed-capacity legal-action menu for current_player.
		 * @return Empty menu when no betting player is selected.
		 */
		[[nodiscard]] MultiwayFixedActionMenu legal_actions() const noexcept;
		/** @brief Applies one legal action to a copy of the state.
		 *
		 * For Bet and Raise, target_street_contribution is the acting seat's new
		 * total contribution on this street. Other actions ignore that argument.
		 * @param action Action selected from legal_actions().
		 * @param target_street_contribution New street total for Bet or Raise.
		 * @return Successor state; this state is unchanged.
		 * @throws std::invalid_argument If the state, action or raise target is invalid.
		 */
		[[nodiscard]] MultiwayFixedState apply(MultiwayAction action, int target_street_contribution = 0) const;
		/** @brief Starts the immediately following betting street.
		 * @param next_street Street directly after the current street.
		 * @param first_player Seat that acts first on the new street.
		 * @return Successor state with per-street betting data reset.
		 * @throws std::invalid_argument If betting is not complete or the transition is invalid.
		 */
		[[nodiscard]] MultiwayFixedState begin_next_street(Street next_street, PlayerId first_player) const;
	};

	/** @brief Converts a validated dynamic game config to fixed-capacity state.
	 * @param config Initial stacks, contributions, street and first actor.
	 * @return Fixed-capacity state corresponding to config.
	 */
	[[nodiscard]] MultiwayFixedState make_multiway_fixed_state(const MultiwayGameConfig& config);
	/** @brief Builds fixed-capacity initial state from a rules profile and first actor.
	 * @param rules Supported rules profile.
	 * @param first_player First voluntary preflop actor.
	 * @return Fixed-capacity initial state.
	 */
	[[nodiscard]] MultiwayFixedState make_multiway_fixed_state(
		const MultiwayGameRules& rules,
		PlayerId first_player = 0);
	/** @brief Converts a validated dynamic betting snapshot to fixed-capacity state.
	 * @param snapshot Betting state with dynamically sized per-seat arrays.
	 * @return Fixed-capacity copy of the snapshot.
	 */
	[[nodiscard]] MultiwayFixedState make_multiway_fixed_state(const MultiwayBettingSnapshot& snapshot);
	/** @brief Converts fixed-capacity state to a dynamically sized betting snapshot.
	 * @param state State to convert.
	 * @return Snapshot containing entries for the active seats.
	 * @throws std::invalid_argument If state invariants fail validation.
	 */
	[[nodiscard]] MultiwayBettingSnapshot make_multiway_betting_snapshot(
		const MultiwayFixedState& state);

	/** @brief Inputs required to settle a multiway hand in fixed-capacity storage. */
	struct MultiwayFixedTerminalInput
	{
		std::uint8_t seat_count = 0; ///< Active table size; per-seat arrays use entries [0, seat_count).
		std::array<int, kMultiwayFixedMaxSeats> contributions = {}; ///< Total chips committed by each seat.
		std::array<bool, kMultiwayFixedMaxSeats> folded = {}; ///< Whether each seat folded before settlement.
		std::array<Strength, kMultiwayFixedMaxSeats> strengths = {}; ///< Evaluated hand strength for each seat.
		PlayerId odd_chip_first_seat = 0; ///< First seat eligible to receive an odd split chip.
		MultiwayRakePolicy rake_policy = MultiwayRakePolicy::explicit_zero(); ///< Policy used to calculate rake.
		bool flop_seen = true; ///< Whether the flop was dealt for no-flop-no-drop rake.

		/** @brief Validates seat counts, contributions and rake configuration.
		 * @throws std::invalid_argument If there is no live player or the input is invalid.
		 */
		void validate() const;
	};

	/** @brief One fixed-capacity side-pot layer and its eligible seats. */
	struct MultiwayFixedSidePot
	{
		int amount = 0; ///< Chips remaining in this pot after rake is taken.
		int contribution_cap = 0; ///< Highest per-seat contribution included in this layer.
		std::array<PlayerId, kMultiwayFixedMaxSeats> eligible_players = {}; ///< Non-folded seats eligible to win this pot.
		std::uint8_t eligible_count = 0; ///< Number of valid eligible_players entries.
	};

	/** @brief Reusable temporary arrays for fixed-capacity settlement. */
	struct MultiwayFixedTerminalScratch
	{
		std::array<int, kMultiwayFixedMaxSeats> levels = {}; ///< Sorted distinct contribution levels for the current call.
		std::uint8_t level_count = 0; ///< Number of valid levels entries.
		std::array<PlayerId, kMultiwayFixedMaxSeats> winners = {}; ///< Temporary winner seats for one pot.
	};

	/** @brief Fixed-capacity payouts, refunds, utilities and pot details for a settled hand. */
	struct MultiwayFixedTerminalResult
	{
		std::uint8_t seat_count = 0; ///< Number of seats represented by result arrays.
		std::array<MultiwayFixedSidePot, kMultiwayFixedMaxSeats> pots = {}; ///< Contested side pots in contribution order.
		std::uint8_t pot_count = 0; ///< Number of valid pots entries.
		std::array<int, kMultiwayFixedMaxSeats> refunds = {}; ///< Uncalled contributions returned to each seat.
		std::array<int, kMultiwayFixedMaxSeats> payouts = {}; ///< Pot winnings after rake and odd-chip allocation.
		std::array<Value, kMultiwayFixedMaxSeats> utilities = {}; ///< Net payout minus contribution for each seat.
		int rake_taken = 0; ///< Chips removed from contested pots under the rake policy.
	};

	/** @brief Settles a hand using the rake policy stored in input.
	 *
	 * The function overwrites result and reuses scratch. Ties split each pot
	 * evenly, with remainder chips assigned in seat order starting at
	 * odd_chip_first_seat. scratch is intended to be owned by one worker.
	 * @param input Contributions, eligible seats, strengths and rake settings.
	 * @param scratch Caller-owned temporary storage reused across settlements.
	 * @param[out] result Receives side pots, refunds, payouts and net utilities.
	 * @throws std::invalid_argument If the input is invalid.
	 * @throws std::logic_error If a derived side-pot layer has no eligible winner.
	 */
	void settle_multiway_terminal_fixed(
		const MultiwayFixedTerminalInput& input,
		MultiwayFixedTerminalScratch& scratch,
		MultiwayFixedTerminalResult& result);

	/** @brief Settles a hand using the rules profile's rake policy.
	 *
	 * The policy stored in input is replaced with rules.rake_policy.
	 * @param input Contributions, eligible seats and hand strengths.
	 * @param rules Supported profile with the same seat count as input.
	 * @param scratch Caller-owned temporary storage reused across settlements.
	 * @param[out] result Receives the settlement result.
	 * @throws std::invalid_argument If the rules, seat count or input are invalid.
	 * @throws std::logic_error If a derived side-pot layer has no eligible winner.
	 */
	void settle_multiway_terminal_fixed(
		const MultiwayFixedTerminalInput& input,
		const MultiwayGameRules& rules,
		MultiwayFixedTerminalScratch& scratch,
		MultiwayFixedTerminalResult& result);

} // namespace texas::games::multiway
