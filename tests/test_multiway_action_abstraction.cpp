#include "solver/multiway_action_abstraction.hpp"
#include "test_harness.hpp"

TEST_CASE(multiway_action_abstraction_expands_multiway_bet_sizes) {
    core::MultiwayGameConfig config;
    config.starting_stacks = {2000, 2000, 2000};
    config.initial_contributions = {100, 100, 100};
    config.initial_street_contributions = {0, 0, 0};
    config.first_player = 0;
    config.big_blind = 100;
    config.street = core::Street::Flop;
    const auto actions = core::MultiwayActionAbstraction().make_legal_actions(
        core::MultiwayState::initial(config).snapshot(), 21);

    std::size_t bets = 0;
    for (const auto& action : actions) {
        if (action.action == core::MultiwayAction::Bet) ++bets;
    }
    EXPECT_EQ(bets, 3U);
    EXPECT_EQ(actions.back().action, core::MultiwayAction::AllIn);
}
