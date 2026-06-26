#include "games/kuhn.hpp"
#include "test_harness.hpp"

#include <algorithm>

TEST_CASE(kuhn_initial_state_is_chance_node) {
    const auto state = core::KuhnState::initial();
    EXPECT_EQ(state.current_player(), -1);
    EXPECT_TRUE(!state.is_terminal());
    EXPECT_TRUE(state.legal_actions().empty());
    EXPECT_EQ(state.chance_outcomes().size(), 3U);
}

TEST_CASE(kuhn_showdown_and_fold_utilities_match_rust_tests) {
    const auto showdown = core::KuhnState::initial().next_state(13).next_state(12).next_state(core::PASS).next_state(core::PASS);
    EXPECT_TRUE(showdown.is_terminal());
    EXPECT_EQ(showdown.utility()[0], 1.0);
    EXPECT_EQ(showdown.utility()[1], -1.0);

    const auto fold = core::KuhnState::initial().next_state(11).next_state(13).next_state(core::PASS).next_state(core::BET).next_state(core::PASS);
    EXPECT_TRUE(fold.is_terminal());
    EXPECT_EQ(fold.utility()[0], -1.0);
    EXPECT_EQ(fold.utility()[1], 1.0);
}

TEST_CASE(kuhn_infoset_key_includes_private_card_and_history) {
    const auto state = core::KuhnState::initial().next_state(12).next_state(13).next_state(core::PASS).next_state(core::BET);
    EXPECT_EQ(state.infoset_key(0), std::string("12|pb"));
    EXPECT_EQ(state.infoset_key(1), std::string("13|pb"));
}


