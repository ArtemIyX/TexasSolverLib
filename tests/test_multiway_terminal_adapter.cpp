#include "solver/multiway_terminal_adapter.hpp"
#include "test_harness.hpp"

#include <array>
#include <cstdint>
#include <numeric>
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

core::MultiwayState flop_state_with_stacks(std::vector<int> stacks) {
    core::MultiwayGameConfig game;
    game.starting_stacks = std::move(stacks);
    game.initial_contributions.assign(game.starting_stacks.size(), 0);
    game.initial_street_contributions.assign(game.starting_stacks.size(), 0);
    game.first_player = 0;
    game.street = core::Street::Flop;
    return core::MultiwayState::initial(game);
}

core::MultiwayState preflop_state() {
    core::MultiwayGameConfig game;
    game.starting_stacks = {1000, 1000, 1000};
    game.initial_contributions = {0, 0, 0};
    game.initial_street_contributions = {0, 0, 0};
    game.first_player = 0;
    game.street = core::Street::Preflop;
    return core::MultiwayState::initial(game);
}

core::MultiwayState two_handed_flop_state() {
    core::MultiwayGameConfig game;
    game.starting_stacks = {1000, 1000};
    game.initial_contributions = {0, 0};
    game.initial_street_contributions = {0, 0};
    game.first_player = 0;
    game.street = core::Street::Flop;
    return core::MultiwayState::initial(game);
}

core::MultiwayRootSnapshot root_with_first_seat(core::PlayerId first_seat = 0, core::PlayerId odd_chip_first_seat = 0) {
    const auto state = flop_state();
    core::MultiwayRootSnapshot root;
    root.public_state.id = {42};
    root.public_state.canonical_history_id = 4242;
    root.public_state.betting = state.snapshot();
    root.public_state.board = kFlop;
    root.public_state.legal_actions = {
        {core::MultiwayAction::Check, 0, 0, 8800},
        {core::MultiwayAction::Bet, 1, 100, 8800},
        {core::MultiwayAction::AllIn, 2, 0, 8800},
    };
    root.root_infoset = {{42}, 0};
    root.seat_order = {first_seat, static_cast<core::PlayerId>((first_seat + 1) % 3),
                       static_cast<core::PlayerId>((first_seat + 2) % 3)};
    root.next_street_first_seat = first_seat;
    root.odd_chip_first_seat = odd_chip_first_seat;
    root.private_ranges.board = kFlop;
    const auto deal = private_deal();
    for (const auto& hole : deal.holes) root.private_ranges.ranges.push_back({{hole, 1.0}});
    root.action_abstraction_version = 1;
    root.leaf_model_version = 1;
    return root;
}

core::MultiwayBettingSnapshot complete_flop_betting() {
    return flop_state().apply(core::MultiwayAction::Check)
        .apply(core::MultiwayAction::Check)
        .apply(core::MultiwayAction::Check)
        .snapshot();
}

int sum(const std::vector<int>& values) {
    return std::accumulate(values.begin(), values.end(), 0);
}

}  // namespace

