#pragma once

#include <array>
#include <bitset>
#include <cstddef>
#include <cstdint>

namespace core {

// HUNL cards use rank * 4 + suit, so the valid deck is the contiguous range
// [8, 59]. This module deliberately does not accept compact artifact cards.
inline constexpr std::uint8_t HUNL_CARD_FIRST = 8U;
inline constexpr std::uint8_t HUNL_CARD_LAST = 59U;
inline constexpr std::size_t HUNL_CARD_COUNT = 52U;
inline constexpr std::size_t CANONICAL_HOLE_COMBINATION_COUNT = 1326U;

using CanonicalComboId = std::uint16_t;
using CanonicalComboCards = std::array<std::uint8_t, 2>;
using CanonicalComboLegalMask = std::bitset<CANONICAL_HOLE_COMBINATION_COUNT>;

[[nodiscard]] constexpr bool is_hunl_card(std::uint8_t card) noexcept {
    return card >= HUNL_CARD_FIRST && card <= HUNL_CARD_LAST;
}

// Immutable, non-owning view over all unordered HUNL hole-card pairs. IDs are
// contiguous and independent of a public board.
class CanonicalComboView {
public:
    [[nodiscard]] const CanonicalComboCards& cards(CanonicalComboId id) const;
    [[nodiscard]] CanonicalComboId id(const CanonicalComboCards& hole) const;
    [[nodiscard]] CanonicalComboLegalMask legal_mask(
        const std::uint8_t* dead_cards,
        std::size_t dead_card_count) const;

private:
    friend const CanonicalComboView& canonical_combos() noexcept;
    CanonicalComboView() = default;
};

[[nodiscard]] const CanonicalComboView& canonical_combos() noexcept;

}  // namespace core
