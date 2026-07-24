#include "solver/multiway_terminal_adapter.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <stdexcept>
#include <utility>

namespace core {
namespace {

std::uint8_t board_card_count(Street street) {
    switch (street) {
        case Street::Preflop: return 0;
        case Street::Flop: return 3;
        case Street::Turn: return 4;
        case Street::River: return 5;
        case Street::Showdown: break;
    }
    throw std::invalid_argument("multiway terminal adapter has an invalid street");
}

Street next_street(Street street) {
    switch (street) {
        case Street::Preflop: return Street::Flop;
        case Street::Flop: return Street::Turn;
        case Street::Turn: return Street::River;
        case Street::River:
        case Street::Showdown:
            break;
    }
    throw std::invalid_argument("multiway terminal adapter cannot advance past river");
}

void validate_board(const std::vector<std::uint8_t>& board) {
    if (board.size() > 5U || !are_valid_and_distinct_cards(board.data(), board.size())) {
        throw std::invalid_argument("multiway terminal adapter has an invalid board");
    }
}

void validate_private_deal(
    const MultiwayRootSnapshot& root,
    const std::vector<std::uint8_t>& board,
    const MultiwayJointPrivateSample& private_deal) {
    const auto seat_count = root.seat_order.size();
    if (private_deal.holes.size() != seat_count) {
        throw std::invalid_argument("multiway terminal adapter private deal does not match root seats");
    }
    validate_board(board);
    std::array<bool, 64> used = {};
    for (const auto card : board) used[card] = true;
    for (const auto& hole : private_deal.holes) {
        if (!are_valid_and_distinct_cards(hole.data(), hole.size()) || used[hole[0]] || used[hole[1]]) {
            throw std::invalid_argument("multiway terminal adapter private deal overlaps the board or another seat");
        }
        used[hole[0]] = true;
        used[hole[1]] = true;
    }
}

MultiwayState validate_root_consistent_state(
    const MultiwayRootSnapshot& root,
    const MultiwayBettingSnapshot& betting,
    const std::vector<std::uint8_t>& board) {
    betting.validate();
    validate_board(board);
    const auto seat_count = root.seat_order.size();
    const auto& root_betting = root.public_state.betting;
    if (betting.stacks.size() != seat_count || root_betting.stacks.size() != seat_count) {
        throw std::invalid_argument("multiway terminal adapter betting snapshot does not match root seats");
    }
    for (std::size_t seat = 0; seat < seat_count; ++seat) {
        const auto root_total = static_cast<std::int64_t>(root_betting.stacks[seat]) +
            root_betting.contributions[seat];
        const auto total = static_cast<std::int64_t>(betting.stacks[seat]) + betting.contributions[seat];
        if (total != root_total) {
            throw std::invalid_argument("multiway terminal adapter betting snapshot changes root chip accounting");
        }
        if (betting.contributions[seat] < root_betting.contributions[seat] ||
            (root_betting.folded[seat] && !betting.folded[seat]) ||
            (root_betting.all_in[seat] && !betting.all_in[seat])) {
            throw std::invalid_argument("multiway terminal adapter betting snapshot reverses root seat state");
        }
    }
    if (static_cast<std::uint8_t>(betting.street) < static_cast<std::uint8_t>(root_betting.street) ||
        board.size() < root.public_state.board.size() ||
        !std::equal(root.public_state.board.begin(), root.public_state.board.end(), board.begin())) {
        throw std::invalid_argument("multiway terminal adapter state is outside the root board/street lineage");
    }

    const auto state = MultiwayState::from_snapshot(betting);
    const auto minimum_board_cards = board_card_count(state.street());
    const auto valid_board_shape = state.requires_board_runout()
        ? board.size() >= minimum_board_cards && board.size() <= 5U
        : board.size() == minimum_board_cards;
    if (!valid_board_shape) {
        throw std::invalid_argument("multiway terminal adapter board is incompatible with its betting state");
    }
    return state;
}

}  // namespace

MultiwayTerminalAdapter::MultiwayTerminalAdapter(const MultiwayRootSnapshot& root) : root_(root) {
    root_.validate();
}

std::vector<MultiwayBoardChanceEdge> MultiwayTerminalAdapter::canonical_board_chance_edges(
    const MultiwayBettingSnapshot& betting,
    const std::vector<std::uint8_t>& board,
    const MultiwayJointPrivateSample& private_deal) const {
    const auto state = validate_root_consistent_state(root_, betting, board);
    validate_private_deal(root_, board, private_deal);

    const auto kind = state.next_node_kind();
    const auto expected_current_board = board_card_count(state.street());
    std::uint8_t maximum_board_cards = expected_current_board;
    bool chance_only_runout = false;
    if (kind == MultiwayNextNodeKind::BoardRunout) {
        maximum_board_cards = 5;
        chance_only_runout = true;
    } else if (kind == MultiwayNextNodeKind::StreetTransition) {
        maximum_board_cards = board_card_count(next_street(state.street()));
    } else {
        throw std::logic_error("multiway board chance is unavailable for this betting state");
    }
    if (board.size() < expected_current_board || board.size() >= maximum_board_cards) {
        throw std::invalid_argument("multiway board chance has an incomplete or over-complete board");
    }

    std::array<bool, 64> used = {};
    for (const auto card : board) used[card] = true;
    for (const auto& hole : private_deal.holes) {
        used[hole[0]] = true;
        used[hole[1]] = true;
    }
    std::vector<std::uint8_t> available;
    available.reserve(52U - board.size() - private_deal.holes.size() * 2U);
    for (std::uint8_t card = 0; card < 52U; ++card) {
        if (!used[card]) available.push_back(card);
    }
    if (available.empty()) {
        throw std::invalid_argument("multiway board chance has no available cards");
    }

    const auto probability = 1.0 / static_cast<Probability>(available.size());
    std::vector<MultiwayBoardChanceEdge> edges;
    edges.reserve(available.size());
    for (const auto card : available) {
        MultiwayBoardChanceEdge edge;
        edge.dealt_card = card;
        edge.board = board;
        edge.board.push_back(card);
        edge.board_runout.remaining_board_cards = static_cast<std::uint8_t>(5U - edge.board.size());
        edge.board_runout.chance_only_runout = chance_only_runout;
        edge.probability = probability;
        edges.push_back(std::move(edge));
    }
    return edges;
}

MultiwayStreetTransition MultiwayTerminalAdapter::apply_street_transition(
    const MultiwayBettingSnapshot& betting,
    const std::vector<std::uint8_t>& board) const {
    const auto state = validate_root_consistent_state(root_, betting, board);
    if (!state.requires_street_transition()) {
        throw std::logic_error("multiway street transition is unavailable for this betting state");
    }
    validate_board(board);
    const auto street = next_street(state.street());
    if (board.size() != board_card_count(street)) {
        throw std::invalid_argument("multiway street transition requires the next street's complete board");
    }

    MultiwayStreetTransition result;
    result.betting = state.begin_next_street(street, root_.next_street_first_seat).snapshot();
    result.board = board;
    result.board_runout.remaining_board_cards = static_cast<std::uint8_t>(5U - board.size());
    result.board_runout.chance_only_runout = false;
    return result;
}

MultiwayTerminalResult MultiwayTerminalAdapter::resolve_terminal(
    const MultiwayBettingSnapshot& betting,
    const std::vector<std::uint8_t>& board,
    const MultiwayJointPrivateSample& private_deal) const {
    const auto state = validate_root_consistent_state(root_, betting, board);
    validate_private_deal(root_, board, private_deal);

    if (state.next_node_kind() == MultiwayNextNodeKind::FoldTerminal) {
        MultiwayTerminalInput input;
        input.contributions = betting.contributions;
        input.folded = betting.folded;
        input.strengths.assign(betting.contributions.size(), Strength{});
        input.odd_chip_first_seat = root_.odd_chip_first_seat;
        return settle_multiway_terminal(input);
    }
    const auto showdown_ready = state.next_node_kind() == MultiwayNextNodeKind::ShowdownTerminal ||
        (state.requires_board_runout() && board.size() == 5U);
    if (!showdown_ready) {
        throw std::logic_error("multiway terminal resolution requires a fold terminal or completed showdown");
    }

    MultiwayShowdownInput input;
    input.board = board;
    input.holes = private_deal.holes;
    input.contributions = betting.contributions;
    input.folded = betting.folded;
    input.odd_chip_first_seat = root_.odd_chip_first_seat;
    return evaluate_multiway_showdown(input);
}

}  // namespace core
