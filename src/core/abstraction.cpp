#include "core/abstraction.hpp"

#include <algorithm>
#include <fstream>
#include <sstream>
#include <stdexcept>

namespace core {

namespace {

std::string rank_char(std::uint8_t rank) {
    static constexpr char RANKS[] = "23456789TJQKA";
    return std::string(1, RANKS[rank - 2]);
}

std::string suit_char(std::uint8_t suit) {
    static constexpr char SUITS[] = "shdc";
    return std::string(1, SUITS[suit]);
}

std::string card_key(std::uint8_t card) {
    return rank_char(rank_of(card)) + suit_char(suit_of(card));
}

}  // namespace

std::string canonicalize_board(const std::vector<std::uint8_t>& board, std::size_t* perm_index) {
    std::vector<std::pair<std::uint8_t, std::uint8_t>> best;
    std::size_t best_idx = 0;
    bool first = true;
    for (std::size_t i = 0; i < SUIT_PERMUTATIONS.size(); ++i) {
        std::vector<std::pair<std::uint8_t, std::uint8_t>> pairs;
        pairs.reserve(board.size());
        for (auto c : board) {
            pairs.push_back({rank_of(c), SUIT_PERMUTATIONS[i][suit_of(c)]});
        }
        std::sort(pairs.begin(), pairs.end());
        if (first || pairs < best) {
            best = std::move(pairs);
            best_idx = i;
            first = false;
        }
    }
    if (perm_index) *perm_index = best_idx;
    std::string out;
    for (std::size_t i = 0; i < best.size(); ++i) {
        if (i) out += '_';
        out += 'r';
        out += std::to_string(best[i].first);
        out += 's';
        out += std::to_string(best[i].second);
    }
    return out;
}

std::pair<std::string, std::string> canonicalize(const std::vector<std::uint8_t>& board, const std::array<std::uint8_t, 2>& hole) {
    std::size_t perm_index = 0;
    const auto board_key = canonicalize_board(board, &perm_index);
    auto c0 = hole[0];
    auto c1 = hole[1];
    c0 = static_cast<std::uint8_t>(rank_of(c0) * 4U + SUIT_PERMUTATIONS[perm_index][suit_of(c0)]);
    c1 = static_cast<std::uint8_t>(rank_of(c1) * 4U + SUIT_PERMUTATIONS[perm_index][suit_of(c1)]);
    if (c1 < c0) std::swap(c0, c1);
    const auto hand_key = card_key(c0) + card_key(c1);
    return {board_key, hand_key};
}

AbstractionTables load_abstraction(const std::filesystem::path&) {
    throw std::runtime_error("abstraction npz loading not implemented in C++ yet");
}

std::int32_t lookup_bucket(
    const AbstractionTables& tables,
    const std::vector<std::uint8_t>& board,
    const std::array<std::uint8_t, 2>& hole,
    Street street) {
    if (street == Street::Preflop) return -1;
    const auto [board_key, hand_key] = canonicalize(board, hole);
    const auto& board_index = street == Street::Flop ? tables.flop_board_index
                              : street == Street::Turn ? tables.turn_board_index
                              : tables.river_board_index;
    const auto& hand_index = street == Street::Flop ? tables.flop_hand_index
                             : street == Street::Turn ? tables.turn_hand_index
                             : tables.river_hand_index;
    const auto b = board_index.at(board_key);
    const auto& per_board = hand_index.at(board_key);
    const auto within = per_board.at(hand_key);
    const auto& assignments = street == Street::Flop ? tables.flop_assignments
                                : street == Street::Turn ? tables.turn_assignments
                                : tables.river_assignments;
    return static_cast<std::int32_t>(assignments.at(b + within));
}

}  // namespace core
