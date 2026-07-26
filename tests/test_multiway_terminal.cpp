#include "games/multiway_terminal.hpp"
#include "test_harness.hpp"

#include <numeric>
#include <stdexcept>

namespace {

core::Strength hand(std::uint64_t value) {
    return core::Strength{value};
}

core::MultiwayTerminalInput input(
    std::vector<int> contributions,
    std::vector<bool> folded,
    std::vector<std::uint64_t> strengths) {
    core::MultiwayTerminalInput value;
    value.contributions = std::move(contributions);
    value.folded = std::move(folded);
    for (const auto strength : strengths) value.strengths.push_back(hand(strength));
    return value;
}

int sum(const std::vector<int>& values) {
    return std::accumulate(values.begin(), values.end(), 0);
}

}  // namespace

TEST_CASE(multiway_terminal_rejects_mismatched_or_invalid_inputs) {
    EXPECT_THROW(core::settle_multiway_terminal(input({100, 100}, {false}, {1, 2})), std::invalid_argument);
    EXPECT_THROW(core::settle_multiway_terminal(input({100, -1}, {false, false}, {1, 2})), std::invalid_argument);
    EXPECT_THROW(core::settle_multiway_terminal(input({100, 100}, {true, true}, {1, 2})), std::invalid_argument);
}

TEST_CASE(multiway_terminal_single_uncontested_pot_awards_all_contributions) {
    const auto result = core::settle_multiway_terminal(input({100, 100, 100}, {false, true, true}, {1, 99, 99}));
    EXPECT_EQ(result.pots.size(), std::size_t{1});
    EXPECT_EQ(result.pots[0].amount, 300);
    EXPECT_EQ(result.payouts[0], 300);
    EXPECT_EQ(result.utilities[0], 200.0);
    EXPECT_EQ(result.utilities[1], -100.0);
    EXPECT_EQ(result.utilities[2], -100.0);
}

TEST_CASE(multiway_terminal_equal_contributions_make_one_main_pot) {
    const auto result = core::settle_multiway_terminal(input({200, 200, 200}, {false, false, false}, {3, 8, 5}));
    EXPECT_EQ(result.pots.size(), std::size_t{1});
    EXPECT_EQ(result.pots[0].amount, 600);
    EXPECT_EQ(result.pots[0].contribution_cap, 200);
    EXPECT_EQ(result.payouts[1], 600);
    EXPECT_EQ(sum(result.payouts), 600);
}

TEST_CASE(multiway_terminal_builds_main_and_side_pot_for_one_short_stack) {
    const auto result = core::settle_multiway_terminal(input({100, 300, 300}, {false, false, false}, {9, 5, 7}));
    EXPECT_EQ(result.pots.size(), std::size_t{2});
    EXPECT_EQ(result.pots[0].amount, 300);
    EXPECT_EQ(result.pots[1].amount, 400);
    EXPECT_EQ(result.pots[0].eligible_players.size(), std::size_t{3});
    EXPECT_EQ(result.pots[1].eligible_players.size(), std::size_t{2});
    EXPECT_EQ(result.payouts[0], 300);
    EXPECT_EQ(result.payouts[2], 400);
}

TEST_CASE(multiway_terminal_builds_three_ordered_pots_for_staggered_all_ins) {
    const auto result = core::settle_multiway_terminal(input({100, 200, 400, 400}, {false, false, false, false}, {9, 8, 7, 6}));
    EXPECT_EQ(result.pots.size(), std::size_t{3});
    EXPECT_EQ(result.pots[0].amount, 400);
    EXPECT_EQ(result.pots[1].amount, 300);
    EXPECT_EQ(result.pots[2].amount, 400);
    EXPECT_EQ(result.pots[0].eligible_players.size(), std::size_t{4});
    EXPECT_EQ(result.pots[1].eligible_players.size(), std::size_t{3});
    EXPECT_EQ(result.pots[2].eligible_players.size(), std::size_t{2});
}

TEST_CASE(multiway_terminal_folded_player_contributes_but_cannot_win) {
    const auto result = core::settle_multiway_terminal(input({200, 200, 200}, {false, true, false}, {5, 99, 8}));
    EXPECT_EQ(result.payouts[1], 0);
    EXPECT_EQ(result.payouts[2], 600);
    EXPECT_EQ(result.pots[0].eligible_players.size(), std::size_t{2});
}

