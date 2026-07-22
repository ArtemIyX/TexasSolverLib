#include "games/multiway_state.hpp"
#include "test_harness.hpp"

#include <algorithm>
#include <stdexcept>

namespace {

core::MultiwayState three_handed(int stack = 1000) {
    core::MultiwayGameConfig config;
    config.starting_stacks = {stack, stack, stack};
    config.initial_contributions = {0, 0, 0};
    config.initial_street_contributions = {0, 0, 0};
    config.first_player = 0;
    config.street = core::Street::Flop;
    return core::MultiwayState::initial(config);
}

bool has(const std::vector<core::MultiwayAction>& actions, core::MultiwayAction action) {
    return std::find(actions.begin(), actions.end(), action) != actions.end();
}

}  // namespace

TEST_CASE(multiway_rejects_invalid_seat_configuration) {
    core::MultiwayGameConfig config;
    config.starting_stacks = {1000};
    config.initial_contributions = {0};
    EXPECT_THROW(core::MultiwayState::initial(config), std::invalid_argument);

    config.starting_stacks = {1000, 1000, 1000};
    config.initial_contributions = {0, 0};
    EXPECT_THROW(core::MultiwayState::initial(config), std::invalid_argument);

    config.starting_stacks = {1000, 1000, 1000, 1000, 1000, 1000, 1000};
    config.initial_contributions = {0, 0, 0, 0, 0, 0, 0};
    EXPECT_THROW(core::MultiwayState::initial(config), std::invalid_argument);
}

TEST_CASE(multiway_initializes_variable_seats_and_action_ring) {
    const auto state = three_handed();
    EXPECT_EQ(state.current_player(), 0);
    EXPECT_EQ(state.stacks().size(), std::size_t{3});
    EXPECT_EQ(state.current_bet(), 0);
    EXPECT_TRUE(has(state.legal_actions(), core::MultiwayAction::Check));
    EXPECT_TRUE(has(state.legal_actions(), core::MultiwayAction::Bet));
    EXPECT_TRUE(has(state.legal_actions(), core::MultiwayAction::AllIn));
}

TEST_CASE(multiway_initial_contributions_reduce_remaining_stacks) {
    core::MultiwayGameConfig config;
    config.starting_stacks = {1000, 800, 600};
    config.initial_contributions = {150, 100, 50};
    config.initial_street_contributions = {0, 0, 0};
    const auto state = core::MultiwayState::initial(config);
    EXPECT_EQ(state.stacks()[0], 850);
    EXPECT_EQ(state.stacks()[1], 700);
    EXPECT_EQ(state.stacks()[2], 550);
    EXPECT_EQ(state.contributions()[0], 150);
}

TEST_CASE(multiway_six_seat_ring_wraps_without_two_player_assumptions) {
    core::MultiwayGameConfig config;
    config.starting_stacks = {1000, 1000, 1000, 1000, 1000, 1000};
    config.initial_contributions = {0, 0, 0, 0, 0, 0};
    config.initial_street_contributions = {0, 0, 0, 0, 0, 0};
    config.first_player = 4;
    auto state = core::MultiwayState::initial(config);
    EXPECT_EQ(state.current_player(), 4);
    state = state.apply(core::MultiwayAction::Check);
    EXPECT_EQ(state.current_player(), 5);
    state = state.apply(core::MultiwayAction::Check);
    EXPECT_EQ(state.current_player(), 0);
}

TEST_CASE(multiway_checks_walk_the_ring_and_complete_the_street) {
    const auto final = three_handed().apply(core::MultiwayAction::Check)
                                     .apply(core::MultiwayAction::Check)
                                     .apply(core::MultiwayAction::Check);
    EXPECT_TRUE(final.is_betting_round_complete());
    EXPECT_TRUE(final.requires_street_transition());
    EXPECT_TRUE(!final.is_hand_over());
    EXPECT_EQ(final.current_player(), -1);
}

