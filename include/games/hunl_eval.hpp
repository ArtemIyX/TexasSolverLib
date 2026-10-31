#pragma once

/** @file
 * @brief Declares hand-strength values and five-to-seven-card evaluation helpers.
 */

#include "games/hunl.hpp"

#include <array>
#include <cstdint>

namespace texas::games::hunl
{

	/** @brief Comparable strength value for a Hold'em hand; larger values are stronger. */
#pragma pack(push, 8)
	struct Strength
	{
		std::uint64_t value = 0; ///< Evaluator-derived value used for strength ordering.

		/** @brief Tests whether two evaluated strengths are equal.
		 * @param other Strength to compare.
		 * @return True when both values are equal.
		 */
		constexpr bool operator==(const Strength& other) const noexcept
		{
			return value == other.value;
		}
		/** @brief Tests whether this strength is weaker than another.
		 * @param other Strength to compare.
		 * @return True when this value is smaller.
		 */
		constexpr bool operator<(const Strength& other) const noexcept
		{
			return value < other.value;
		}
		/** @brief Tests whether this strength is stronger than another.
		 * @param other Strength to compare.
		 * @return True when this value is larger.
		 */
		constexpr bool operator>(const Strength& other) const noexcept
		{
			return value > other.value;
		}
		/** @brief Tests whether this strength is no stronger than another.
		 * @param other Strength to compare.
		 * @return True when this value is less than or equal to the other value.
		 */
		constexpr bool operator<=(const Strength& other) const noexcept
		{
			return value <= other.value;
		}
		/** @brief Tests whether this strength is no weaker than another.
		 * @param other Strength to compare.
		 * @return True when this value is greater than or equal to the other value.
		 */
		constexpr bool operator>=(const Strength& other) const noexcept
		{
			return value >= other.value;
		}

		/** @brief Evaluates a five-card hand.
		 * @param cards Five card indices in the evaluator's card encoding.
		 * @return Comparable strength value; larger values rank stronger hands.
		 */
		static Strength evaluate_5(const std::array<std::uint8_t, 5>& cards);
		/** @brief Evaluates the best five-card hand from six cards.
		 * @param cards Six card indices in the evaluator's card encoding.
		 * @return Comparable strength value; larger values rank stronger hands.
		 */
		static Strength evaluate_6(const std::array<std::uint8_t, 6>& cards);
		/** @brief Evaluates the best five-card hand from seven cards.
		 * @param cards Seven card indices in the evaluator's card encoding.
		 * @return Comparable strength value; larger values rank stronger hands.
		 */
		static Strength evaluate_7(const std::array<std::uint8_t, 7>& cards);
	};
#pragma pack(pop)
	/** @brief Compares two seven-card hands by evaluated strength.
	 * @param lhs First seven-card hand.
	 * @param rhs Second seven-card hand.
	 * @return 1 if lhs is stronger, -1 if rhs is stronger, and 0 for a tie.
	 */
	int compare_7(
		const std::array<std::uint8_t, 7>& lhs,
		const std::array<std::uint8_t, 7>& rhs) noexcept;

} // namespace texas::games::hunl
