#pragma once

/** @file
 * @brief Declares the polymorphic interface for finite game-tree states.
 */

#include "core/types.hpp"

#include <memory>
#include <vector>

namespace texas::core
{

	/**
	 * @brief Common game-tree interface used by DCFR and exploitability walks.
	 */
	class Game
	{
	public:
		/** @brief Destroys a game state through the base interface. */
		virtual ~Game() = default;

		/** @brief Returns the number of players represented by this game. */
		virtual std::size_t num_players() const = 0;
		/** @brief Reports whether this state is terminal. */
		virtual bool is_terminal() const = 0;
		/** @brief Returns the active player, or -1 when chance acts. */
		virtual PlayerId current_player() const = 0;
		/** @brief Returns the legal action IDs at this state. */
		virtual std::vector<ActionId> legal_actions() const = 0;
		/** @brief Returns chance action IDs and probabilities when chance acts. */
		virtual std::vector<ChanceOutcome> chance_outcomes() const = 0;
		/** @brief Creates an owning polymorphic clone of this state.
		 * @return A heap-allocated game state managed by the returned unique pointer.
		 */
		virtual std::unique_ptr<Game> clone() const = 0;
		/** @brief Creates the successor state produced by an action.
		 * @param action Action ID interpreted by the concrete game implementation.
		 * @return A heap-allocated successor managed by the returned unique pointer.
		 */
		virtual std::unique_ptr<Game> apply(ActionId action) const = 0;
		/** @brief Returns a player's information-set key at this state.
		 * @param player Player whose information set is requested.
		 * @return Key used to group states indistinguishable to that player.
		 */
		virtual std::string infoset_key(PlayerId player) const = 0;
		/** @brief Returns terminal payoffs indexed by player.
		 * @return Utility vector; concrete games define behavior for nonterminal states.
		 */
		virtual std::vector<Value> utility() const = 0;
	};

} // namespace texas::core
