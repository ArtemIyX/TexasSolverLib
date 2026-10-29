#pragma once

/** @file
 * @brief Declares the two-player Kuhn poker state and action IDs.
 */

#include "core/legacy_namespace_compat.hpp"

#include "core/game.hpp"

#include <array>
#include <memory>

namespace texas::games
{

	inline constexpr ActionId PASS = 0; ///< Checks or folds, depending on the betting history.
	inline constexpr ActionId BET = 1; ///< Bets or calls, depending on the betting history.

	/**
	 * @brief Kuhn poker game state.
	 */
	class KuhnState final : public Game
	{
	public:
		std::array<int, 2> cards; ///< Private card for each player; -1 marks an undealt card.
		std::vector<ActionId> history; ///< Player actions in chronological order.
		std::uint8_t chance_phase; ///< Number of private cards dealt, from zero through two.

		/** @brief Constructs the initial chance node with both cards undealt. */
		KuhnState();

		/** @brief Returns a fresh initial Kuhn state. */
		static KuhnState initial();

		/** @brief Encodes PASS as p and BET as b in the action history. */
		std::string history_string() const;
		/** @copydoc texas::core::Game::is_terminal */
		bool is_terminal() const override;
		/** @copydoc texas::core::Game::utility
		 * @throws std::logic_error If this state is not terminal.
		 */
		std::vector<Value> utility() const override;
		/** @copydoc texas::core::Game::current_player */
		PlayerId current_player() const override;
		/** @copydoc texas::core::Game::chance_outcomes */
		std::vector<ChanceOutcome> chance_outcomes() const override;
		/** @copydoc texas::core::Game::legal_actions */
		std::vector<ActionId> legal_actions() const override;
		/** @brief Returns a copied state after dealing a card or recording a player action.
		 * @param action Card ID during either chance phase, otherwise PASS or BET.
		 * @return Successor state; this state is not modified.
		 */
		KuhnState next_state(ActionId action) const;
		/** @copydoc texas::core::Game::infoset_key */
		std::string infoset_key(PlayerId player) const override;

		/** @copydoc texas::core::Game::num_players
		 * @return Always two.
		 */
		std::size_t num_players() const override;
		/** @copydoc texas::core::Game::clone */
		std::unique_ptr<Game> clone() const override;
		/** @copydoc texas::core::Game::apply */
		std::unique_ptr<Game> apply(ActionId action) const override;
	};

} // namespace texas::games
