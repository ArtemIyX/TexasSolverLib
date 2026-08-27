#include "solver/multiway/abstraction/multiway_terminal_adapter.hpp"
#include "solver/multiway/abstraction/multiway_public_builder.hpp"
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
    return texas::card_to_int(rank, suit);
}

const std::vector<std::uint8_t> kFlop = {c(2, 0), c(7, 1), c(9, 2)};

texas::MultiwayJointPrivateSample private_deal() {
    texas::MultiwayJointPrivateSample deal;
    deal.holes = {{c(14, 0), c(13, 0)}, {c(12, 0), c(11, 0)}, {c(10, 0), c(8, 0)}};
    return deal;
}

texas::MultiwayState flop_state(int stack = 1000) {
    texas::MultiwayGameConfig game;
    game.starting_stacks = {stack, stack, stack};
    game.initial_contributions = {0, 0, 0};
    game.initial_street_contributions = {0, 0, 0};
    game.first_player = 0;
    game.street = texas::Street::Flop;
    return texas::MultiwayState::initial(game);
}

texas::MultiwayState preflop_state(int stack = 1000) {
    texas::MultiwayGameConfig game;
    game.starting_stacks = {stack, stack, stack};
    game.initial_contributions = {0, 0, 0};
    game.initial_street_contributions = {0, 0, 0};
    game.first_player = 0;
    game.street = texas::Street::Preflop;
    return texas::MultiwayState::initial(game);
}

std::vector<texas::MultiwayActionDescriptor> action_descriptors(const texas::MultiwayState& state) {
    std::vector<texas::MultiwayActionDescriptor> descriptors;
    const auto actions = state.legal_actions();
    descriptors.reserve(actions.size());
    for (std::size_t index = 0; index < actions.size(); ++index) {
        const auto action = actions[index];
        const auto seat = static_cast<std::size_t>(state.current_player());
        const auto current = state.street_contributions()[seat];
        const auto target = action == texas::MultiwayAction::Bet || action == texas::MultiwayAction::Raise
            ? state.current_bet() + state.last_full_raise_size()
            : action == texas::MultiwayAction::AllIn
                ? current + state.stacks()[seat]
                : action == texas::MultiwayAction::Call
                    ? std::min(state.current_bet(), current + state.stacks()[seat])
                    : current;
        descriptors.push_back({action, static_cast<std::uint32_t>(index), target, 8800});
    }
    return descriptors;
}

texas::MultiwayRootSnapshot root_for_betting_state(
    const texas::MultiwayState& state,
    const std::vector<std::uint8_t>& board = kFlop,
    texas::PlayerId first_seat = 0,
    texas::PlayerId odd_chip_first_seat = 0) {
    texas::MultiwayRootSnapshot root;
    root.public_state = texas::MultiwayPublicBuilder::make_root(
        state.snapshot(), board, action_descriptors(state));
    root.root_infoset = {root.public_state.id, state.current_player()};
    root.seat_order = {first_seat, static_cast<texas::PlayerId>((first_seat + 1) % 3),
                       static_cast<texas::PlayerId>((first_seat + 2) % 3)};
    root.next_street_first_seat = first_seat;
    root.odd_chip_first_seat = odd_chip_first_seat;
    root.private_ranges.board = board;
    const auto deal = private_deal();
    for (const auto& hole : deal.holes) root.private_ranges.ranges.push_back({{hole, 1.0}});
    root.action_abstraction_version = 1;
    root.leaf_model_version = 1;
    return root;
}

texas::MultiwaySolverLimits limits() {
    texas::MultiwaySolverLimits result;
    result.max_public_states = 16;
    result.max_sparse_rows = 1;
    result.max_sparse_values = 3;
    result.max_worker_delta_entries = 1;
    return result;
}

struct AdapterFixture {
    explicit AdapterFixture(texas::MultiwayRootSnapshot root)
        : request(std::move(root), cfr_config(), limits()), coordinator(request), adapter(coordinator) {}

    texas::MultiwayPublicStateDescriptor admit_chance_child(
        const texas::MultiwayPublicStateDescriptor& parent,
        const texas::MultiwayBoardChanceEdge& chance) {
        texas::MultiwayPublicBoardChanceEdge edge;
        edge.parent_id = parent.id;
        edge.incoming_edge.kind = texas::MultiwayPublicParentEdgeKind::BoardChance;
        edge.chance = chance;
        const auto child = texas::MultiwayPublicBuilder::make_board_chance_child(
            parent, edge, parent.legal_actions);
        coordinator.admit_public_state(child);
        return child;
    }

    texas::MultiwayPublicStateDescriptor admit_action_child(
        const texas::MultiwayPublicStateDescriptor& parent,
        std::size_t action_index) {
        const auto action = parent.legal_actions.at(action_index);
        const auto state = texas::MultiwayState::from_snapshot(parent.betting)
                               .apply(action.action, action.target_street_contribution);
        const auto child = texas::MultiwayPublicBuilder::make_action_child(
            parent, static_cast<std::uint32_t>(action_index), action_descriptors(state));
        coordinator.admit_public_state(child);
        return child;
    }

