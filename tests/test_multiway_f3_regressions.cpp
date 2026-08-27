#include "games/multiway_replay.hpp"
#include "games/multiway_terminal.hpp"
#include "solver/multiway/abstraction/multiway_action_abstraction.hpp"
#include "test_harness.hpp"

#include <numeric>

namespace {
texas::MultiwayState preflop() {
    texas::MultiwayGameConfig config;
    config.starting_stacks = {10000, 10000, 10000, 10000, 10000, 10000};
    config.initial_contributions = {0, 0, 0, 0, 50, 100};
    config.initial_street_contributions = config.initial_contributions;
    config.first_player = 0;
    config.big_blind = 100;
    config.street = texas::Street::Preflop;
    return texas::MultiwayState::initial(config);
}
}

TEST_CASE(multiway_f3_preflop_translation_maps_nearby_off_tree_raise) {
    const auto state = preflop();
    const texas::MultiwayActionAbstraction abstraction;
    const auto menu = abstraction.make_legal_actions(state.snapshot());
    const auto translated = abstraction.translate_observed_action(
        state.snapshot(), menu, texas::MultiwayAction::Bet, 325);
    EXPECT_EQ(translated.status, texas::MultiwayActionTranslationStatus::Translated);
    EXPECT_TRUE(translated.translated_action.action_menu_id != 0U);
}

TEST_CASE(multiway_f3_replay_rejects_noncanonical_transition_board) {
    auto history = texas::MultiwayHandHistory::from_rules(texas::MultiwayGameRules::standard_6max());
    texas::MultiwayReplayEvent event;
    event.kind = texas::MultiwayReplayEventKind::StreetTransition;
    event.next_street = texas::Street::Flop;
    event.first_player = 0;
    event.board = {2U, 1U, 3U};
    history.events.push_back(event);
    EXPECT_THROW(history.validate(), std::invalid_argument);
}

TEST_CASE(multiway_f3_terminal_folded_all_in_side_pot_conservation) {
    texas::MultiwayTerminalInput input;
    input.contributions = {100, 300, 300};
    input.folded = {false, true, false};
    input.strengths = {texas::Strength{5}, texas::Strength{99}, texas::Strength{8}};
    const auto result = texas::settle_multiway_terminal(input);
    EXPECT_EQ(result.payouts[1], 0);
    EXPECT_EQ(result.pots.size(), std::size_t{2});
    const auto paid = std::accumulate(result.payouts.begin(), result.payouts.end(), 0);
    const auto refunded = std::accumulate(result.refunds.begin(), result.refunds.end(), 0);
    EXPECT_EQ(paid + refunded + result.rake_taken, 700);
}

TEST_CASE(multiway_f3_full_hand_fixture_preserves_decision_seed_and_street_board) {
    auto history = texas::MultiwayHandHistory::from_rules(texas::MultiwayGameRules::standard_6max(), 0, 77U);
    EXPECT_EQ(history.hand_seed, 77U);
    EXPECT_EQ(history.initial_config.street, texas::Street::Preflop);
    EXPECT_EQ(history.initial_config.initial_contributions[4], 50);
    EXPECT_EQ(history.initial_config.initial_contributions[5], 100);
}