TEST_CASE(multiway_full_bet_reopens_every_other_live_seat) {
    const auto bet = three_handed().apply(core::MultiwayAction::Bet, 200);
    EXPECT_EQ(bet.current_bet(), 200);
    EXPECT_EQ(bet.last_full_raise_size(), 200);
    EXPECT_EQ(bet.current_player(), 1);
    EXPECT_TRUE(bet.may_raise()[1]);
    EXPECT_TRUE(bet.may_raise()[2]);
    EXPECT_TRUE(has(bet.legal_actions(), core::MultiwayAction::Fold));
    EXPECT_TRUE(has(bet.legal_actions(), core::MultiwayAction::Call));
    EXPECT_TRUE(has(bet.legal_actions(), core::MultiwayAction::Raise));
}

TEST_CASE(multiway_full_raise_returns_action_to_prior_callers) {
    const auto state = three_handed().apply(core::MultiwayAction::Bet, 100)
                                     .apply(core::MultiwayAction::Call)
                                     .apply(core::MultiwayAction::Raise, 300);
    EXPECT_EQ(state.current_bet(), 300);
    EXPECT_EQ(state.last_full_raise_size(), 200);
    EXPECT_EQ(state.current_player(), 0);
    EXPECT_TRUE(state.may_raise()[0]);
    EXPECT_TRUE(state.may_raise()[1]);
}

TEST_CASE(multiway_short_all_in_does_not_reopen_prior_caller) {
    core::MultiwayGameConfig config;
    config.starting_stacks = {1000, 1000, 150};
    config.initial_contributions = {0, 0, 0};
    config.initial_street_contributions = {0, 0, 0};
    config.first_player = 0;
    auto state = core::MultiwayState::initial(config)
                     .apply(core::MultiwayAction::Bet, 100)
                     .apply(core::MultiwayAction::Call)
                     .apply(core::MultiwayAction::AllIn);
    EXPECT_EQ(state.current_bet(), 150);
    EXPECT_EQ(state.current_player(), 0);
    EXPECT_TRUE(!state.may_raise()[0]);
    EXPECT_TRUE(!has(state.legal_actions(), core::MultiwayAction::Raise));
    EXPECT_TRUE(has(state.legal_actions(), core::MultiwayAction::Call));
}

TEST_CASE(multiway_short_all_in_leaves_unacted_seat_eligible_to_raise) {
    core::MultiwayGameConfig config;
    config.starting_stacks = {1000, 150, 1000};
    config.initial_contributions = {0, 0, 0};
    config.initial_street_contributions = {0, 0, 0};
    config.first_player = 0;
    const auto state = core::MultiwayState::initial(config)
                           .apply(core::MultiwayAction::Bet, 100)
                           .apply(core::MultiwayAction::AllIn);
    EXPECT_EQ(state.current_player(), 2);
    EXPECT_TRUE(state.may_raise()[2]);
    EXPECT_TRUE(has(state.legal_actions(), core::MultiwayAction::Raise));
}

TEST_CASE(multiway_cumulative_short_all_ins_reopen_a_prior_caller_at_the_full_raise_threshold) {
    core::MultiwayGameConfig config;
    config.starting_stacks = {1000, 1000, 150, 200};
    config.initial_contributions = {0, 0, 0, 0};
    config.initial_street_contributions = {0, 0, 0, 0};
    config.first_player = 0;
    const auto state = core::MultiwayState::initial(config)
                           .apply(core::MultiwayAction::Bet, 100)
                           .apply(core::MultiwayAction::Call)
                           .apply(core::MultiwayAction::AllIn)
                           .apply(core::MultiwayAction::AllIn);
    EXPECT_EQ(state.current_player(), 0);
    EXPECT_EQ(state.current_bet(), 200);
    EXPECT_TRUE(state.may_raise()[0]);
    EXPECT_TRUE(has(state.legal_actions(), core::MultiwayAction::Raise));
}

TEST_CASE(multiway_snapshot_round_trips_an_arbitrary_live_subgame_root) {
    const auto live = three_handed()
                          .apply(core::MultiwayAction::Bet, 100)
                          .apply(core::MultiwayAction::Call);
    const auto restored = core::MultiwayState::from_snapshot(live.snapshot());
    EXPECT_EQ(restored.current_player(), live.current_player());
    EXPECT_EQ(restored.current_bet(), live.current_bet());
    EXPECT_EQ(restored.last_full_raise_size(), live.last_full_raise_size());
    EXPECT_EQ(restored.stacks(), live.stacks());
    EXPECT_EQ(restored.may_raise(), live.may_raise());
    EXPECT_EQ(restored.legal_actions(), live.legal_actions());
}

