#pragma once

/** @file
 * @brief Declares multiway betting progression, dynamic state and live snapshots.
 */

#include "core/poker.hpp"

#include "games/multiway_rake.hpp"

#include <cstdint>
#include <vector>

namespace texas::games::multiway
{

	struct MultiwayGameRules;

	/** @brief Betting actions accepted by the multiway state machine. */
	enum class MultiwayAction : std::uint8_t
	{
		Fold,  ///< Folds while retaining prior contributions in the pot.
		Check, ///< Ends the turn without adding chips when no call is owed.
		Call,  ///< Adds up to the amount needed to match the current bet.
		Bet,   ///< Opens or increases the current bet using a street-total target.
		Raise, ///< Increases an existing bet using a street-total target.
		AllIn, ///< Commits the acting seat's remaining stack.
	};

	/** @brief Classifies the next betting or terminal operation for a state. */
	enum class MultiwayNextNodeKind : std::uint8_t
	{
		BettingDecision,  ///< An actionable seat must choose a betting action.
		StreetTransition, ///< Betting is complete and another street begins.
		BoardRunout,	  ///< Betting is closed but cards must run out before showdown.
		FoldTerminal,	  ///< At most one player remains live.
		ShowdownTerminal, ///< River betting is complete and hands are ready to settle.
	};

	/** @brief Initial stacks, contributions and betting metadata for a multiway hand. */
	struct MultiwayGameConfig
	{
		std::vector<int> starting_stacks;									  ///< Total stack by seat before initial_contributions are posted.
		std::vector<int> initial_contributions;								  ///< Total-hand chips already committed by each seat.
		std::vector<int> initial_street_contributions;						  ///< Current-street part of each initial contribution; empty means zero.
		core::PlayerId first_player = 0;									  ///< First seat considered for action.
		int big_blind = 100;												  ///< Positive amount used as the minimum opening full raise.
		core::Street street = core::Street::Flop;							  ///< Street represented by the initial state.
		MultiwayRakePolicy rake_policy = MultiwayRakePolicy::explicit_zero(); ///< Settlement policy carried into the game.

		/** @brief Validates seat vectors, contributions, first player and rake policy.
		 * @throws std::invalid_argument If the configuration is invalid.
		 */
		void validate() const;
	};

	/** @brief Complete public betting state for restoring a live subgame.
	 *
	 * This snapshot omits canonical action and abstraction metadata; callers
	 * provide that separately for information-set lookup.
	 */
	struct MultiwayBettingSnapshot
	{
		std::vector<int> stacks;				  ///< Remaining chips by seat.
		std::vector<int> contributions;			  ///< Total chips committed by seat this hand.
		std::vector<int> street_contributions;	  ///< Chips committed by seat on the current street.
		std::vector<bool> folded;				  ///< Whether each seat has folded.
		std::vector<bool> all_in;				  ///< Whether each seat has no remaining chips.
		std::vector<bool> may_raise;			  ///< Whether each seat currently retains raise rights.
		std::vector<bool> pending;				  ///< Whether each actionable seat still owes a response.
		std::vector<bool> has_acted;			  ///< Whether each seat has acted on this street.
		std::vector<int> bet_faced_when_acted;	  ///< Bet amount each seat faced at its last action.
		core::PlayerId current_player = -1;		  ///< Acting seat, or -1 when no action is pending.
		core::PlayerId last_aggressor = -1;		  ///< Last seat to bet or raise, or -1 if none.
		int current_bet = 0;					  ///< Highest current-street contribution.
		int last_full_raise_size = 0;			  ///< Minimum increment for a full raise.
		int big_blind = 0;						  ///< Big-blind amount used to reset the minimum raise size.
		core::Street street = core::Street::Flop; ///< Street represented by this state.

		void validate() const;
		void validate() const;
	};

	/** @brief Value-owned dynamic state for multiway betting progression. */
	class MultiwayState
	{
	public:
		/** @brief Creates the initial state from a validated dynamic configuration.
		 * @param config Starting stacks, contributions and betting metadata.
		 * @return Initial betting state.
		 * @throws std::invalid_argument If config is invalid.
		 */
		static MultiwayState initial(const MultiwayGameConfig& config);
		/** @brief Creates a preflop root from a supported rules profile and posts blinds.
		 * @param rules Supported multiway game rules.
		 * @param first_player First voluntary preflop actor.
		 * @return Initial state with both blinds committed.
		 * @throws std::invalid_argument If the rules or first_player is invalid.
		 */
		static MultiwayState initial(const MultiwayGameRules& rules, core::PlayerId first_player = 0);
		/** @brief Restores a state from a validated complete betting snapshot.
		 * @param snapshot Betting state to restore.
		 * @return State reconstructed from the snapshot.
		 * @throws std::invalid_argument If snapshot invariants fail validation.
		 */
		static MultiwayState from_snapshot(const MultiwayBettingSnapshot& snapshot);
		/** @brief Copies the current public betting state into a snapshot. */
		[[nodiscard]] MultiwayBettingSnapshot snapshot() const;

