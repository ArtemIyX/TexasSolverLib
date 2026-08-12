#include "core/canonical_combo.hpp"

#include <algorithm>
#include <stdexcept>

namespace texas::core {
namespace {

constexpr std::size_t compact_pair_index(std::uint8_t low, std::uint8_t high) noexcept {
    return static_cast<std::size_t>(low) * (103U - low) / 2U + (high - low - 1U);
}

constexpr std::array<CanonicalComboCards, CANONICAL_HOLE_COMBINATION_COUNT> make_combo_cards() {
    std::array<CanonicalComboCards, CANONICAL_HOLE_COMBINATION_COUNT> result = {};
    std::size_t index = 0U;
    for (std::uint8_t low = 0U; low + 1U < DECK_CARD_COUNT; ++low) {
        for (std::uint8_t high = static_cast<std::uint8_t>(low + 1U); high < DECK_CARD_COUNT; ++high) {
            result[index++] = {low, high};
        }
    }
    return result;
}

constexpr auto kComboCards = make_combo_cards();

}  // namespace

const CanonicalComboCards& CanonicalComboView::cards(CanonicalComboId id) const {
    if (id >= CANONICAL_HOLE_COMBINATION_COUNT) {
        throw std::out_of_range("canonical combo ID is unavailable");
    }
    return kComboCards[id];
}

CanonicalComboId CanonicalComboView::id(const CanonicalComboCards& hole) const {
    if (!is_card_index(hole[0]) || !is_card_index(hole[1]) || hole[0] == hole[1]) {
        throw std::invalid_argument("canonical combo requires distinct valid cards");
    }
    const auto low = std::min(hole[0], hole[1]);
    const auto high = std::max(hole[0], hole[1]);
    return static_cast<CanonicalComboId>(compact_pair_index(low, high));
}

CanonicalComboLegalMask CanonicalComboView::legal_mask(
    const std::uint8_t* dead_cards,
    std::size_t dead_card_count) const {
    if (dead_cards == nullptr && dead_card_count != 0U) {
        throw std::invalid_argument("canonical combo dead cards are missing");
    }
    std::array<bool, DECK_CARD_COUNT> dead = {};
    for (std::size_t index = 0U; index < dead_card_count; ++index) {
        const auto card = dead_cards[index];
        if (!is_card_index(card)) {
            throw std::invalid_argument("canonical combo dead cards must be valid cards");
        }
        if (dead[card]) {
            throw std::invalid_argument("canonical combo dead cards must be distinct");
        }
        dead[card] = true;
    }

    CanonicalComboLegalMask mask;
    for (std::size_t index = 0U; index < kComboCards.size(); ++index) {
        const auto& hole = kComboCards[index];
        mask.set(index, !dead[hole[0]] && !dead[hole[1]]);
    }
    return mask;
}

const CanonicalComboView& canonical_combos() noexcept {
    static const CanonicalComboView view;
    return view;
}

}  // namespace texas::core
