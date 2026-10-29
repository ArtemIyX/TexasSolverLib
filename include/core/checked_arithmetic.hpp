#pragma once

/** @file
 * @brief Declares checked conversions and size arithmetic for non-negative integral values.
 */

#include <cstddef>
#include <cstdint>
#include <limits>
#include <type_traits>

namespace texas::core
{

	/** @brief Converts a non-negative integral value to std::size_t when representable.
	 * @tparam T Integral type supported by std::make_unsigned_t.
	 * @param value Value to convert; a negative signed value fails.
	 * @param[out] result Receives the converted value on success and is unchanged on failure.
	 * @return True when value is representable as std::size_t.
	 */
	template <typename T>
	[[nodiscard]] constexpr bool checked_to_size(T value, std::size_t& result) noexcept
	{
		static_assert(std::is_integral_v<T>);
		if constexpr (std::is_signed_v<T>)
		{
			if (value < 0)
				return false;
		}
		using Unsigned = std::make_unsigned_t<T>;
		const auto unsigned_value = static_cast<std::uintmax_t>(static_cast<Unsigned>(value));
		if (unsigned_value > std::numeric_limits<std::size_t>::max())
			return false;
		result = static_cast<std::size_t>(unsigned_value);
		return true;
	}

	/** @brief Multiplies non-negative integral values when the product fits std::size_t.
	 * @tparam Left Integral factor type supported by std::make_unsigned_t.
	 * @tparam Right Integral factor type supported by std::make_unsigned_t.
	 * @param left First factor; negative signed values fail.
	 * @param right Second factor; negative signed values fail.
	 * @param[out] result Receives the product on success and is unchanged on failure.
	 * @return True when the product is representable as std::size_t.
	 */
	template <typename Left, typename Right>
	[[nodiscard]] constexpr bool checked_multiply_to_size(
		Left left, Right right, std::size_t& result) noexcept
	{
		static_assert(std::is_integral_v<Left> && std::is_integral_v<Right>);
		if constexpr (std::is_signed_v<Left>)
		{
			if (left < 0)
				return false;
		}
		if constexpr (std::is_signed_v<Right>)
		{
			if (right < 0)
				return false;
		}
		const auto unsigned_left = static_cast<std::uintmax_t>(static_cast<std::make_unsigned_t<Left>>(left));
		const auto unsigned_right = static_cast<std::uintmax_t>(static_cast<std::make_unsigned_t<Right>>(right));
		if (unsigned_left != 0U && unsigned_right > std::numeric_limits<std::size_t>::max() / unsigned_left)
			return false;
		result = static_cast<std::size_t>(unsigned_left * unsigned_right);
		return true;
	}

	/** @brief Adds non-negative integral values when the sum fits std::size_t.
	 * @tparam Left Integral operand type supported by std::make_unsigned_t.
	 * @tparam Right Integral operand type supported by std::make_unsigned_t.
	 * @param left First operand; negative signed values fail.
	 * @param right Second operand; negative signed values fail.
	 * @param[out] result Receives the sum on success and is unchanged on failure.
	 * @return True when the sum is representable as std::size_t.
	 */
	template <typename Left, typename Right>
	[[nodiscard]] constexpr bool checked_add_to_size(
		Left left, Right right, std::size_t& result) noexcept
	{
		static_assert(std::is_integral_v<Left> && std::is_integral_v<Right>);
		if constexpr (std::is_signed_v<Left>)
		{
			if (left < 0)
				return false;
		}
		if constexpr (std::is_signed_v<Right>)
		{
			if (right < 0)
				return false;
		}
		const auto unsigned_left = static_cast<std::uintmax_t>(static_cast<std::make_unsigned_t<Left>>(left));
		const auto unsigned_right = static_cast<std::uintmax_t>(static_cast<std::make_unsigned_t<Right>>(right));
		if (unsigned_left > std::numeric_limits<std::size_t>::max() - unsigned_right)
			return false;
		result = static_cast<std::size_t>(unsigned_left + unsigned_right);
		return true;
	}

} // namespace texas::core