		/** @brief Returns remaining chips by seat. */
		const std::vector<int>& stacks() const noexcept { return stacks_; }
		/** @brief Returns total hand contributions by seat. */
		const std::vector<int>& contributions() const noexcept { return contributions_; }
		/** @brief Returns current-street contributions by seat. */
		const std::vector<int>& street_contributions() const noexcept { return street_contributions_; }
		/** @brief Returns fold status by seat. */
		const std::vector<bool>& folded() const noexcept { return folded_; }
		/** @brief Returns all-in status by seat. */
		const std::vector<bool>& all_in() const noexcept { return all_in_; }
		/** @brief Returns raise rights by seat. */
		const std::vector<bool>& may_raise() const noexcept { return may_raise_; }
		/** @brief Returns the acting seat, or -1 when no betting action is pending. */
		core::PlayerId current_player() const noexcept { return current_player_; }
		/** @brief Returns the last aggressor, or -1 if the street has no aggressor. */
		core::PlayerId last_aggressor() const noexcept { return last_aggressor_; }
		/** @brief Returns the highest current-street contribution. */
		int current_bet() const noexcept { return current_bet_; }
		/** @brief Returns the minimum chip increment for a full raise. */
		int last_full_raise_size() const noexcept { return last_full_raise_size_; }
		/** @brief Returns the current betting street. */
		core::Street street() const noexcept { return street_; }

		/** @brief Reports whether one player remains or river betting has ended. */
		bool is_hand_over() const noexcept;
		/** @brief Reports whether the state is a fold terminal or showdown terminal. */
		bool is_terminal() const noexcept;
		/** @brief Classifies the next betting, board, street or terminal operation. */
		[[nodiscard]] MultiwayNextNodeKind next_node_kind() const noexcept;
		/** @brief Reports that betting is closed while public cards still need to run out. */
		bool requires_board_runout() const noexcept;
		/** @brief Reports whether betting is complete and the next node is a street transition. */
		bool is_betting_round_complete() const noexcept;
		/** @brief Reports whether another betting street must be started. */
		bool requires_street_transition() const noexcept;
		/** @brief Returns the legal actions for the current betting decision.
		 * @return Empty vector when no player is selected to act.
		 */
		std::vector<MultiwayAction> legal_actions() const;

		/** @brief Applies a legal betting action and returns the successor state.
		 *
		 * target_street_contribution is the acting seat's new total on this street
		 * for Bet and Raise; other actions ignore it.
		 * @param action Action selected from legal_actions().
		 * @param target_street_contribution New street total for Bet or Raise.
		 * @return Successor state; this state is unchanged.
		 * @throws std::logic_error If no betting action is pending.
		 * @throws std::invalid_argument If action or its raise target is invalid.
		 */
		MultiwayState apply(MultiwayAction action, int target_street_contribution = 0) const;
		/** @brief Starts the next betting street after a completed round.
		 * @param next_street Street directly after the current street.
		 * @param first_player First actor on the new street.
		 * @return Successor state with street-specific betting data reset.
		 * @throws std::invalid_argument If the street transition is invalid.
		 */
		MultiwayState begin_next_street(core::Street next_street, core::PlayerId first_player) const;

	private:
		std::vector<int> stacks_;
		std::vector<int> contributions_;
		std::vector<int> street_contributions_;
		std::vector<bool> folded_;
		std::vector<bool> all_in_;
		std::vector<bool> may_raise_;
		std::vector<bool> pending_;
		std::vector<bool> has_acted_;
		std::vector<int> bet_faced_when_acted_;
		core::PlayerId current_player_ = -1;
		core::PlayerId last_aggressor_ = -1;
		int current_bet_ = 0;
		int last_full_raise_size_ = 0;
		int big_blind_ = 0;
		core::Street street_ = core::Street::Flop;

		bool is_actionable(core::PlayerId player) const noexcept;
		std::size_t live_player_count() const noexcept;
		std::size_t actionable_player_count() const noexcept;
		core::PlayerId next_pending_after(core::PlayerId player) const noexcept;
		void select_next_player();
		void refresh_round_completion();
		void reset_pending_after_full_raise(core::PlayerId aggressor);
		void refresh_raise_rights_after_short_raise(core::PlayerId aggressor);
	};

} // namespace texas::games::multiway
