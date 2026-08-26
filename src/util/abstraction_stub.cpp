#include "util/abstraction.hpp"

#if !defined(_WIN32)

#include <algorithm>
#include <stdexcept>
#include <utility>

namespace texas::util {
namespace {

std::string permuted_hole_key(
    const std::array<std::uint8_t, 2>& hole,
    std::size_t perm_index) {
    std::array<std::uint8_t, 2> cards = {
        card_to_int(rank_of(hole[0]), SUIT_PERMUTATIONS[perm_index][suit_of(hole[0])]),
        card_to_int(rank_of(hole[1]), SUIT_PERMUTATIONS[perm_index][suit_of(hole[1])]),
    };
    if (cards[1] < cards[0]) std::swap(cards[0], cards[1]);
    return card_to_string(cards[0]) + card_to_string(cards[1]);
}

}  // namespace

std::string canonicalize_board(
    const std::vector<std::uint8_t>& board,
    std::size_t* perm_index) {
    std::vector<std::pair<std::uint8_t, std::uint8_t>> best;
    std::size_t best_index = 0U;
    bool first = true;
    for (std::size_t index = 0U; index < SUIT_PERMUTATIONS.size(); ++index) {
        std::vector<std::pair<std::uint8_t, std::uint8_t>> candidate;
        candidate.reserve(board.size());
        for (const auto card : board) {
            candidate.push_back({rank_of(card), SUIT_PERMUTATIONS[index][suit_of(card)]});
        }
        std::sort(candidate.begin(), candidate.end());
        if (first || candidate < best) {
            best = std::move(candidate);
            best_index = index;
            first = false;
        }
    }
    if (perm_index != nullptr) *perm_index = best_index;
    std::string result;
    for (std::size_t index = 0U; index < best.size(); ++index) {
        if (index != 0U) result += '_';
        result += "r" + std::to_string(best[index].first) +
            "s" + std::to_string(best[index].second);
    }
    return result;
}

std::pair<std::string, std::string> canonicalize(
    const std::vector<std::uint8_t>& board,
    const std::array<std::uint8_t, 2>& hole) {
    std::size_t perm_index = 0U;
    return {canonicalize_board(board, &perm_index), permuted_hole_key(hole, perm_index)};
}

AbstractionTables load_abstraction(const std::filesystem::path&) {
    throw std::runtime_error(
        "NPZ abstraction loading requires the optional Windows compression adapter");
}

std::int32_t lookup_bucket(
    const AbstractionTables& tables,
    const std::vector<std::uint8_t>& board,
    const std::array<std::uint8_t, 2>& hole,
    Street street) {
    if (street == Street::Preflop) return -1;
    std::size_t perm_index = 0U;
    const auto board_key = canonicalize_board(board, &perm_index);
    const auto hand_key = permuted_hole_key(hole, perm_index);
    const auto& board_index = street == Street::Flop ? tables.flop_board_index
        : street == Street::Turn ? tables.turn_board_index : tables.river_board_index;
    const auto& hand_index = street == Street::Flop ? tables.flop_hand_index
        : street == Street::Turn ? tables.turn_hand_index : tables.river_hand_index;
    const auto& assignments = street == Street::Flop ? tables.flop_assignments
        : street == Street::Turn ? tables.turn_assignments : tables.river_assignments;
    const auto board_offset = board_index.at(board_key);
    const auto within = hand_index.at(board_key).at(hand_key);
    if (static_cast<std::size_t>(within) >= assignments.size() ||
        static_cast<std::size_t>(board_offset) >
            assignments.size() - static_cast<std::size_t>(within) - 1U) {
        throw std::out_of_range("abstraction bucket assignment is out of bounds");
    }
    return static_cast<std::int32_t>(assignments[board_offset + within]);
}

}  // namespace texas::util

#endif
