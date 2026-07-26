#include "games/multiway_fixed.hpp"
#include "test_harness.hpp"

#include <vector>

namespace {

core::MultiwayState vector_state() {
    core::MultiwayGameConfig config;
    config.starting_stacks = {1000, 1000, 1000};
    config.initial_contributions = {0, 0, 0};
    config.initial_street_contributions = {0, 0, 0};
    config.first_player = 0;
    config.street = core::Street::Flop;
    return core::MultiwayState::initial(config);
}

void expect_state_parity(const core::MultiwayState& oracle, const core::MultiwayFixedState& fixed) {
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
    auto fixed = core::make_multiway_fixed_state(oracle.snapshot());
    expect_state_parity(oracle, fixed);

    const auto oracle_actions = oracle.legal_actions();
    const auto fixed_actions = fixed.legal_actions();
    EXPECT_EQ(fixed_actions.count, oracle_actions.size());
    for (std::size_t action = 0; action < fixed_actions.count; ++action) {
        EXPECT_EQ(fixed_actions.actions[action], oracle_actions[action]);
    }

    oracle = oracle.apply(core::MultiwayAction::Bet, 200);
    fixed = fixed.apply(core::MultiwayAction::Bet, 200);
    expect_state_parity(oracle, fixed);
}

TEST_CASE(multiway_fixed_terminal_matches_vector_oracle_with_side_pots_and_rake) {
    core::MultiwayTerminalInput oracle_input;
    oracle_input.contributions = {100, 300, 300};
    oracle_input.folded = {false, false, false};
    oracle_input.strengths = {core::Strength{10}, core::Strength{5}, core::Strength{8}};
    oracle_input.rake_policy.mode = core::MultiwayRakeMode::PercentageOfContestedPot;
    oracle_input.rake_policy.basis_points = 500U;
    oracle_input.rake_policy.cap = 20;
    const auto oracle = core::settle_multiway_terminal(oracle_input);

    core::MultiwayFixedTerminalInput fixed_input;
    fixed_input.seat_count = 3U;
    fixed_input.rake_policy = oracle_input.rake_policy;
    for (std::size_t seat = 0; seat < fixed_input.seat_count; ++seat) {
        fixed_input.contributions[seat] = oracle_input.contributions[seat];
        fixed_input.folded[seat] = oracle_input.folded[seat];
        fixed_input.strengths[seat] = oracle_input.strengths[seat];
    }
    core::MultiwayFixedTerminalScratch scratch;
    core::MultiwayFixedTerminalResult fixed;
    core::settle_multiway_terminal_fixed(fixed_input, scratch, fixed);

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
