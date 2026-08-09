#include "core/canonical_combo.hpp"

#include <algorithm>
#include <stdexcept>

namespace core {
namespace {

constexpr std::size_t compact_pair_index(std::uint8_t low, std::uint8_t high) noexcept {
    return static_cast<std::size_t>(low) * (103U - low) / 2U + (high - low - 1U);
}

constexpr std::array<CanonicalComboCards, CANONICAL_HOLE_COMBINATION_COUNT> make_combo_cards() {
    std::array<CanonicalComboCards, CANONICAL_HOLE_COMBINATION_COUNT> result = {};
    std::size_t index = 0U;
    for (std::uint8_t low = HUNL_CARD_FIRST; low < HUNL_CARD_LAST; ++low) {
        for (std::uint8_t high = static_cast<std::uint8_t>(low + 1U); high <= HUNL_CARD_LAST; ++high) {
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
    if (!is_hunl_card(hole[0]) || !is_hunl_card(hole[1]) || hole[0] == hole[1]) {
        throw std::invalid_argument("canonical combo requires distinct HUNL cards");
    }
    const auto low = std::min(hole[0], hole[1]);
    const auto high = std::max(hole[0], hole[1]);
    return static_cast<CanonicalComboId>(compact_pair_index(
        static_cast<std::uint8_t>(low - HUNL_CARD_FIRST),
        static_cast<std::uint8_t>(high - HUNL_CARD_FIRST)));
}

CanonicalComboLegalMask CanonicalComboView::legal_mask(
    const std::uint8_t* dead_cards,
    std::size_t dead_card_count) const {
    if (dead_cards == nullptr && dead_card_count != 0U) {
        throw std::invalid_argument("canonical combo dead cards are missing");
    }
    std::array<bool, HUNL_CARD_COUNT> dead = {};
    for (std::size_t index = 0U; index < dead_card_count; ++index) {
        const auto card = dead_cards[index];
        if (!is_hunl_card(card)) {
            throw std::invalid_argument("canonical combo dead cards must be HUNL cards");
        }
        const auto compact = static_cast<std::size_t>(card - HUNL_CARD_FIRST);
        if (dead[compact]) {
            throw std::invalid_argument("canonical combo dead cards must be distinct");
        }
        dead[compact] = true;
    }

    CanonicalComboLegalMask mask;
    for (std::size_t index = 0U; index < kComboCards.size(); ++index) {
        const auto& hole = kComboCards[index];
        mask.set(index, !dead[hole[0] - HUNL_CARD_FIRST] && !dead[hole[1] - HUNL_CARD_FIRST]);
    }
    return mask;
}

const CanonicalComboView& canonical_combos() noexcept {
    static const CanonicalComboView view;
    return view;
}

}  // namespace core
