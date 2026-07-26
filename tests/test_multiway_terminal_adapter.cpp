#include "solver/multiway_terminal_adapter.hpp"
#include "test_harness.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <utility>
#include <vector>

namespace {

constexpr std::uint8_t c(std::uint8_t rank, std::uint8_t suit) {
    return core::card_to_int(rank, suit);
}

const std::vector<std::uint8_t> kFlop = {c(2, 0), c(7, 1), c(9, 2)};

core::MultiwayJointPrivateSample private_deal() {
    core::MultiwayJointPrivateSample deal;
    deal.holes = {{c(14, 0), c(13, 0)}, {c(12, 0), c(11, 0)}, {c(10, 0), c(8, 0)}};
    return deal;
}

core::MultiwayState flop_state(int stack = 1000) {
    core::MultiwayGameConfig game;
    game.starting_stacks = {stack, stack, stack};
    game.initial_contributions = {0, 0, 0};
    game.initial_street_contributions = {0, 0, 0};
    game.first_player = 0;
    game.street = core::Street::Flop;
    return core::MultiwayState::initial(game);
}

core::MultiwayState preflop_state(int stack = 1000) {
    core::MultiwayGameConfig game;
    game.starting_stacks = {stack, stack, stack};
    game.initial_contributions = {0, 0, 0};
    game.initial_street_contributions = {0, 0, 0};
    game.first_player = 0;
    game.street = core::Street::Preflop;
    return core::MultiwayState::initial(game);
}

std::vector<core::MultiwayActionDescriptor> action_descriptors(const core::MultiwayState& state) {
    std::vector<core::MultiwayActionDescriptor> descriptors;
    const auto actions = state.legal_actions();
    descriptors.reserve(actions.size());
    for (std::size_t index = 0; index < actions.size(); ++index) {
        const auto action = actions[index];
        const auto seat = static_cast<std::size_t>(state.current_player());
        const auto current = state.street_contributions()[seat];
        const auto target = action == core::MultiwayAction::Bet || action == core::MultiwayAction::Raise
            ? state.current_bet() + state.last_full_raise_size()
            : action == core::MultiwayAction::AllIn
                ? current + state.stacks()[seat]
                : action == core::MultiwayAction::Call
                    ? std::min(state.current_bet(), current + state.stacks()[seat])
                    : current;
        descriptors.push_back({action, static_cast<std::uint32_t>(index), target, 8800});
    }
    return descriptors;
}

core::MultiwayRootSnapshot root_for_betting_state(
    const core::MultiwayState& state,
    const std::vector<std::uint8_t>& board = kFlop,
    core::PlayerId first_seat = 0,
    core::PlayerId odd_chip_first_seat = 0) {
    core::MultiwayRootSnapshot root;
    root.public_state.id = {42};
    root.public_state.canonical_history_id = 4242;
    root.public_state.betting = state.snapshot();
    root.public_state.board = board;
    root.public_state.board_runout.remaining_board_cards = static_cast<std::uint8_t>(5U - board.size());
    root.public_state.board_runout.chance_only_runout = state.requires_board_runout();
    root.public_state.legal_actions = action_descriptors(state);
    root.root_infoset = {{42}, state.current_player()};
    root.seat_order = {first_seat, static_cast<core::PlayerId>((first_seat + 1) % 3),
                       static_cast<core::PlayerId>((first_seat + 2) % 3)};
    root.next_street_first_seat = first_seat;
    root.odd_chip_first_seat = odd_chip_first_seat;
    root.private_ranges.board = board;
    const auto deal = private_deal();
    for (const auto& hole : deal.holes) root.private_ranges.ranges.push_back({{hole, 1.0}});
    root.action_abstraction_version = 1;
    root.leaf_model_version = 1;
    return root;
}

core::MultiwaySolverLimits limits() {
    core::MultiwaySolverLimits result;
    result.max_public_states = 16;
    result.max_sparse_rows = 1;
    result.max_sparse_values = 3;
    result.max_worker_delta_entries = 1;
    return result;
}

struct AdapterFixture {
    explicit AdapterFixture(core::MultiwayRootSnapshot root)
        : request(std::move(root), cfr_config(), limits()), coordinator(request), adapter(coordinator) {}