TEST_CASE(multiway_terminal_refunds_uncalled_excess_bet) {
    const auto result = core::settle_multiway_terminal(input({300, 100, 100}, {false, true, true}, {1, 2, 3}));
    EXPECT_EQ(result.pots.size(), std::size_t{1});
    EXPECT_EQ(result.pots[0].amount, 300);
    EXPECT_EQ(result.refunds[0], 200);
    EXPECT_EQ(result.payouts[0], 300);
    EXPECT_EQ(result.utilities[0], 200.0);
}

TEST_CASE(multiway_terminal_refunds_excess_after_short_all_in) {
    const auto result = core::settle_multiway_terminal(input({500, 200, 200}, {false, false, false}, {5, 8, 7}));
    EXPECT_EQ(result.pots.size(), std::size_t{1});
    EXPECT_EQ(result.pots[0].amount, 600);
    EXPECT_EQ(result.refunds[0], 300);
    EXPECT_EQ(result.payouts[1], 600);
}

TEST_CASE(multiway_terminal_tied_main_pot_splits_evenly) {
    const auto result = core::settle_multiway_terminal(input({150, 150, 150, 150}, {false, false, false, false}, {8, 8, 3, 1}));
    EXPECT_EQ(result.payouts[0], 300);
    EXPECT_EQ(result.payouts[1], 300);
    EXPECT_EQ(sum(result.payouts), 600);
}

TEST_CASE(multiway_terminal_tied_odd_chip_uses_explicit_positional_order) {
    auto terminal_input = input({101, 101, 101}, {false, false, false}, {7, 7, 1});
    terminal_input.odd_chip_first_seat = 1;
    const auto result = core::settle_multiway_terminal(terminal_input);
    EXPECT_EQ(result.payouts[0], 151);
    EXPECT_EQ(result.payouts[1], 152);
    EXPECT_EQ(result.payouts[2], 0);
}

TEST_CASE(multiway_terminal_different_winners_can_take_different_side_pots) {
    const auto result = core::settle_multiway_terminal(input({100, 300, 300}, {false, false, false}, {10, 5, 8}));
    EXPECT_EQ(result.payouts[0], 300);
    EXPECT_EQ(result.payouts[2], 400);
    EXPECT_EQ(result.utilities[0], 200.0);
    EXPECT_EQ(result.utilities[1], -300.0);
    EXPECT_EQ(result.utilities[2], 100.0);
}

TEST_CASE(multiway_terminal_tie_can_be_limited_to_the_main_pot) {
    const auto result = core::settle_multiway_terminal(input({100, 300, 300}, {false, false, false}, {9, 9, 7}));
    EXPECT_EQ(result.payouts[0], 150);
    EXPECT_EQ(result.payouts[1], 550);
    EXPECT_EQ(result.payouts[2], 0);
}

TEST_CASE(multiway_terminal_refunds_are_not_side_pots) {
    const auto result = core::settle_multiway_terminal(input({400, 100, 0}, {false, false, true}, {8, 7, 1}));
    EXPECT_EQ(result.pots.size(), std::size_t{1});
    EXPECT_EQ(result.pots[0].amount, 200);
    EXPECT_EQ(result.refunds[0], 300);
    EXPECT_EQ(sum(result.payouts) + sum(result.refunds), 500);
}

TEST_CASE(multiway_terminal_utilities_are_zero_sum_with_refunds) {
    const auto result = core::settle_multiway_terminal(input({100, 250, 400, 400}, {false, false, false, false}, {4, 10, 8, 6}));
    const auto utility_sum = std::accumulate(result.utilities.begin(), result.utilities.end(), 0.0);
    EXPECT_NEAR(utility_sum, 0.0, 1e-9);
    EXPECT_EQ(sum(result.payouts) + sum(result.refunds), 1150);
}

