#pragma once

/** @file
 * @brief Lightweight enums describing flat HUNL storage.
 */

#include <cstdint>

namespace texas::solver::hunl
{
	/** @brief Memory order used for an information set's bucket/action values. */
	enum class HUNLFlatValueLayout : std::uint8_t
	{
		InfosetHandAction = 0, ///< Values laid out by hand, then action.
		InfosetActionHand = 1, ///< Values laid out by action, then hand.
	};

	/** @brief Scalar precision used for flat regret and strategy storage. */
	enum class HUNLFlatStoragePrecision : std::uint8_t
	{
		Float64 = 0, ///< Store values as double precision.
		Float32 = 1, ///< Store values as single precision.
	};
}