TEST_CASE(multiway_terminal_adapter_enumerates_canonical_exclusive_board_chance_edges) {
    const core::MultiwayTerminalAdapter adapter(root_with_first_seat());
    const auto deal = private_deal();
    const auto edges = adapter.canonical_board_chance_edges(complete_flop_betting(), kFlop, deal);

    EXPECT_EQ(edges.size(), std::size_t{43});
    double total_probability = 0.0;
    std::array<bool, 64> excluded = {};
    for (const auto card : kFlop) excluded[card] = true;
    for (const auto& hole : deal.holes) {
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

TEST_CASE(multiway_terminal_adapter_uses_root_owned_first_player_for_street_transition) {
    const core::MultiwayTerminalAdapter adapter(root_with_first_seat(2));
    const std::vector<std::uint8_t> turn = {kFlop[0], kFlop[1], kFlop[2], c(3, 3)};
    const auto transition = adapter.apply_street_transition(complete_flop_betting(), turn);

    EXPECT_EQ(transition.betting.street, core::Street::Turn);
    EXPECT_EQ(transition.betting.current_player, 2);
    EXPECT_EQ(transition.betting.current_bet, 0);
    EXPECT_EQ(transition.board, turn);
    EXPECT_EQ(transition.board_runout.remaining_board_cards, 1U);
    EXPECT_TRUE(!transition.board_runout.chance_only_runout);
}

TEST_CASE(multiway_terminal_adapter_runs_out_all_in_board_before_showdown) {
    const core::MultiwayTerminalAdapter adapter(root_with_first_seat());
    const auto deal = private_deal();
    const auto all_in = flop_state().apply(core::MultiwayAction::AllIn)
        .apply(core::MultiwayAction::Call)
        .apply(core::MultiwayAction::Call)
        .snapshot();

    const auto turn_edges = adapter.canonical_board_chance_edges(all_in, kFlop, deal);
    const auto& turn = turn_edges.front();
    EXPECT_TRUE(turn.board_runout.chance_only_runout);
    EXPECT_THROW(adapter.resolve_terminal(all_in, turn.board, deal), std::logic_error);

    const auto river_edges = adapter.canonical_board_chance_edges(all_in, turn.board, deal);
    const auto result = adapter.resolve_terminal(all_in, river_edges.front().board, deal);
    EXPECT_EQ(sum(result.payouts) + sum(result.refunds), 3000);
    EXPECT_NEAR(std::accumulate(result.utilities.begin(), result.utilities.end(), 0.0), 0.0, 1e-12);
}

TEST_CASE(multiway_terminal_adapter_delegates_fold_terminals) {
    const core::MultiwayTerminalAdapter adapter(root_with_first_seat());
    const auto deal = private_deal();
    const auto folded = flop_state().apply(core::MultiwayAction::Bet, 100)
        .apply(core::MultiwayAction::Fold)
        .apply(core::MultiwayAction::Fold)
        .snapshot();

    const auto result = adapter.resolve_terminal(folded, kFlop, deal);
    core::MultiwayTerminalInput expected_input;
    expected_input.contributions = folded.contributions;
    expected_input.folded = folded.folded;
    expected_input.strengths.assign(folded.contributions.size(), core::Strength{});
    expected_input.odd_chip_first_seat = 0;
    const auto expected = core::settle_multiway_terminal(expected_input);
    EXPECT_EQ(result.payouts, expected.payouts);
    EXPECT_EQ(result.refunds, expected.refunds);
    EXPECT_EQ(result.utilities, expected.utilities);
}

TEST_CASE(multiway_terminal_adapter_delegates_side_pot_showdown_and_odd_chip_order) {
    auto root = root_with_first_seat(0, 1);
    root.public_state.betting = flop_state_with_stacks({101, 301, 301}).snapshot();
    const core::MultiwayTerminalAdapter adapter(root);
    core::MultiwayGameConfig game;
    game.starting_stacks = {101, 301, 301};
    game.initial_contributions = {101, 301, 301};
    game.initial_street_contributions = {0, 0, 0};
    game.street = core::Street::River;
    const auto betting = core::MultiwayState::initial(game).snapshot();
    const std::vector<std::uint8_t> board = {kFlop[0], kFlop[1], kFlop[2], c(10, 3), c(11, 0)};
    core::MultiwayJointPrivateSample deal;
    deal.holes = {{c(12, 0), c(13, 0)}, {c(12, 1), c(13, 1)}, {c(14, 3), c(3, 3)}};

    const auto result = adapter.resolve_terminal(betting, board, deal);
    EXPECT_EQ(result.pots.size(), std::size_t{2});
    EXPECT_EQ(result.payouts[0], 151);
    EXPECT_EQ(result.payouts[1], 552);
    EXPECT_EQ(result.payouts[2], 0);
    EXPECT_EQ(sum(result.payouts) + sum(result.refunds), 703);
}

TEST_CASE(multiway_terminal_adapter_rejects_invalid_state_board_and_private_deal) {
    const core::MultiwayTerminalAdapter adapter(root_with_first_seat());
    const auto deal = private_deal();
    const auto complete = complete_flop_betting();

    EXPECT_THROW(adapter.canonical_board_chance_edges(flop_state().snapshot(), kFlop, deal), std::logic_error);
    EXPECT_THROW(adapter.apply_street_transition(complete, kFlop), std::invalid_argument);

    auto duplicate_board = kFlop;
    duplicate_board[2] = duplicate_board[1];
    EXPECT_THROW(adapter.canonical_board_chance_edges(complete, duplicate_board, deal), std::invalid_argument);

    auto overlapping_deal = deal;
    overlapping_deal.holes[1][0] = overlapping_deal.holes[0][0];
    EXPECT_THROW(adapter.canonical_board_chance_edges(complete, kFlop, overlapping_deal), std::invalid_argument);
}

TEST_CASE(multiway_terminal_adapter_rejects_snapshots_outside_root_lineage) {
    const core::MultiwayTerminalAdapter adapter(root_with_first_seat());
    const auto deal = private_deal();
    const auto complete = complete_flop_betting();

    EXPECT_THROW(
        adapter.canonical_board_chance_edges(two_handed_flop_state().snapshot(), kFlop, deal),
        std::invalid_argument);

    auto changed_total = complete;
    --changed_total.stacks[1];
    EXPECT_THROW(adapter.canonical_board_chance_edges(changed_total, kFlop, deal), std::invalid_argument);

    EXPECT_THROW(
        adapter.canonical_board_chance_edges(preflop_state().snapshot(), kFlop, deal),
        std::invalid_argument);

    auto incompatible_street = complete;
    incompatible_street.street = core::Street::Turn;
    EXPECT_THROW(adapter.canonical_board_chance_edges(incompatible_street, kFlop, deal), std::invalid_argument);

    const std::vector<std::uint8_t> changed_prefix = {c(3, 0), kFlop[1], kFlop[2]};
    EXPECT_THROW(adapter.canonical_board_chance_edges(complete, changed_prefix, deal), std::invalid_argument);
}
