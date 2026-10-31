#pragma once

#include "core/value_types.hpp"

/** @file
 * @brief Compatibility entry point for shared identifiers and value types.
 */

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <unordered_map>

namespace texas::core
{
	/// Number of encoded cards in the standard deck.
	inline constexpr std::size_t DECK_CARD_COUNT = 52U;

	/** @brief Internal numeric infoset identifier used by solver internals. */
	struct InfosetId
	{
		std::uint32_t value = 0; ///< Numeric identifier assigned by an infoset registry.

		/** @brief Compares identifiers by their numeric value.
		 * @param other Identifier to compare.
		 * @return True when both numeric values are equal.
		 */
		constexpr bool operator==(const InfosetId& other) const noexcept
		{
			return value == other.value;
		}
		/** @brief Reports whether this identifier differs from another numeric ID.
		 * @param other Identifier to compare.
		 * @return True when the numeric values differ.
		 */
		constexpr bool operator!=(const InfosetId& other) const noexcept
		{
			return value != other.value;
		}
		/** @brief Orders identifiers by their numeric value.
		 * @param other Identifier to compare.
		 * @return True when this numeric value is less than other's value.
		 */
		constexpr bool operator<(const InfosetId& other) const noexcept
		{
			return value < other.value;
		}
	};
} // namespace texas::core

/** @brief Hashes an information-set ID by its numeric value. */
template <>
struct std::hash<texas::core::InfosetId>
{
	/** @brief Returns the standard-library hash of the identifier's numeric value.
	 * @param id Identifier to hash.
	 * @return Hash value derived from id.value.
	 */
	std::size_t operator()(const texas::core::InfosetId& id) const noexcept
	{
		return std::hash<std::uint32_t>{}(id.value);
	}
};
