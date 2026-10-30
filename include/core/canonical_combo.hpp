#pragma once

/** @file
 * @brief Declares canonical indexing for unordered two-card combinations.
 */

#include "core/types.hpp"

#include <array>
#include <bitset>
#include <cstddef>
#include <cstdint>

namespace texas::core
{

	/// Number of unordered two-card combinations in a standard 52-card deck.
	inline constexpr std::size_t CANONICAL_HOLE_COMBINATION_COUNT = 1326U;

	/// Compact index in the canonical two-card combination table.
	using CanonicalComboId = std::uint16_t;
	/// Two card indices forming an unordered hole-card combination.
	using CanonicalComboCards = std::array<std::uint8_t, 2>;
	/// One legality bit for each canonical hole-card combination.
	using CanonicalComboLegalMask = std::bitset<CANONICAL_HOLE_COMBINATION_COUNT>;

	/** @brief Reports whether a card index belongs to the standard deck. */
	[[nodiscard]] constexpr bool is_card_index(std::uint8_t card) noexcept
	{
		return card < DECK_CARD_COUNT;
	}

	/** @brief Provides read-only lookup of all canonical unordered hole-card pairs.
	 *
	 * IDs follow ascending card-index pair order and do not depend on a public board.
	 */
	class CanonicalComboView
	{
	public:
		/** @brief Returns the pair assigned to a canonical combination ID.
		 * @param id Combination ID in [0, CANONICAL_HOLE_COMBINATION_COUNT).
		 * @return The two card indices in ascending order.
		 * @throws std::out_of_range If id is outside the canonical table.
		 */
		[[nodiscard]] const CanonicalComboCards& cards(CanonicalComboId id) const;
		/** @brief Finds the canonical ID for an unordered pair of distinct cards.
		 * @param hole Two card indices; their order does not affect the result.
		 * @return The pair's ID in the canonical table.
		 * @throws std::invalid_argument If either card is outside the deck or both are equal.
		 */
		[[nodiscard]] CanonicalComboId id(const CanonicalComboCards& hole) const;
		/** @brief Builds a mask of combinations that contain no dead card.
		 * @param dead_cards Pointer to card indices; may be null only when the count is zero.
		 * @param dead_card_count Number of entries in dead_cards.
		 * @return A bitset indexed by CanonicalComboId; set bits denote legal pairs.
		 * @throws std::invalid_argument If the pointer/count pair is inconsistent or a card is invalid or repeated.
		 */
		[[nodiscard]] CanonicalComboLegalMask legal_mask(
			const std::uint8_t* dead_cards,
			std::size_t dead_card_count) const;

	private:
		friend const CanonicalComboView& canonical_combos() noexcept;
		CanonicalComboView() = default;
	};

	/** @brief Returns the shared canonical combination lookup table.
	 * @return A process-lifetime immutable view.
	 */
	[[nodiscard]] const CanonicalComboView& canonical_combos() noexcept;

} // namespace texas::core