    core::MultiwayPublicStateDescriptor admit_chance_child(
        const core::MultiwayPublicStateDescriptor& parent,
        const core::MultiwayBoardChanceEdge& chance) {
        auto child = parent;
        child.id = {next_id++};
        child.parent_id = parent.id;
        child.canonical_history_id = next_history_id++;
        child.incoming_edge = {};
        child.incoming_edge.kind = core::MultiwayPublicParentEdgeKind::BoardChance;
        child.incoming_edge.dealt_card = chance.dealt_card;
        child.incoming_edge.dealt_cards = chance.dealt_cards;
        child.board = chance.board;
        child.board_runout = chance.board_runout;
        coordinator.admit_public_state(child);
        return child;
    }

    core::MultiwayPublicStateDescriptor admit_action_child(
        const core::MultiwayPublicStateDescriptor& parent,
        std::size_t action_index) {
        auto child = parent;
        const auto action = parent.legal_actions.at(action_index);
        child.id = {next_id++};
        child.parent_id = parent.id;
        child.canonical_history_id = next_history_id++;
        child.incoming_edge = {};
        child.incoming_edge.kind = core::MultiwayPublicParentEdgeKind::BettingAction;
        child.incoming_edge.action = action;
        child.history.push_back({parent.betting.current_player, action});
        child.betting = core::MultiwayState::from_snapshot(parent.betting)
                            .apply(action.action, action.target_street_contribution)
                            .snapshot();
        child.legal_actions = action_descriptors(core::MultiwayState::from_snapshot(child.betting));
        coordinator.admit_public_state(child);
        return child;
    }

    core::MultiwayPublicStateDescriptor admit_street_child(
        const core::MultiwayPublicStateDescriptor& parent,
        const core::MultiwayStreetTransition& transition) {
        auto child = parent;
        child.id = {next_id++};
        child.parent_id = parent.id;
        child.canonical_history_id = next_history_id++;
        child.incoming_edge = {};
        child.incoming_edge.kind = core::MultiwayPublicParentEdgeKind::StreetTransition;
        child.incoming_edge.transition_board = transition.board;
        child.betting = transition.betting;
        child.board = transition.board;
        child.board_runout = transition.board_runout;
        child.legal_actions = action_descriptors(core::MultiwayState::from_snapshot(child.betting));
        coordinator.admit_public_state(child);
        return child;
    }

    static core::MultiwayCFRConfig cfr_config() {
        core::MultiwayCFRConfig result;
        result.player_count = 3;
        return result;
    }

    core::MultiwaySolveRequest request;
    core::MultiwaySolverCoordinator coordinator;
    core::MultiwayTerminalAdapter adapter;
    std::uint64_t next_id = 43;
    std::uint64_t next_history_id = 4243;
};

}  // namespace

TEST_CASE(multiway_terminal_adapter_enumerates_canonical_exclusive_board_chance_edges) {
    AdapterFixture fixture(root_for_betting_state(flop_state()));
    const auto deal = fixture.adapter.sample_private_deal(1);
    auto state = fixture.request.root().public_state;
    state = fixture.admit_action_child(state, 0);
    state = fixture.admit_action_child(state, 0);
    state = fixture.admit_action_child(state, 0);
    const auto edges = fixture.adapter.canonical_board_chance_edges(state.id, deal);

    EXPECT_EQ(edges.size(), std::size_t{43});
    double total_probability = 0.0;
    std::array<bool, 64> excluded = {};
    for (const auto card : kFlop) excluded[card] = true;
    for (const auto& hole : private_deal().holes) {
        excluded[hole[0]] = true;
        excluded[hole[1]] = true;
    }
    for (std::size_t index = 0; index < edges.size(); ++index) {
        const auto& edge = edges[index];
        if (index != 0U) EXPECT_TRUE(edges[index - 1U].dealt_card < edge.dealt_card);
        EXPECT_TRUE(!excluded[edge.dealt_card]);
        EXPECT_EQ(edge.board.size(), std::size_t{4});
        EXPECT_EQ(edge.board[0], kFlop[0]);
        EXPECT_EQ(edge.board[1], kFlop[1]);
        EXPECT_EQ(edge.board[2], kFlop[2]);
        EXPECT_EQ(edge.board[3], edge.dealt_card);
        EXPECT_EQ(edge.board_runout.remaining_board_cards, 1U);
        EXPECT_TRUE(!edge.board_runout.chance_only_runout);
        EXPECT_NEAR(edge.probability, 1.0 / 43.0, 1e-12);
        total_probability += edge.probability;
    }
    EXPECT_NEAR(total_probability, 1.0, 1e-12);
}

