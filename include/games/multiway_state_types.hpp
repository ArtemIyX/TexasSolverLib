#pragma once

/** @file
 * @brief Multiway initial configuration and betting snapshot values.
 */

#include "core/street_types.hpp"
#include "core/value_types.hpp"
#include "games/multiway_rake_types.hpp"

#include <vector>

#pragma pack(push, 8)

namespace texas::games::multiway
{
	/** @brief Initial stacks, contributions and betting metadata for a multiway hand. */
	struct MultiwayGameConfig
	{
		std::vector<int> starting_stacks;									  ///< Total stack by seat before initial contributions are posted.
		std::vector<int> initial_contributions;								  ///< Total-hand chips already committed by each seat.
		std::vector<int> initial_street_contributions;						  ///< Current-street contribution; empty means zero.
		core::PlayerId first_player = 0;									  ///< First seat considered for action.
		int big_blind = 100;												  ///< Positive minimum opening full raise.
		core::Street street = core::Street::Flop;							  ///< Street represented by the initial state.
		MultiwayRakePolicy rake_policy = MultiwayRakePolicy::explicit_zero(); ///< Settlement policy carried into the game.

		/** @brief Validates seat vectors, contributions, first player and rake policy.
		 * @throws std::invalid_argument If the configuration is invalid.
		 */
		void validate() const;
	};

	/** @brief Complete public betting state for restoring a live subgame.
	 * The snapshot omits canonical action and abstraction metadata; callers
	 * provide that separately for information-set lookup.
	 */
	struct MultiwayBettingSnapshot
	{
		std::vector<int> stacks;				  ///< Remaining chips by seat.
		std::vector<int> contributions;			  ///< Total chips committed by seat this hand.
		std::vector<int> street_contributions;	  ///< Chips committed on the current street.
		std::vector<bool> folded;				  ///< Whether each seat has folded.
		std::vector<bool> all_in;				  ///< Whether each seat has no remaining chips.
		std::vector<bool> may_raise;			  ///< Whether each seat retains raise rights.
		std::vector<bool> pending;				  ///< Whether each actionable seat still owes a response.
		std::vector<bool> has_acted;			  ///< Whether each seat has acted on this street.
		std::vector<int> bet_faced_when_acted;	  ///< Bet amount each seat faced at its last action.
		core::PlayerId current_player = -1;		  ///< Acting seat, or -1 when no action is pending.
		core::PlayerId last_aggressor = -1;		  ///< Last seat to bet or raise, or -1 if none.
		int current_bet = 0;					  ///< Highest current-street contribution.
		int last_full_raise_size = 0;			  ///< Minimum increment for a full raise.
		int big_blind = 0;						  ///< Big blind used to reset minimum raise size.
		core::Street street = core::Street::Flop; ///< Street represented by this state.

		/** @brief Validates snapshot vector sizes and betting metadata.
		 * @throws std::invalid_argument If the snapshot fields are inconsistent.
		 */
		void validate() const;
	};
} // namespace texas::games::multiway

#pragma pack(pop)