TEST_CASE(multiway_snapshot_rejects_inconsistent_raise_or_turn_metadata) {
    auto snapshot = three_handed().snapshot();
    snapshot.current_player = 1;
    snapshot.pending[1] = false;
    EXPECT_THROW(core::MultiwayState::from_snapshot(snapshot), std::invalid_argument);
    snapshot = three_handed().snapshot();
    snapshot.current_bet = 100;
    EXPECT_THROW(core::MultiwayState::from_snapshot(snapshot), std::invalid_argument);
    snapshot = three_handed().snapshot();
    snapshot.current_player = -1;
    EXPECT_THROW(core::MultiwayState::from_snapshot(snapshot), std::invalid_argument);
    snapshot = three_handed().snapshot();
    snapshot.may_raise[0] = false;
    EXPECT_THROW(core::MultiwayState::from_snapshot(snapshot), std::invalid_argument);
}

TEST_CASE(multiway_fold_terminates_when_one_player_remains) {
    const auto state = three_handed().apply(core::MultiwayAction::Bet, 100)
                                     .apply(core::MultiwayAction::Fold)
                                     .apply(core::MultiwayAction::Fold);
    EXPECT_TRUE(state.is_hand_over());
    EXPECT_EQ(state.current_player(), -1);
    EXPECT_TRUE(state.folded()[1]);
    EXPECT_TRUE(state.folded()[2]);
}

TEST_CASE(multiway_all_in_players_are_skipped_and_matched_players_complete_round) {
    core::MultiwayGameConfig config;
    config.starting_stacks = {1000, 100, 1000};
    config.initial_contributions = {0, 0, 0};
    config.initial_street_contributions = {0, 0, 0};
    config.first_player = 0;
    const auto state = core::MultiwayState::initial(config)
                           .apply(core::MultiwayAction::Bet, 100)
                           .apply(core::MultiwayAction::Call)
                           .apply(core::MultiwayAction::Call);
    EXPECT_TRUE(state.is_hand_over());
    EXPECT_TRUE(state.requires_board_runout());
    EXPECT_TRUE(!state.is_betting_round_complete());
    EXPECT_EQ(state.current_player(), -1);
    EXPECT_TRUE(state.all_in()[1]);
}

TEST_CASE(multiway_covering_stack_has_no_betting_actions_after_all_in_calls) {
    core::MultiwayGameConfig config;
    config.starting_stacks = {100, 100, 1000};
    config.initial_contributions = {0, 0, 0};
    config.initial_street_contributions = {0, 0, 0};
    config.first_player = 0;
    const auto state = core::MultiwayState::initial(config)
                           .apply(core::MultiwayAction::AllIn)
                           .apply(core::MultiwayAction::Call)
                           .apply(core::MultiwayAction::Call);
    EXPECT_TRUE(state.is_hand_over());
    EXPECT_TRUE(state.requires_board_runout());
    EXPECT_TRUE(state.legal_actions().empty());
    EXPECT_THROW(state.begin_next_street(core::Street::Turn, 0), std::logic_error);
}

TEST_CASE(multiway_all_in_has_one_canonical_transition) {
    core::MultiwayGameConfig config;
    config.starting_stacks = {1000, 150, 1000};
    config.initial_contributions = {0, 0, 0};
    config.initial_street_contributions = {0, 0, 0};
    config.first_player = 0;
    const auto facing_bet = core::MultiwayState::initial(config)
                                .apply(core::MultiwayAction::Bet, 100);
    EXPECT_TRUE(!has(facing_bet.legal_actions(), core::MultiwayAction::Raise));
    EXPECT_TRUE(has(facing_bet.legal_actions(), core::MultiwayAction::AllIn));
    EXPECT_THROW(facing_bet.apply(core::MultiwayAction::Raise, 150), std::invalid_argument);
}

