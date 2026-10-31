#pragma once

/** @file
 * @brief Compatibility entry point for shared identifiers and value types.
 */

#include "core/value_types.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace texas::core
{
	/// Number of encoded cards in the standard deck.
	inline constexpr std::size_t DECK_CARD_COUNT = 52U;
}
