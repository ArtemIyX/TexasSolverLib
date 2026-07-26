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

MultiwayTerminalResult convert_terminal_utilities(
    MultiwayTerminalResult result,
    const MultiwayRootSnapshot& root,
    int big_blind) {
    if (result.utility_units != MultiwayValueUnits::Chips || big_blind <= 0) {
        throw std::logic_error("multiway terminal adapter has invalid terminal value units");
    }
    if (root.value_units == MultiwayValueUnits::BigBlinds) {
        const auto divisor = static_cast<double>(big_blind);
        for (auto& utility : result.utilities) utility /= divisor;
    } else if (root.value_units != MultiwayValueUnits::Chips) {
        throw std::logic_error("multiway terminal adapter has unsupported root value units");
    }
    result.utility_units = root.value_units;
    return result;
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
        : state.requires_street_transition()
            ? board.size() >= minimum_board_cards &&
                  board.size() <= board_card_count(next_street(state.street()))
            : board.size() == minimum_board_cards;
    if (!valid_board_shape) {
        throw std::invalid_argument("multiway terminal adapter board is incompatible with its betting state");
    }
    return state;
}

}  // namespace

MultiwayTerminalAdapter::MultiwayTerminalAdapter(const MultiwaySolverCoordinator& coordinator)
    : coordinator_(&coordinator), root_(coordinator.request_.root()) {
    root_.validate();
}

MultiwaySamplerDealToken MultiwayTerminalAdapter::sample_private_deal(std::uint64_t seed) const {
    MultiwayPrivateWorkerScratch scratch;
    if (!coordinator_->request_.compiled_private_ranges().try_sample_into(seed, scratch)) {
        throw std::runtime_error("multiway sampler proposal collided");
    }
    MultiwayJointPrivateSample deal;
    deal.holes.assign(scratch.holes.begin(), scratch.holes.begin() + scratch.seat_count);
    deal.attempts = scratch.attempts;
    deal.chance_reach = scratch.chance_reach;
    deal.conditional_deal_probability = scratch.conditional_deal_probability;
    deal.proposal_reach = scratch.proposal_reach;
    deal.inclusion_reach = scratch.inclusion_reach;
    deal.accepted_trajectories = scratch.accepted_trajectories;
    deal.rejected_trajectories = scratch.rejected_trajectories;
    deal.discarded_trajectories = scratch.discarded_trajectories;
    return MultiwaySamplerDealToken(*coordinator_, std::move(deal));
}

const MultiwayPublicStateDescriptor& MultiwayTerminalAdapter::require_public_state(
    MultiwayPublicStateId id) const {
    const auto* state = coordinator_->public_state(id);
    if (state == nullptr) throw std::invalid_argument("multiway terminal adapter requires an admitted public state");
    return *state;
}

void MultiwayTerminalAdapter::validate_token(const MultiwaySamplerDealToken& token) const {
    if (token.coordinator_ != coordinator_) {
        throw std::invalid_argument("multiway terminal adapter received a deal token from another coordinator");
    }
}

std::vector<MultiwayBoardChanceEdge> MultiwayTerminalAdapter::canonical_board_chance_edges(
    MultiwayPublicStateId public_state,
    const MultiwaySamplerDealToken& private_deal) const {
    validate_token(private_deal);
    const auto& state = require_public_state(public_state);
    return canonical_board_chance_edges_impl(state.betting, state.board, private_deal.deal_);
}

std::vector<MultiwayBoardChanceEdge> MultiwayTerminalAdapter::canonical_board_chance_edges_impl(
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
    for (std::uint8_t card = 0; card < used.size(); ++card) {
        if (is_valid_card(card) && !used[card]) available.push_back(card);
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

std::vector<MultiwayPublicBoardChanceEdge> MultiwayTerminalAdapter::canonical_public_board_chance_edges(
    MultiwayPublicStateId parent_id,
    const MultiwaySamplerDealToken& private_deal) const {
    if (parent_id.value == 0) {
        throw std::invalid_argument("multiway public board chance requires a parent identity");
    }
    const auto edges = canonical_board_chance_edges(parent_id, private_deal);
    std::vector<MultiwayPublicBoardChanceEdge> result;
    result.reserve(edges.size());
    for (const auto& edge : edges) {
        MultiwayPublicBoardChanceEdge successor;
        successor.parent_id = parent_id;
        successor.incoming_edge.kind = MultiwayPublicParentEdgeKind::BoardChance;
        successor.incoming_edge.dealt_card = edge.dealt_card;
        successor.chance = edge;
        result.push_back(std::move(successor));
    }
    return result;
}

MultiwayStreetTransition MultiwayTerminalAdapter::apply_street_transition(
    MultiwayPublicStateId public_state) const {
    const auto& state = require_public_state(public_state);
    return apply_street_transition_impl(state.betting, state.board);
}

MultiwayStreetTransition MultiwayTerminalAdapter::apply_street_transition_impl(
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

MultiwayPublicStreetTransition MultiwayTerminalAdapter::apply_public_street_transition(
    MultiwayPublicStateId parent_id) const {
    if (parent_id.value == 0) {
        throw std::invalid_argument("multiway public street transition requires a parent identity");
    }
    MultiwayPublicStreetTransition result;
    result.parent_id = parent_id;
    result.incoming_edge.kind = MultiwayPublicParentEdgeKind::StreetTransition;
    result.transition = apply_street_transition(parent_id);
    result.incoming_edge.transition_board = result.transition.board;
    return result;
}

MultiwayTerminalResult MultiwayTerminalAdapter::resolve_terminal(
    MultiwayPublicStateId public_state,
    const MultiwaySamplerDealToken& private_deal) const {
    validate_token(private_deal);
    const auto& state = require_public_state(public_state);
    return resolve_terminal_impl(state.betting, state.board, private_deal.deal_);
}

MultiwayTerminalResult MultiwayTerminalAdapter::resolve_terminal_impl(
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
        return convert_terminal_utilities(
            settle_multiway_terminal(input), root_, betting.big_blind);
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
    return convert_terminal_utilities(
        evaluate_multiway_showdown(input), root_, betting.big_blind);
}

}  // namespace core