TEST_CASE(multiway_every_remaining_player_all_in_ends_the_hand) {
    core::MultiwayGameConfig config;
    config.starting_stacks = {100, 100, 100};
    config.initial_contributions = {0, 0, 0};
    config.initial_street_contributions = {0, 0, 0};
    config.first_player = 0;
    const auto state = core::MultiwayState::initial(config)
                           .apply(core::MultiwayAction::AllIn)
                           .apply(core::MultiwayAction::Call)
                           .apply(core::MultiwayAction::Call);
    EXPECT_TRUE(state.is_hand_over());
    EXPECT_EQ(state.current_player(), -1);
    EXPECT_TRUE(state.all_in()[0]);
    EXPECT_TRUE(state.all_in()[1]);
    EXPECT_TRUE(state.all_in()[2]);
}

TEST_CASE(multiway_initial_all_in_seat_is_skipped_in_the_ring) {
    core::MultiwayGameConfig config;
    config.starting_stacks = {1000, 100, 1000};
    config.initial_contributions = {0, 100, 0};
    config.initial_street_contributions = {0, 100, 0};
    config.first_player = 1;
    const auto state = core::MultiwayState::initial(config);
    EXPECT_TRUE(state.all_in()[1]);
    EXPECT_EQ(state.current_player(), 2);
}

TEST_CASE(multiway_folded_seat_is_not_reintroduced_after_a_raise) {
    const auto state = three_handed().apply(core::MultiwayAction::Bet, 100)
                                     .apply(core::MultiwayAction::Fold)
                                     .apply(core::MultiwayAction::Raise, 300);
    EXPECT_EQ(state.current_player(), 0);
    EXPECT_TRUE(state.folded()[1]);
    EXPECT_TRUE(!state.may_raise()[1]);
}

TEST_CASE(multiway_rejects_invalid_street_transitions) {
    const auto complete = three_handed().apply(core::MultiwayAction::Check)
                                        .apply(core::MultiwayAction::Check)
                                        .apply(core::MultiwayAction::Check);
    EXPECT_THROW(complete.begin_next_street(core::Street::River, 0), std::invalid_argument);
    EXPECT_THROW(complete.begin_next_street(core::Street::Turn, 3), std::invalid_argument);
}

TEST_CASE(multiway_next_street_resets_betting_and_uses_requested_ring_start) {
    const auto complete = three_handed().apply(core::MultiwayAction::Check)
                                        .apply(core::MultiwayAction::Check)
                                        .apply(core::MultiwayAction::Check);
    const auto turn = complete.begin_next_street(core::Street::Turn, 2);
    EXPECT_EQ(turn.street(), core::Street::Turn);
    EXPECT_EQ(turn.current_player(), 2);
    EXPECT_EQ(turn.current_bet(), 0);
    EXPECT_EQ(turn.last_full_raise_size(), 100);
    EXPECT_EQ(turn.street_contributions()[0], 0);
}

TEST_CASE(multiway_river_completion_is_terminal_without_pot_evaluation) {
    auto state = three_handed().apply(core::MultiwayAction::Check)
                               .apply(core::MultiwayAction::Check)
                               .apply(core::MultiwayAction::Check)
                               .begin_next_street(core::Street::Turn, 0)
                               .apply(core::MultiwayAction::Check)
                               .apply(core::MultiwayAction::Check)
                               .apply(core::MultiwayAction::Check)
                               .begin_next_street(core::Street::River, 0)
                               .apply(core::MultiwayAction::Check)
                               .apply(core::MultiwayAction::Check)
                               .apply(core::MultiwayAction::Check);
    EXPECT_TRUE(state.is_terminal());
    EXPECT_TRUE(state.is_hand_over());
    EXPECT_TRUE(!state.requires_street_transition());
}

TEST_CASE(multiway_rejects_illegal_actions_and_bad_raise_sizes) {
    const auto state = three_handed();
    EXPECT_THROW(state.apply(core::MultiwayAction::Call), std::invalid_argument);
    EXPECT_THROW(state.apply(core::MultiwayAction::Bet, 50), std::invalid_argument);
    const auto bet = state.apply(core::MultiwayAction::Bet, 100);
    EXPECT_THROW(bet.apply(core::MultiwayAction::Raise, 150), std::invalid_argument);
    EXPECT_THROW(bet.begin_next_street(core::Street::Turn, 0), std::logic_error);
}
