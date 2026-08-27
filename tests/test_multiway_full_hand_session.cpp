#include "solver/multiway/session/multiway_full_hand_session.hpp"
#include "test_harness.hpp"

#include <stdexcept>

namespace {
texas::MultiwayReplayEvent action(texas::PlayerId seat, texas::MultiwayAction value) {
    texas::MultiwayReplayEvent event;
    event.kind = texas::MultiwayReplayEventKind::Decision;
    event.decision = {seat, value, 100, 1U};
    return event;
}

texas::MultiwayReplayEvent transition(texas::Street street, std::vector<std::uint8_t> board) {
    texas::MultiwayReplayEvent event;
    event.kind = texas::MultiwayReplayEventKind::StreetTransition;
    event.next_street = street;
    event.first_player = 0;
    event.board = std::move(board);
    return event;
}
}

TEST_CASE(multiway_full_hand_session_initializes_all_public_seat_beliefs) {
    texas::MultiwayFullHandSession session(texas::MultiwayGameRules::standard_6max(), 0, 91U);
    EXPECT_EQ(session.beliefs().seat_count(), std::size_t{6U});
    EXPECT_EQ(session.history().events.size(), std::size_t{0U});
    EXPECT_TRUE(!session.state().is_hand_over());
    for (std::size_t seat = 0U; seat < 6U; ++seat) {
        EXPECT_TRUE(session.beliefs().view(seat).valid());
        EXPECT_NEAR(session.beliefs().view(seat).metadata().normalized_mass, 1.0, 1e-12);
    }
}

TEST_CASE(multiway_full_hand_session_applies_public_decision_through_replay) {
    texas::MultiwayFullHandSession session(texas::MultiwayGameRules::standard_6max(), 0, 92U);
    const auto legal = session.state().legal_actions();
    if (legal.empty()) throw std::runtime_error("fixture has no legal action");

    texas::MultiwayReplayEvent event;
    event.kind = texas::MultiwayReplayEventKind::Decision;
    event.decision.acting_seat = session.state().current_player();
    event.decision.action = legal.front();
    event.decision.target_street_contribution =
        session.state().street_contributions()[static_cast<std::size_t>(event.decision.acting_seat)];
    event.decision.decision_seed = 17U;
    const auto& next = session.observe(event);
    EXPECT_EQ(session.history().events.size(), std::size_t{1U});
    EXPECT_EQ(next.current_player(), session.state().current_player());
}

TEST_CASE(multiway_full_hand_session_replays_flop_turn_river_and_settles) {
    texas::MultiwayGameConfig config;
    config.starting_stacks = {1000, 1000};
    config.initial_contributions = {100, 100};
    config.initial_street_contributions = {100, 100};
    config.first_player = 0;
    config.big_blind = 100;
    config.street = texas::Street::Flop;
    texas::MultiwayFullHandSession session(config, {0U, 5U, 10U}, 93U);

    session.observe(action(0, texas::MultiwayAction::Check));
    session.observe(action(1, texas::MultiwayAction::Check));
    session.observe(transition(texas::Street::Turn, {0U, 5U, 10U, 15U}));
    session.observe(action(0, texas::MultiwayAction::Check));
    session.observe(action(1, texas::MultiwayAction::Check));
    session.observe(transition(texas::Street::River, {0U, 5U, 10U, 15U, 20U}));
    session.observe(action(0, texas::MultiwayAction::Check));
    session.observe(action(1, texas::MultiwayAction::Check));

    EXPECT_EQ(session.board().size(), std::size_t{5U});
    const auto settled = session.settle({{{1U, 2U}, {3U, 4U}}});
    EXPECT_EQ(settled.payouts.size(), std::size_t{2U});
    EXPECT_EQ(settled.payouts[0] + settled.payouts[1], 200);
}

TEST_CASE(multiway_full_hand_session_preserves_folded_and_side_pot_settlement) {
    texas::MultiwayTerminalInput input;
    input.contributions = {100, 300, 300};
    input.folded = {false, true, false};
    input.strengths = {texas::Strength{5}, texas::Strength{99}, texas::Strength{8}};
    const auto result = texas::settle_multiway_terminal(input);
    EXPECT_EQ(result.payouts[1], 0);
    EXPECT_EQ(result.pots.size(), std::size_t{2U});
    EXPECT_EQ(result.payouts[0] + result.payouts[2] + result.refunds[0], 700);
}

TEST_CASE(multiway_full_hand_session_reuses_frozen_actual_hand_policy) {
    texas::MultiwayFullHandSession session(texas::MultiwayGameRules::standard_6max(), 0, 94U);
    texas::MultiwayResolverResult expected;
    expected.has_sampled_action = true;
    expected.sampled_action = {texas::MultiwayAction::Check, 0U, 0, 17U};
    expected.policy = {{expected.sampled_action, 1.0}};
    session.freeze_actual_hand_policy(expected);

    const auto actual = session.decide(0, {1U, 2U}, texas::MultiwayResolverConfig{});
    EXPECT_EQ(actual.sampled_action, expected.sampled_action);
    EXPECT_EQ(actual.policy.size(), std::size_t{1U});
    EXPECT_TRUE(session.actual_hand_policy_frozen());
}