    texas::MultiwayPublicStateDescriptor admit_street_child(
        const texas::MultiwayPublicStateDescriptor& parent,
        const texas::MultiwayStreetTransition& transition) {
        texas::MultiwayPublicStreetTransition public_transition;
        public_transition.parent_id = parent.id;
        public_transition.incoming_edge.kind = texas::MultiwayPublicParentEdgeKind::StreetTransition;
        public_transition.transition = transition;
        const auto child = texas::MultiwayPublicBuilder::make_street_transition_child(
            parent, public_transition,
            action_descriptors(texas::MultiwayState::from_snapshot(transition.betting)));
        coordinator.admit_public_state(child);
        return child;
    }

    static texas::MultiwayCFRConfig cfr_config() {
        texas::MultiwayCFRConfig result;
        result.player_count = 3;
        return result;
    }

    texas::MultiwaySolveRequest request;
    texas::MultiwaySolverCoordinator coordinator;
    texas::MultiwayTerminalAdapter adapter;
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
        EXPECT_TRUE(std::is_sorted(edge.board.begin(), edge.board.end()));
        for (const auto card : kFlop) {
            EXPECT_TRUE(std::find(edge.board.begin(), edge.board.end(), card) != edge.board.end());
        }
        EXPECT_TRUE(std::find(edge.board.begin(), edge.board.end(), edge.dealt_card) != edge.board.end());
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

    EXPECT_EQ(transition.betting.street, texas::Street::Turn);
    EXPECT_EQ(transition.betting.current_player, 2);
    EXPECT_EQ(transition.betting.current_bet, 0);
    EXPECT_EQ(transition.board, chance.board);
    EXPECT_EQ(transition.board_runout.remaining_board_cards, 1U);
    EXPECT_TRUE(!transition.board_runout.chance_only_runout);

    const auto next_state = fixture.admit_street_child(turn, transition);
    EXPECT_EQ(next_state.betting.street, texas::Street::Turn);
}

TEST_CASE(multiway_terminal_adapter_delegates_fold_terminals) {
    AdapterFixture fixture(root_for_betting_state(flop_state()));
    const auto deal = fixture.adapter.sample_private_deal(1);
    auto state = fixture.request.root().public_state;
    state = fixture.admit_action_child(state, 1);
    state = fixture.admit_action_child(state, 0);
    state = fixture.admit_action_child(state, 0);
    const auto result = fixture.adapter.resolve_terminal(state.id, deal);
    texas::MultiwayTerminalInput expected_input;
    expected_input.contributions = state.betting.contributions;
    expected_input.folded = state.betting.folded;
    expected_input.strengths.assign(expected_input.contributions.size(), texas::Strength{});
    expected_input.odd_chip_first_seat = 0;
    const auto expected = texas::settle_multiway_terminal(expected_input);
    EXPECT_EQ(result.payouts, expected.payouts);
    EXPECT_EQ(result.refunds, expected.refunds);
    EXPECT_EQ(result.utilities, expected.utilities);
}

TEST_CASE(multiway_terminal_adapter_converts_fold_terminal_utilities_once_at_the_root_boundary) {
    auto chips_root = root_for_betting_state(flop_state());
    auto big_blinds_root = chips_root;
    big_blinds_root.value_units = texas::MultiwayValueUnits::BigBlinds;
    AdapterFixture chips(std::move(chips_root));
    AdapterFixture big_blinds(std::move(big_blinds_root));
    const auto resolve_fold = [](AdapterFixture& fixture) {
        const auto deal = fixture.adapter.sample_private_deal(1U);
        auto state = fixture.request.root().public_state;
        state = fixture.admit_action_child(state, 1U);
        state = fixture.admit_action_child(state, 0U);
        state = fixture.admit_action_child(state, 0U);
        return fixture.adapter.resolve_terminal(state.id, deal);
    };

    const auto chips_result = resolve_fold(chips);
    const auto big_blinds_result = resolve_fold(big_blinds);
    EXPECT_EQ(chips_result.utility_units, texas::MultiwayValueUnits::Chips);
    EXPECT_EQ(big_blinds_result.utility_units, texas::MultiwayValueUnits::BigBlinds);
    EXPECT_EQ(big_blinds_result.payouts, chips_result.payouts);
    EXPECT_EQ(big_blinds_result.refunds, chips_result.refunds);
    EXPECT_EQ(big_blinds_result.rake_taken, chips_result.rake_taken);
    for (std::size_t seat = 0; seat < chips_result.utilities.size(); ++seat) {
        EXPECT_NEAR(big_blinds_result.utilities[seat], chips_result.utilities[seat] / 100.0, 1e-12);
    }
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
