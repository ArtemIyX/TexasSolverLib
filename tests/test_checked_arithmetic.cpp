#include "core/checked_arithmetic.hpp"
#include "test_harness.hpp"

#include <cstddef>
#include <cstdint>
#include <limits>

TEST_CASE(checked_size_multiply_accepts_zero_and_boundary)
{
	std::size_t result = 0U;
	EXPECT_TRUE(texas::core::checked_multiply_to_size(0U, std::numeric_limits<std::uint32_t>::max(), result));
	EXPECT_EQ(result, 0U);
	EXPECT_TRUE(texas::core::checked_multiply_to_size(
		std::numeric_limits<std::size_t>::max(), 1U, result));
	EXPECT_EQ(result, std::numeric_limits<std::size_t>::max());
}

TEST_CASE(checked_size_multiply_rejects_overflow_and_negative_values)
{
	std::size_t result = 0U;
	EXPECT_TRUE(!texas::core::checked_multiply_to_size(
		std::numeric_limits<std::size_t>::max(), 2U, result));
	EXPECT_TRUE(!texas::core::checked_multiply_to_size(-1, 1U, result));
	EXPECT_TRUE(!texas::core::checked_multiply_to_size(1, -1, result));
}

TEST_CASE(checked_size_add_rejects_overflow)
{
	std::size_t result = 0U;
	EXPECT_TRUE(texas::core::checked_add_to_size(
		std::numeric_limits<std::size_t>::max() - 1U, 1U, result));
	EXPECT_EQ(result, std::numeric_limits<std::size_t>::max());
	EXPECT_TRUE(!texas::core::checked_add_to_size(
		std::numeric_limits<std::size_t>::max(), 1U, result));
}
