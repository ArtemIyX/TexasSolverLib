#pragma once

#include "core/types.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace texas::core {

// Shared Hold'em domain values. Game and solver layers consume this type
// without depending on the HUNL compatibility header.
enum class Street : std::uint8_t {
    Preflop = 0,
    Flop = 1,
    Turn = 2,
    River = 3,
    Showdown = 4,
};

inline constexpr std::uint8_t card_to_int(
    std::uint8_t rank,
    std::uint8_t suit) noexcept {
    return static_cast<std::uint8_t>((rank - 2U) * 4U + suit);
}

[[nodiscard]] std::optional<Street> street_from_u8(std::uint8_t value) noexcept;
[[nodiscard]] const char* street_token(Street street);
[[nodiscard]] std::uint8_t cards_to_deal(Street street) noexcept;
[[nodiscard]] std::uint8_t rank_of(std::uint8_t card) noexcept;
[[nodiscard]] std::uint8_t suit_of(std::uint8_t card) noexcept;
[[nodiscard]] bool is_valid_card(std::uint8_t card) noexcept;
[[nodiscard]] bool are_valid_and_distinct_cards(
    const std::uint8_t* cards,
    std::size_t count) noexcept;
[[nodiscard]] std::string card_to_string(std::uint8_t card);
[[nodiscard]] std::string sorted_card_string(const std::vector<std::uint8_t>& cards);

}  // namespace texas::core

// Transitional names for existing game-specific callers. New shared-domain
// code should include core/poker.hpp and qualify texas::core directly.
namespace texas::games::hunl {
using ::texas::core::are_valid_and_distinct_cards;
using ::texas::core::card_to_int;
using ::texas::core::card_to_string;
using ::texas::core::cards_to_deal;
using ::texas::core::is_valid_card;
using ::texas::core::rank_of;
using ::texas::core::sorted_card_string;
using ::texas::core::street_from_u8;
using ::texas::core::street_token;
using ::texas::core::suit_of;
using ::texas::core::Street;
}  // namespace texas::games::hunl
