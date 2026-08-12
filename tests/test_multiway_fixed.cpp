#include "games/multiway_fixed.hpp"
#include "test_harness.hpp"

#include <cstdint>
#include <vector>

namespace {

texas::MultiwayState vector_state() {
    texas::MultiwayGameConfig config;
    config.starting_stacks = {1000, 1000, 1000};
    config.initial_contributions = {0, 0, 0};
    config.initial_street_contributions = {0, 0, 0};
    config.first_player = 0;
    config.street = texas::Street::Flop;
    return texas::MultiwayState::initial(config);
}

void expect_state_parity(const texas::MultiwayState& oracle, const texas::MultiwayFixedState& fixed) {
    const auto snapshot = oracle.snapshot();
    EXPECT_EQ(fixed.seat_count, snapshot.stacks.size());
    EXPECT_EQ(fixed.current_player, snapshot.current_player);
    EXPECT_EQ(fixed.current_bet, snapshot.current_bet);
    EXPECT_EQ(fixed.last_full_raise_size, snapshot.last_full_raise_size);
    for (std::size_t seat = 0; seat < fixed.seat_count; ++seat) {
        EXPECT_EQ(fixed.stacks[seat], snapshot.stacks[seat]);
        EXPECT_EQ(fixed.contributions[seat], snapshot.contributions[seat]);
        EXPECT_EQ(fixed.street_contributions[seat], snapshot.street_contributions[seat]);
        EXPECT_EQ(fixed.folded[seat], snapshot.folded[seat]);
        EXPECT_EQ(fixed.pending[seat], snapshot.pending[seat]);
    }
}

}  // namespace

TEST_CASE(multiway_fixed_state_action_menu_and_apply_match_vector_oracle) {
    auto oracle = vector_state();
    auto fixed = texas::make_multiway_fixed_state(oracle.snapshot());
    expect_state_parity(oracle, fixed);

    const auto oracle_actions = oracle.legal_actions();
    const auto fixed_actions = fixed.legal_actions();
    EXPECT_EQ(fixed_actions.count, oracle_actions.size());
    for (std::size_t action = 0; action < fixed_actions.count; ++action) {
        EXPECT_EQ(fixed_actions.actions[action], oracle_actions[action]);
    }

    oracle = oracle.apply(texas::MultiwayAction::Bet, 200);
    fixed = fixed.apply(texas::MultiwayAction::Bet, 200);
    expect_state_parity(oracle, fixed);
}

TEST_CASE(multiway_fixed_terminal_matches_vector_oracle_with_side_pots_and_rake) {
    texas::MultiwayTerminalInput oracle_input;
    oracle_input.contributions = {100, 300, 300};
    oracle_input.folded = {false, false, false};
    oracle_input.strengths = {texas::Strength{10}, texas::Strength{5}, texas::Strength{8}};
    oracle_input.rake_policy.mode = texas::MultiwayRakeMode::PercentageOfContestedPot;
    oracle_input.rake_policy.basis_points = 500U;
    oracle_input.rake_policy.cap = 20;
    const auto oracle = texas::settle_multiway_terminal(oracle_input);

    texas::MultiwayFixedTerminalInput fixed_input;
    fixed_input.seat_count = 3U;
    fixed_input.rake_policy = oracle_input.rake_policy;
    for (std::size_t seat = 0; seat < fixed_input.seat_count; ++seat) {
        fixed_input.contributions[seat] = oracle_input.contributions[seat];
        fixed_input.folded[seat] = oracle_input.folded[seat];
        fixed_input.strengths[seat] = oracle_input.strengths[seat];
    }
    texas::MultiwayFixedTerminalScratch scratch;
    texas::MultiwayFixedTerminalResult fixed;
    texas::settle_multiway_terminal_fixed(fixed_input, scratch, fixed);

    EXPECT_EQ(fixed.pot_count, oracle.pots.size());
    EXPECT_EQ(fixed.rake_taken, oracle.rake_taken);
    for (std::size_t pot = 0; pot < fixed.pot_count; ++pot) {
        EXPECT_EQ(fixed.pots[pot].amount, oracle.pots[pot].amount);
        EXPECT_EQ(fixed.pots[pot].contribution_cap, oracle.pots[pot].contribution_cap);
    }
    for (std::size_t seat = 0; seat < fixed.seat_count; ++seat) {
        EXPECT_EQ(fixed.refunds[seat], oracle.refunds[seat]);
        EXPECT_EQ(fixed.payouts[seat], oracle.payouts[seat]);
        EXPECT_EQ(fixed.utilities[seat], oracle.utilities[seat]);
    }
}

TEST_CASE(multiway_fixed_two_and_three_seat_raked_terminal_toys_match_the_oracle) {
    for (const std::size_t seats : {std::size_t{2}, std::size_t{3}}) {
        texas::MultiwayTerminalInput oracle_input;
        oracle_input.contributions.assign(seats, 100);
        oracle_input.folded.assign(seats, false);
        oracle_input.strengths.reserve(seats);
        for (std::size_t seat = 0; seat < seats; ++seat) {
            oracle_input.strengths.push_back(texas::Strength{static_cast<std::uint64_t>(seats - seat)});
        }
        oracle_input.rake_policy.mode = texas::MultiwayRakeMode::PercentageOfContestedPot;
        oracle_input.rake_policy.basis_points = 500U;
        oracle_input.rake_policy.cap = 100;
        const auto oracle = texas::settle_multiway_terminal(oracle_input);

        texas::MultiwayFixedTerminalInput fixed_input;
        fixed_input.seat_count = static_cast<std::uint8_t>(seats);
        fixed_input.rake_policy = oracle_input.rake_policy;
        for (std::size_t seat = 0; seat < seats; ++seat) {
            fixed_input.contributions[seat] = oracle_input.contributions[seat];
            fixed_input.folded[seat] = oracle_input.folded[seat];
            fixed_input.strengths[seat] = oracle_input.strengths[seat];
        }
        texas::MultiwayFixedTerminalScratch scratch;
        texas::MultiwayFixedTerminalResult fixed;
        texas::settle_multiway_terminal_fixed(fixed_input, scratch, fixed);

        EXPECT_EQ(fixed.rake_taken, oracle.rake_taken);
        int settled_chips = fixed.rake_taken;
        for (std::size_t seat = 0; seat < seats; ++seat) {
            EXPECT_EQ(fixed.payouts[seat], oracle.payouts[seat]);
            EXPECT_EQ(fixed.utilities[seat], oracle.utilities[seat]);
            settled_chips += fixed.payouts[seat] + fixed.refunds[seat];
        }
        EXPECT_EQ(settled_chips, static_cast<int>(seats * 100U));
    }
}
