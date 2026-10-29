#pragma once

/** @file
 * @brief Declares the two-player Leduc poker state and action IDs.
 */

#include "core/legacy_namespace_compat.hpp"

#include "core/game.hpp"

#include <array>
#include <cstdint>
#include <memory>
#include <optional>

namespace texas::games
{

	inline constexpr ActionId LEDUC_FOLD = 0; ///< Folds the acting player's hand.
	inline constexpr ActionId LEDUC_CALL = 1; ///< Matches the current stake.
	inline constexpr ActionId LEDUC_RAISE = 2; ///< Increases the current stake.

	/**
	 * @brief Leduc poker game state.
	 */
	class LeducState final : public Game
	{
	public:
		std::vector<int> private_cards; ///< Dealt private cards in player order.
		std::optional<int> public_card; ///< Public card dealt between betting rounds, if available.
		std::vector<ActionId> round1_history; ///< Actions in the first betting round.
		std::vector<ActionId> round2_history; ///< Actions in the second betting round.
		std::array<int, 2> ante; ///< Chips committed by each player, including matched stakes.
		std::array<bool, 2> folded; ///< Whether each player has folded.
		std::uint8_t round_num; ///< Current betting round, one or two.
		std::uint8_t num_raises; ///< Raises made in the current betting round.
		std::uint8_t num_calls; ///< Calls made since the latest raise in this round.
		int stakes; ///< Amount each player must reach to match the current wager.
		PlayerId cur_player; ///< Acting player, or -1 while chance or no player acts.

		/** @brief Constructs the initial state with one-chip antes and a pending deal. */
		LeducState();

		/** @brief Returns a fresh initial Leduc state. */
		static LeducState initial();

		/** @brief Returns the total chips committed by both players. */
		int pot() const;
		/** @brief Reports whether the current betting round has received all required calls. */
		bool round_complete() const;
		/** @brief Encodes fold, call and raise actions as f, c and r.
		 * @param history Action sequence to encode.
		 * @return Compact token string.
		 * @throws std::logic_error If history contains an unknown action ID.
		 */
		std::string round_string(const std::vector<ActionId>& history) const;

		/** @copydoc texas::core::Game::is_terminal */
		bool is_terminal() const override;
		/** @copydoc texas::core::Game::utility
		 * @throws std::logic_error If this state is not terminal or lacks showdown cards.
		 */
		std::vector<Value> utility() const override;
		/** @copydoc texas::core::Game::current_player */
		PlayerId current_player() const override;
		/** @copydoc texas::core::Game::chance_outcomes */
		std::vector<ChanceOutcome> chance_outcomes() const override;
		/** @copydoc texas::core::Game::legal_actions */
		std::vector<ActionId> legal_actions() const override;
		/** @brief Returns a copied state after applying a chance card or player action.
		 * @param action Card ID during a chance node, otherwise a Leduc action ID.
		 * @return Successor state; this state is not modified.
		 */
		LeducState next_state(ActionId action) const;
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

	private:
		LeducState apply_chance(ActionId action) const;
		LeducState apply_player(ActionId action) const;
		static PlayerId first_non_folded(const std::array<bool, 2>& folded);
		static PlayerId next_player(PlayerId player, const std::array<bool, 2>& folded);
	};

} // namespace texas::games
