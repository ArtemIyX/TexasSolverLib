#include "solver/multiway_full_hand_session.hpp"
#include "test_harness.hpp"

#include <stdexcept>

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
