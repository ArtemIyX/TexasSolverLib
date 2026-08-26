#include "games/hunl_eval.hpp"

#include <phevaluator/phevaluator.h>

#include <array>

namespace texas::games::hunl {

namespace {

std::uint8_t to_phevaluator_card(std::uint8_t card) {
    return card;
}

Strength from_phevaluator_rank(int rank) {
    return Strength{static_cast<std::uint64_t>(7463 - rank)};
}

}  // namespace
Strength Strength::evaluate_5(const std::array<std::uint8_t, 5>& cards) {
    return from_phevaluator_rank(evaluate_5cards(
        to_phevaluator_card(cards[0]), to_phevaluator_card(cards[1]), to_phevaluator_card(cards[2]),
        to_phevaluator_card(cards[3]), to_phevaluator_card(cards[4])));
}

Strength Strength::evaluate_6(const std::array<std::uint8_t, 6>& cards) {
    return from_phevaluator_rank(evaluate_6cards(
        to_phevaluator_card(cards[0]), to_phevaluator_card(cards[1]), to_phevaluator_card(cards[2]),
        to_phevaluator_card(cards[3]), to_phevaluator_card(cards[4]), to_phevaluator_card(cards[5])));
}

Strength Strength::evaluate_7(const std::array<std::uint8_t, 7>& cards) {
    return from_phevaluator_rank(evaluate_7cards(
        to_phevaluator_card(cards[0]),
        to_phevaluator_card(cards[1]),
        to_phevaluator_card(cards[2]),
        to_phevaluator_card(cards[3]),
        to_phevaluator_card(cards[4]),
        to_phevaluator_card(cards[5]),
        to_phevaluator_card(cards[6])));
}

int compare_7(
    const std::array<std::uint8_t, 7>& lhs,
    const std::array<std::uint8_t, 7>& rhs) noexcept {
    const auto lhs_strength = Strength::evaluate_7(lhs);
    const auto rhs_strength = Strength::evaluate_7(rhs);
    if (lhs_strength > rhs_strength) {
        return 1;
    }
    if (rhs_strength > lhs_strength) {
        return -1;
    }
    return 0;
}

}  // namespace texas::games::hunl


