#pragma once

/** @file
 * @brief HUNL terminal classification values.
 */

#include <cstdint>

namespace texas::games::hunl
{
	/** @brief Tag describing why a HUNL tree node has terminal semantics. */
	enum class TerminalKindTag : std::uint8_t
	{
		NonTerminal = 0, ///< Node is not a terminal settlement.
		Fold = 1,        ///< One player folded and the other wins.
		Showdown = 2,    ///< Remaining players are evaluated at showdown.
	};

	/** @brief Terminal classification and settlement metadata for a tree node. */
	struct TerminalKind
	{
		TerminalKindTag tag = TerminalKindTag::NonTerminal; ///< Terminal category.
		std::uint8_t winner = 0; ///< Winning player for a fold terminal.
		int contribution_loss = 0; ///< Loser's committed chips in a fold terminal.
		bool board_complete = false; ///< Whether a showdown board has five cards.

		/** @brief Creates a non-terminal classification. */
		static TerminalKind non_terminal();
		/** @brief Creates a fold result.
		 * @param winner Player who did not fold.
		 * @param contribution_loss Chips committed by the folding player.
		 */
		static TerminalKind fold(std::uint8_t winner, int contribution_loss);
		/** @brief Creates a showdown result.
		 * @param board_complete True when the board contains all five community cards.
		 */
		static TerminalKind showdown(bool board_complete);
	};
}
