#pragma once

/** @file
 * @brief Street identifiers shared by Hold'em value types.
 */

#include <cstdint>

namespace texas::core
{
	/** @brief Betting and resolution stages in a Hold'em hand. */
	enum class Street : std::uint8_t
	{
		Preflop = 0,  ///< Before community cards are dealt.
		Flop = 1,	  ///< Three community cards are available.
		Turn = 2,	  ///< Four community cards are available.
		River = 3,	  ///< Five community cards are available.
		Showdown = 4, ///< Hand resolution after betting is complete.
	};
} // namespace texas::core
