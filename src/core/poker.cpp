#include "core/poker.hpp"

#include <algorithm>
#include <stdexcept>

namespace texas::core {

std::optional<Street> street_from_u8(std::uint8_t value) noexcept {
    switch (value) {
        case 0: return Street::Preflop;
        case 1: return Street::Flop;
        case 2: return Street::Turn;
        case 3: return Street::River;
        case 4: return Street::Showdown;
        default: return std::nullopt;
    }
}

const char* street_token(Street street) {
    switch (street) {
        case Street::Preflop: return "p";
        case Street::Flop: return "f";
        case Street::Turn: return "t";
        case Street::River: return "r";
        case Street::Showdown: return "s";
    }
    throw std::logic_error("invalid Street");
}

std::uint8_t cards_to_deal(Street street) noexcept {
    switch (street) {
        case Street::Flop: return 3U;
        case Street::Turn:
        case Street::River: return 1U;
        default: return 0U;
    }
}

std::uint8_t rank_of(std::uint8_t card) noexcept {
    return static_cast<std::uint8_t>(card / 4U + 2U);
}

std::uint8_t suit_of(std::uint8_t card) noexcept {
    return static_cast<std::uint8_t>(card % 4U);
}

bool is_valid_card(std::uint8_t card) noexcept {
    return card < DECK_CARD_COUNT;
}

bool are_valid_and_distinct_cards(const std::uint8_t* cards, std::size_t count) noexcept {
    if (cards == nullptr && count != 0U) return false;
    std::array<bool, 64> seen = {};
    for (std::size_t index = 0U; index < count; ++index) {
        const auto card = cards[index];
        if (!is_valid_card(card) || seen[card]) return false;
        seen[card] = true;
    }
    return true;
}

std::string card_to_string(std::uint8_t card) {
    static constexpr char ranks[] = "23456789TJQKA";
    static constexpr char suits[] = "shdc";
    const auto rank = rank_of(card);
    const auto suit = suit_of(card);
    if (rank < 2U || rank > 14U || suit > 3U) {
        throw std::invalid_argument("card_to_string received invalid encoded card");
    }
    return {ranks[rank - 2U], suits[suit]};
}

std::string sorted_card_string(const std::vector<std::uint8_t>& cards) {
    auto sorted = cards;
    std::sort(sorted.begin(), sorted.end());
    std::string result;
    result.reserve(sorted.size() * 2U);
    for (const auto card : sorted) result += card_to_string(card);
    return result;
}

}  // namespace texas::core
