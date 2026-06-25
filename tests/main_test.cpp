#include <gtest/gtest.h>

#include "core/kuhn.hpp"
#include "core/solver.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace {

const std::vector<double>* FindStrategy(
    const core::SolveOutput& output,
    const std::string& key) {
    auto it = std::find_if(
        output.average_strategy.begin(),
        output.average_strategy.end(),
        [&key](const auto& entry) { return entry.first == key; });
    return it == output.average_strategy.end() ? nullptr : &it->second;
}

}  // namespace

TEST(KuhnStateTest, InitialStateUsesChanceBeforePlayerAction) {
    const auto state = core::KuhnState::initial();

    EXPECT_FALSE(state.is_terminal());
    EXPECT_EQ(state.current_player(), -1);
    EXPECT_TRUE(state.legal_actions().empty());
    EXPECT_EQ(state.chance_outcomes().size(), 3U);
}

TEST(KuhnStateTest, ChanceDealsUniqueCardsAndBuildsInfosets) {
    const auto first = core::KuhnState::initial().next_state(11);
    const auto second = first.next_state(13);

    EXPECT_EQ(second.cards[0], 11);
    EXPECT_EQ(second.cards[1], 13);
    EXPECT_EQ(second.current_player(), 0);
    EXPECT_EQ(second.legal_actions(), (std::vector<core::ActionId>{core::PASS, core::BET}));
    EXPECT_EQ(second.infoset_key(0), "11|");
    EXPECT_EQ(second.infoset_key(1), "13|");
}

TEST(KuhnStateTest, TerminalUtilitiesMatchCanonicalKuhnPayoffs) {
    const auto base = core::KuhnState::initial().next_state(13).next_state(11);

    EXPECT_EQ(base.next_state(core::PASS).next_state(core::PASS).utility()[0], 1.0);
    EXPECT_EQ(base.next_state(core::BET).next_state(core::PASS).utility()[0], 1.0);
    EXPECT_EQ(base.next_state(core::BET).next_state(core::BET).utility()[0], 2.0);
    EXPECT_EQ(base.next_state(core::PASS).next_state(core::BET).next_state(core::PASS).utility()[0], -1.0);
    EXPECT_EQ(base.next_state(core::PASS).next_state(core::BET).next_state(core::BET).utility()[0], 2.0);
}

TEST(SolverTest, RejectsNegativeDcfrParameters) {
    EXPECT_THROW(core::solve_kuhn(10, -1.0, 0.0, 2.0), std::invalid_argument);
}

TEST(SolverTest, KuhnSolveProducesStrategiesAndFiniteMetrics) {
    const auto output = core::solve_kuhn(200, 1.5, 0.0, 2.0);

    EXPECT_EQ(output.iterations, 200U);
    EXPECT_FALSE(output.average_strategy.empty());
    EXPECT_TRUE(std::isfinite(output.game_value));
    EXPECT_TRUE(std::isfinite(output.exploitability));

    const auto* root_jack = FindStrategy(output, "11|");
    ASSERT_NE(root_jack, nullptr);
    ASSERT_EQ(root_jack->size(), 2U);
    EXPECT_NEAR((*root_jack)[0] + (*root_jack)[1], 1.0, 1e-9);
    EXPECT_GE((*root_jack)[0], 0.0);
    EXPECT_GE((*root_jack)[1], 0.0);
}