TEST_CASE(multiway_terminal_adapter_collapses_preflop_flop_orders_to_canonical_combinations) {
    AdapterFixture fixture(root_for_betting_state(preflop_state(), {}));
    const auto deal = fixture.adapter.sample_private_deal(1);
    auto state = fixture.request.root().public_state;
    state = fixture.admit_action_child(state, 0);
    state = fixture.admit_action_child(state, 0);
    state = fixture.admit_action_child(state, 0);

    const auto edges = fixture.adapter.canonical_board_chance_edges(state.id, deal);
    EXPECT_EQ(edges.size(), std::size_t{15180});
    double total_probability = 0.0;
    for (const auto& edge : edges) {
        EXPECT_EQ(edge.dealt_cards.size(), std::size_t{3});
        EXPECT_TRUE(std::is_sorted(edge.dealt_cards.begin(), edge.dealt_cards.end()));
        EXPECT_EQ(edge.board, edge.dealt_cards);
        EXPECT_EQ(edge.board_runout.remaining_board_cards, 2U);
        total_probability += edge.probability;
    }
    EXPECT_NEAR(total_probability, 1.0, 1e-12);
}

TEST_CASE(multiway_terminal_adapter_uses_root_owned_first_player_for_street_transition) {
    AdapterFixture fixture(root_for_betting_state(flop_state(), kFlop, 2));
    const auto deal = fixture.adapter.sample_private_deal(1);
    auto state = fixture.request.root().public_state;
    state = fixture.admit_action_child(state, 0);
    state = fixture.admit_action_child(state, 0);
    state = fixture.admit_action_child(state, 0);
    const auto chance = fixture.adapter.canonical_board_chance_edges(state.id, deal).front();
    const auto turn = fixture.admit_chance_child(state, chance);
    const auto transition = fixture.adapter.apply_street_transition(turn.id);

    EXPECT_EQ(transition.betting.street, core::Street::Turn);
    EXPECT_EQ(transition.betting.current_player, 2);
    EXPECT_EQ(transition.betting.current_bet, 0);
    EXPECT_EQ(transition.board, chance.board);
    EXPECT_EQ(transition.board_runout.remaining_board_cards, 1U);
    EXPECT_TRUE(!transition.board_runout.chance_only_runout);

    const auto next_state = fixture.admit_street_child(turn, transition);
    EXPECT_EQ(next_state.betting.street, core::Street::Turn);
}

TEST_CASE(multiway_terminal_adapter_delegates_fold_terminals) {
    AdapterFixture fixture(root_for_betting_state(flop_state()));
    const auto deal = fixture.adapter.sample_private_deal(1);
    auto state = fixture.request.root().public_state;
    state = fixture.admit_action_child(state, 1);
    state = fixture.admit_action_child(state, 0);
    state = fixture.admit_action_child(state, 0);
    const auto result = fixture.adapter.resolve_terminal(state.id, deal);
    core::MultiwayTerminalInput expected_input;
    expected_input.contributions = state.betting.contributions;
    expected_input.folded = state.betting.folded;
    expected_input.strengths.assign(expected_input.contributions.size(), core::Strength{});
    expected_input.odd_chip_first_seat = 0;
    const auto expected = core::settle_multiway_terminal(expected_input);
    EXPECT_EQ(result.payouts, expected.payouts);
    EXPECT_EQ(result.refunds, expected.refunds);
    EXPECT_EQ(result.utilities, expected.utilities);
}

TEST_CASE(multiway_terminal_adapter_requires_admitted_state_and_bound_deal_token) {
    AdapterFixture fixture(root_for_betting_state(flop_state()));
    const auto deal = fixture.adapter.sample_private_deal(1);
    EXPECT_THROW(fixture.adapter.canonical_board_chance_edges({0}, deal), std::invalid_argument);
    EXPECT_THROW(fixture.adapter.canonical_board_chance_edges({999}, deal), std::invalid_argument);
    EXPECT_THROW(fixture.adapter.canonical_public_board_chance_edges({0}, deal), std::invalid_argument);

    AdapterFixture other(root_for_betting_state(flop_state()));
    EXPECT_THROW(other.adapter.canonical_board_chance_edges({42}, deal), std::invalid_argument);
}