TEST_CASE(multiway_terminal_rake_is_once_capped_and_conserved) {
    auto terminal_input = input({100, 300, 300}, {false, false, false}, {10, 5, 8});
    terminal_input.rake_policy.mode = core::MultiwayRakeMode::PercentageOfContestedPot;
    terminal_input.rake_policy.basis_points = 500U;
    terminal_input.rake_policy.cap = 20;

    const auto result = core::settle_multiway_terminal(terminal_input);
    EXPECT_EQ(result.rake_taken, 20);
    EXPECT_EQ(result.pots[0].amount, 280);
    EXPECT_EQ(result.pots[1].amount, 400);
    EXPECT_EQ(result.payouts[0], 280);
    EXPECT_EQ(result.payouts[2], 400);
    EXPECT_EQ(sum(result.payouts) + sum(result.refunds) + result.rake_taken, 700);
    const auto utility_sum = std::accumulate(result.utilities.begin(), result.utilities.end(), 0.0);
    EXPECT_NEAR(utility_sum, -20.0, 1e-9);
}

TEST_CASE(multiway_terminal_rake_honors_no_flop_no_drop_and_explicit_zero) {
    auto terminal_input = input({100, 100, 100}, {false, false, false}, {9, 8, 7});
    terminal_input.rake_policy.mode = core::MultiwayRakeMode::PercentageOfContestedPot;
    terminal_input.rake_policy.basis_points = 500U;
    terminal_input.rake_policy.cap = 100;
    terminal_input.flop_seen = false;
    const auto no_drop = core::settle_multiway_terminal(terminal_input);
    EXPECT_EQ(no_drop.rake_taken, 0);
    EXPECT_EQ(sum(no_drop.payouts), 300);

    terminal_input.rake_policy = core::MultiwayRakePolicy::explicit_zero();
    terminal_input.flop_seen = true;
    const auto explicit_zero = core::settle_multiway_terminal(terminal_input);
    EXPECT_EQ(explicit_zero.rake_taken, 0);
    EXPECT_EQ(sum(explicit_zero.payouts), 300);
}

TEST_CASE(multiway_terminal_rejects_ambiguous_zero_rake_policy) {
    auto terminal_input = input({100, 100}, {false, false}, {1, 2});
    terminal_input.rake_policy.basis_points = 1U;
    EXPECT_THROW(core::settle_multiway_terminal(terminal_input), std::invalid_argument);
}

TEST_CASE(multiway_terminal_handles_zero_contribution_folded_seat) {
    const auto result = core::settle_multiway_terminal(input({100, 100, 0}, {false, false, true}, {4, 8, 99}));
    EXPECT_EQ(result.pots.size(), std::size_t{1});
    EXPECT_EQ(result.payouts[1], 200);
    EXPECT_EQ(result.utilities[2], 0.0);
}

TEST_CASE(multiway_terminal_can_settle_a_precomputed_pot_layout) {
    const auto terminal_input = input({100, 300, 300}, {false, false, false}, {10, 5, 8});
    const auto layout = core::build_multiway_pot_layout(terminal_input.contributions, terminal_input.folded);
    const auto result = core::settle_multiway_terminal(terminal_input, layout);
    EXPECT_EQ(result.pots.size(), std::size_t{2});
    EXPECT_EQ(result.payouts[0], 300);
    EXPECT_EQ(result.payouts[2], 400);
}

TEST_CASE(multiway_terminal_rejects_nonconserving_precomputed_layout) {
    const auto terminal_input = input({100, 100}, {false, false}, {1, 2});
    core::MultiwayPotLayout layout;
    layout.refunds = {0, 0};
    layout.pots.push_back({100, 100, {0, 1}});
    EXPECT_THROW(core::settle_multiway_terminal(terminal_input, layout), std::invalid_argument);
}

TEST_CASE(multiway_terminal_rejects_conserving_but_semantically_wrong_precomputed_layout) {
    const auto terminal_input = input({100, 100, 100}, {false, false, false}, {9, 1, 1});
    core::MultiwayPotLayout layout;
    layout.refunds = {0, 0, 0};
    layout.pots.push_back({300, 100, {1, 2}});
    EXPECT_THROW(core::settle_multiway_terminal(terminal_input, layout), std::invalid_argument);
}

TEST_CASE(multiway_terminal_rejects_invalid_odd_chip_order) {
    auto terminal_input = input({100, 100}, {false, false}, {1, 2});
    terminal_input.odd_chip_first_seat = 2;
    EXPECT_THROW(core::settle_multiway_terminal(terminal_input), std::invalid_argument);
}
