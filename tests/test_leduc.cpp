#include "games/leduc.hpp"
#include "test_harness.hpp"

#include <string>

TEST_CASE(leduc_initial_state_matches_rust_port) {
    const auto state = core::LeducState::initial();
    EXPECT_EQ(state.cur_player, -1);
    EXPECT_EQ(state.ante[0], 1);
    EXPECT_EQ(state.ante[1], 1);
    EXPECT_TRUE(state.private_cards.empty());
    EXPECT_EQ(state.chance_outcomes().size(), 6U);
}

TEST_CASE(leduc_round_one_raise_updates_stakes_and_actions) {
    auto state = core::LeducState::initial();
    state = state.next_state(11);
    state = state.next_state(12);
    EXPECT_EQ(state.current_player(), 0);
    EXPECT_EQ(state.legal_actions().size(), 2U);

    state = state.next_state(core::LEDUC_RAISE);
    EXPECT_EQ(state.stakes, 3);
    EXPECT_EQ(state.ante[0], 3);
    EXPECT_EQ(state.num_raises, 1);
    EXPECT_EQ(state.current_player(), 1);
    EXPECT_EQ(state.legal_actions().size(), 3U);
}

TEST_CASE(leduc_infoset_key_and_fold_utility) {
    auto state = core::LeducState::initial();
    state = state.next_state(11);
    state = state.next_state(13);
    EXPECT_EQ(state.infoset_key(0), std::string("11|"));

    state = state.next_state(core::LEDUC_RAISE);
    state = state.next_state(core::LEDUC_FOLD);
    EXPECT_TRUE(state.is_terminal());
    EXPECT_EQ(state.utility()[0], 1.0);
    EXPECT_EQ(state.utility()[1], -1.0);
}


