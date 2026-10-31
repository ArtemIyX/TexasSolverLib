#pragma once

/** @file
 * @brief Declares shared Hold'em card, street and formatting helpers.
 */

#include "core/types.hpp"
#include "core/street_types.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace texas::core
{

	/** @brief Encodes a rank and suit as the deck index used by TexasSolver.
	 *
	 * Rank values are 2 through 14 and suit values are 0 through 3. This helper
	 * performs the encoding without validating either input.
	 * @param rank Card rank, with 11 through 14 denoting jack through ace.
	 * @param suit Suit index in [0, 4).
	 * @return Encoded card index for the supplied values.
	 */
	inline constexpr std::uint8_t card_to_int(
		std::uint8_t rank,
		std::uint8_t suit) noexcept
	{
		return static_cast<std::uint8_t>((rank - 2U) * 4U + suit);
	}

	/** @brief Converts a serialized street value to a known street.
	 * @param value Numeric street value, where 0 through 4 identify Street enumerators.
	 * @return The corresponding street, or std::nullopt for an unknown value.
	 */
	[[nodiscard]] std::optional<Street> street_from_u8(std::uint8_t value) noexcept;
	/** @brief Returns the one-letter token used for a street.
	 * @param street Street to encode.
	 * @return "p", "f", "t", "r", or "s" for preflop through showdown.
	 * @throws std::logic_error If street is not a recognized enumerator.
	 */
	[[nodiscard]] const char* street_token(Street street);
	/** @brief Returns the number of community cards dealt when entering a street.
	 * @param street Street being entered.
	 * @return Three for the flop, one for the turn or river, and zero otherwise.
	 */
	[[nodiscard]] std::uint8_t cards_to_deal(Street street) noexcept;
	/** @brief Decodes the rank of a valid encoded card index.
	 * @param card Encoded card index in [0, DECK_CARD_COUNT).
	 * @return Rank in [2, 14].
	 */
	[[nodiscard]] std::uint8_t rank_of(std::uint8_t card) noexcept;
	/** @brief Decodes the suit of a valid encoded card index.
	 * @param card Encoded card index in [0, DECK_CARD_COUNT).
	 * @return Suit index in [0, 4).
	 */
	[[nodiscard]] std::uint8_t suit_of(std::uint8_t card) noexcept;
	/** @brief Reports whether an encoded card index is in the standard deck. */
	[[nodiscard]] bool is_valid_card(std::uint8_t card) noexcept;
	/** @brief Validates that a card range contains distinct standard-deck indices.
	 * @param cards Pointer to card indices; may be null only when count is zero.
	 * @param count Number of card indices to inspect.
	 * @return True if every entry is valid and distinct; false otherwise.
	 */
	[[nodiscard]] bool are_valid_and_distinct_cards(
		const std::uint8_t* cards,
		std::size_t count) noexcept;
	/** @brief Formats a card as a rank character followed by its suit character.
	 * @param card Encoded card index.
	 * @return Two-character token using suits s, h, d and c.
	 * @throws std::invalid_argument If card does not encode a standard-deck card.
	 */
	[[nodiscard]] std::string card_to_string(std::uint8_t card);
	/** @brief Formats cards in ascending encoded-card order.
	 * @param cards Encoded card indices to format.
	 * @return Concatenated two-character card tokens.
	 * @throws std::invalid_argument If any entry is not a valid card index.
	 */
	[[nodiscard]] std::string sorted_card_string(const std::vector<std::uint8_t>& cards);

} // namespace texas::core

/** @brief Transitional aliases for the shared poker-domain API.
 * New shared-domain code should use texas::core directly.
 */
namespace texas::games::hunl
{
	using ::texas::core::are_valid_and_distinct_cards;
	using ::texas::core::card_to_int;
	using ::texas::core::card_to_string;
	using ::texas::core::cards_to_deal;
	using ::texas::core::is_valid_card;
	using ::texas::core::rank_of;
	using ::texas::core::sorted_card_string;
	using ::texas::core::Street;
	using ::texas::core::street_from_u8;
	using ::texas::core::street_token;
	using ::texas::core::suit_of;
} // namespace texas::games::hunl
