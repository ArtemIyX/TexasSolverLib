#include "games/multiway_replay.hpp"
#include "games/multiway_terminal.hpp"
#include "test_harness.hpp"

#include <numeric>
#include <stdexcept>

namespace {

texas::MultiwayGameConfig three_handed_config(int third_stack = 1'000) {
    texas::MultiwayGameConfig config;
    config.starting_stacks = {1'000, 1'000, third_stack};
    config.initial_contributions = {0, 0, 0};
    config.initial_street_contributions = {0, 0, 0};
    config.first_player = 0;
    config.street = texas::Street::Flop;
    return config;
}

texas::MultiwayReplayEvent decision(
    texas::PlayerId seat,
    texas::MultiwayAction action,
    int target,
    std::uint64_t seed) {
    texas::MultiwayReplayEvent event;
    event.kind = texas::MultiwayReplayEventKind::Decision;
    event.decision = {seat, action, target, seed};
    return event;
}

int settled_chips(const texas::MultiwayTerminalResult& result) {
    return result.rake_taken + std::accumulate(result.refunds.begin(), result.refunds.end(), 0) +
        std::accumulate(result.payouts.begin(), result.payouts.end(), 0);
}

}  // namespace

TEST_CASE(multiway_rules_construct_forced_posts_and_preflop_action_order) {
    const auto rules = texas::MultiwayGameRules::standard_6max();
    const auto config = rules.make_initial_game_config(0);
    const auto state = texas::MultiwayState::initial(rules, 0);

    EXPECT_EQ(config.street, texas::Street::Preflop);
    EXPECT_EQ(config.initial_contributions, (std::vector<int>{0, 0, 0, 0, 50, 100}));
    EXPECT_EQ(config.initial_street_contributions, config.initial_contributions);
    EXPECT_EQ(state.current_player(), 0);
    EXPECT_EQ(state.current_bet(), 100);
    EXPECT_EQ(state.stacks()[4], 9'950);
    EXPECT_EQ(state.stacks()[5], 9'900);
}

TEST_CASE(multiway_replay_preserves_short_all_in_and_decision_seeds) {
    texas::MultiwayHandHistory history;
    history.hand_seed = 77;
    history.initial_config = three_handed_config(150);
    history.events = {
        decision(0, texas::MultiwayAction::Bet, 100, 101),
        decision(1, texas::MultiwayAction::Call, 0, 202),
        decision(2, texas::MultiwayAction::AllIn, 0, 303),
    };

    const auto state = texas::replay_multiway_hand(history);
    EXPECT_EQ(history.events[2].decision.decision_seed, 303U);
    EXPECT_EQ(state.current_player(), 0);
    EXPECT_EQ(state.current_bet(), 150);
    EXPECT_TRUE(!state.may_raise()[0]);
}

TEST_CASE(multiway_replay_all_in_runout_and_malformed_actions_are_engine_checked) {
    texas::MultiwayHandHistory history;
    history.initial_config = three_handed_config(100);
    history.events = {
        decision(0, texas::MultiwayAction::AllIn, 0, 1),
        decision(1, texas::MultiwayAction::Call, 0, 2),
        decision(2, texas::MultiwayAction::Call, 0, 3),
    };

    const auto runout = texas::replay_multiway_hand(history);
    EXPECT_TRUE(runout.requires_board_runout());

    history.events[0].decision.acting_seat = 1;
    EXPECT_THROW(texas::replay_multiway_hand(history), std::invalid_argument);
}

TEST_CASE(multiway_terminal_side_pots_rake_odd_chips_and_conservation_are_exact) {
    texas::MultiwayTerminalInput side_pot;
    side_pot.contributions = {50, 100, 100};
    side_pot.folded = {false, false, false};
    side_pot.strengths = {texas::Strength{3}, texas::Strength{1}, texas::Strength{2}};
    side_pot.rake_policy.mode = texas::MultiwayRakeMode::PercentageOfContestedPot;
    side_pot.rake_policy.basis_points = 400U;
    side_pot.rake_policy.cap = 100;
    const auto settled_side_pot = texas::settle_multiway_terminal(side_pot);
    EXPECT_EQ(settled_side_pot.pots.size(), std::size_t{2});
    EXPECT_EQ(settled_side_pot.rake_taken, 10);
    EXPECT_EQ(settled_side_pot.payouts, (std::vector<int>{140, 0, 100}));
    EXPECT_EQ(settled_chips(settled_side_pot), 250);

    texas::MultiwayTerminalInput odd_chips;
    odd_chips.contributions = {101, 101, 101};
    odd_chips.folded = {false, false, false};
    odd_chips.strengths = {texas::Strength{7}, texas::Strength{7}, texas::Strength{1}};
    odd_chips.odd_chip_first_seat = 1;
    const auto settled_odd_chips = texas::settle_multiway_terminal(odd_chips);
    EXPECT_EQ(settled_odd_chips.payouts, (std::vector<int>{151, 152, 0}));
    EXPECT_EQ(settled_chips(settled_odd_chips), 303);
}
