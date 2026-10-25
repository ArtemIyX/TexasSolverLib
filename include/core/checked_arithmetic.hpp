#pragma once

#include <cstddef>
#include <cstdint>
#include <limits>
#include <type_traits>

namespace texas::core
{

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
