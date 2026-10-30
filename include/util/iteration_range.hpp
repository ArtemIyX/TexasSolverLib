#pragma once

/** @file
 * Inclusive uint32 iteration loops with checked overflow handling.
 */

#include <cstdint>
#include <limits>
#include <stdexcept>

namespace texas::util
{

	/** Calls callback for each integer in (last, target], if any. */
	template <class Callback>
	inline void for_each_u32_after(
		std::uint32_t last,
		std::uint32_t target,
		Callback&& callback)
	{
		if (last >= target)
		{
			return;
		}
		const auto target_wide = static_cast<std::uint64_t>(target);
		for (auto value = static_cast<std::uint64_t>(last) + 1U;
			value <= target_wide;
			++value)
		{
			callback(static_cast<std::uint32_t>(value));
		}
	}

	/** Returns current + 1 or throws when the uint32 counter is exhausted. */
	[[nodiscard]] inline std::uint32_t checked_next_u32_iteration(
		std::uint32_t current)
	{
		if (current == std::numeric_limits<std::uint32_t>::max())
		{
			throw std::overflow_error("32-bit iteration counter is exhausted");
		}
		return current + 1U;
	}

} // namespace texas::util
