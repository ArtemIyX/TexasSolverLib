#include <gtest/gtest.h>

#include "core/exploit.hpp"
#include "core/hunl.hpp"
#include "core/hunl_eval.hpp"
#include "core/hunl_tree.hpp"
#include "core/kuhn.hpp"
#include "core/leduc.hpp"
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

TEST(LeducStateTest, InitialStateMatchesRoundOneChanceSetup) {
    const auto state = core::LeducState::initial();

    EXPECT_EQ(state.current_player(), -1);
    EXPECT_EQ(state.round_num, 1);
    EXPECT_EQ(state.ante[0], 1);
    EXPECT_EQ(state.ante[1], 1);
    EXPECT_EQ(state.stakes, 1);
    EXPECT_EQ(state.chance_outcomes().size(), 6U);
}

TEST(LeducStateTest, CompletingRoundOneTransitionsBackToChance) {
    auto state = core::LeducState::initial();
    state = state.next_state(11);
    state = state.next_state(12);

    ASSERT_EQ(state.current_player(), 0);
    state = state.next_state(core::LEDUC_CALL);
    ASSERT_EQ(state.current_player(), 1);
    state = state.next_state(core::LEDUC_CALL);

    EXPECT_EQ(state.current_player(), -1);
    EXPECT_EQ(state.round_num, 1);
    EXPECT_FALSE(state.public_card.has_value());
    EXPECT_EQ(state.chance_outcomes().size(), 4U);
}

TEST(LeducStateTest, PublicCardStartsRoundTwoAndResetsRoundCounters) {
    auto state = core::LeducState::initial();
    state = state.next_state(11);
    state = state.next_state(12);
    state = state.next_state(core::LEDUC_CALL);
    state = state.next_state(core::LEDUC_CALL);
    state = state.next_state(13);

    EXPECT_EQ(state.round_num, 2);
    ASSERT_TRUE(state.public_card.has_value());
    EXPECT_EQ(*state.public_card, 13);
    EXPECT_EQ(state.current_player(), 0);
    EXPECT_EQ(state.num_raises, 0);
    EXPECT_EQ(state.num_calls, 0);
    EXPECT_EQ(state.infoset_key(0), "11|cc|13|");
}

TEST(SolverTest, LeducSolveProducesStrategiesAndFiniteMetrics) {
    const auto output = core::solve_leduc(50, 1.5, 0.0, 2.0);

    EXPECT_EQ(output.iterations, 50U);
    EXPECT_FALSE(output.average_strategy.empty());
    EXPECT_TRUE(std::isfinite(output.game_value));
    EXPECT_TRUE(std::isfinite(output.exploitability));
}

TEST(HUNLConfigTest, RejectsMalformedPreflopDeadMoneyMix) {
    core::HUNLConfig config;
    config.starting_street = core::Street::Preflop;
    config.initial_contributions = {50, 100};
    config.initial_pot = 0;

    EXPECT_THROW(config.validate(), std::invalid_argument);
}

TEST(HUNLStateTest, BuildsPostflopInitialStateFromConfig) {
    auto config = std::make_shared<core::HUNLConfig>();
    config->starting_street = core::Street::Flop;
    config->initial_board = {
        core::card_to_int(14, 0), core::card_to_int(13, 1), core::card_to_int(12, 2)};
    config->initial_contributions = {500, 500};
    config->initial_hole_cards = std::array<std::array<std::uint8_t, 2>, 2>{{
        {core::card_to_int(11, 0), core::card_to_int(11, 1)},
        {core::card_to_int(10, 0), core::card_to_int(10, 1)},
    }};
    config->initial_pot = 1000;

    const auto state = core::HUNLState::initial(config);

    EXPECT_EQ(state.street, core::Street::Flop);
    EXPECT_EQ(state.cur_player, 1);
    EXPECT_EQ(state.to_call, 0);
    EXPECT_EQ(state.street_aggressor, -1);
    EXPECT_EQ(state.street_num_raises, 0);
    ASSERT_TRUE(state.hole_cards.has_value());
    EXPECT_EQ(state.board.size(), 3U);
}

TEST(HUNLStateTest, PreflopInitialStatePostsBlindsFromConfig) {
    auto config = std::make_shared<core::HUNLConfig>();
    config->starting_street = core::Street::Preflop;
    config->initial_hole_cards = std::array<std::array<std::uint8_t, 2>, 2>{{
        {core::card_to_int(14, 0), core::card_to_int(13, 0)},
        {core::card_to_int(12, 0), core::card_to_int(11, 0)},
    }};

    const auto state = core::HUNLState::initial(config);

    EXPECT_EQ(state.street, core::Street::Preflop);
    EXPECT_EQ(state.contributions[0], config->small_blind + config->ante);
    EXPECT_EQ(state.contributions[1], config->big_blind + config->ante);
    EXPECT_EQ(state.to_call, config->big_blind - config->small_blind);
    EXPECT_EQ(state.cur_player, 0);
    EXPECT_EQ(state.street_aggressor, 1);
}

TEST(HUNLStateTest, ActionContextMatchesRustStylePotAccounting) {
    auto config = std::make_shared<core::HUNLConfig>();
    config->starting_street = core::Street::River;
    config->initial_board = {
        core::card_to_int(14, 0), core::card_to_int(7, 3), core::card_to_int(2, 2),
        core::card_to_int(13, 1), core::card_to_int(5, 0)};
    config->initial_contributions = {500, 500};
    config->initial_hole_cards = std::array<std::array<std::uint8_t, 2>, 2>{{
        {core::card_to_int(14, 1), core::card_to_int(13, 3)},
        {core::card_to_int(12, 2), core::card_to_int(12, 1)},
    }};
    config->initial_pot = 1000;

    const auto state = core::HUNLState::initial(config);
    const auto ctx = state.action_context();

    EXPECT_EQ(ctx.pot, 1000);
    EXPECT_EQ(ctx.cur_player, 1);
    EXPECT_EQ(ctx.street, core::Street::River);
    EXPECT_EQ(ctx.street_action_count, 0U);
}

TEST(HUNLStateTest, InfosetKeyMatchesLosslessRustFormat) {
    auto config = std::make_shared<core::HUNLConfig>(core::default_tiny_subgame());
    const auto state = core::HUNLState::initial(config);

    EXPECT_EQ(state.infoset_key(0), "KcAh|2d5s7cKhAs|r|");
    EXPECT_EQ(state.infoset_key(1), "QhQd|2d5s7cKhAs|r|");
}

TEST(HUNLStateTest, RiverRootLegalActionsMatchRustStyleOpenNode) {
    auto config = std::make_shared<core::HUNLConfig>(core::default_tiny_subgame());
    const auto state = core::HUNLState::initial(config);
    const auto actions = state.legal_actions();

    EXPECT_EQ(actions.front(), core::ACTION_CHECK);
    EXPECT_TRUE(std::find(actions.begin(), actions.end(), core::ACTION_BET_33) != actions.end());
    EXPECT_TRUE(std::find(actions.begin(), actions.end(), core::ACTION_ALL_IN) != actions.end());
}

TEST(HUNLStateTest, CheckCheckAdvancesRiverToShowdown) {
    auto config = std::make_shared<core::HUNLConfig>(core::default_tiny_subgame());
    auto state = core::HUNLState::initial(config);
    state = state.apply(core::ACTION_CHECK);
    state = state.apply(core::ACTION_CHECK);

    EXPECT_EQ(state.street, core::Street::Showdown);
    EXPECT_TRUE(state.is_terminal());
    EXPECT_EQ(state.current_player(), -1);
}

TEST(HUNLEvalTest, RoyalFlushBeatsQuads) {
    const std::array<std::uint8_t, 5> royal_flush = {
        core::card_to_int(14, 0),
        core::card_to_int(13, 0),
        core::card_to_int(12, 0),
        core::card_to_int(11, 0),
        core::card_to_int(10, 0),
    };
    const std::array<std::uint8_t, 5> quads = {
        core::card_to_int(14, 1),
        core::card_to_int(14, 2),
        core::card_to_int(14, 3),
        core::card_to_int(13, 1),
        core::card_to_int(2, 0),
    };

    EXPECT_GT(core::Strength::evaluate_5(royal_flush), core::Strength::evaluate_5(quads));
}

TEST(HUNLEvalTest, WheelStraightRanksBelowSixHighStraight) {
    const std::array<std::uint8_t, 5> wheel = {
        core::card_to_int(14, 0),
        core::card_to_int(2, 1),
        core::card_to_int(3, 2),
        core::card_to_int(4, 3),
        core::card_to_int(5, 0),
    };
    const std::array<std::uint8_t, 5> six_high = {
        core::card_to_int(6, 1),
        core::card_to_int(2, 2),
        core::card_to_int(3, 3),
        core::card_to_int(4, 0),
        core::card_to_int(5, 1),
    };

    EXPECT_LT(core::Strength::evaluate_5(wheel), core::Strength::evaluate_5(six_high));
}

TEST(HUNLEvalTest, SevenCardEvaluationPicksBestFive) {
    const std::array<std::uint8_t, 7> seven = {
        core::card_to_int(14, 0),
        core::card_to_int(11, 0),
        core::card_to_int(8, 0),
        core::card_to_int(5, 0),
        core::card_to_int(2, 0),
        core::card_to_int(7, 1),
        core::card_to_int(6, 2),
    };
    const std::array<std::uint8_t, 5> five = {
        core::card_to_int(14, 0),
        core::card_to_int(11, 0),
        core::card_to_int(8, 0),
        core::card_to_int(5, 0),
        core::card_to_int(2, 0),
    };

    EXPECT_EQ(core::Strength::evaluate_7(seven), core::Strength::evaluate_5(five));
}

TEST(HUNLTreeTest, BuildCreatesRootNodeFromInitialState) {
    auto config = std::make_shared<core::HUNLConfig>();
    config->starting_street = core::Street::Flop;
    config->initial_board = {
        core::card_to_int(14, 0), core::card_to_int(13, 1), core::card_to_int(12, 2)};
    config->initial_contributions = {500, 500};
    config->initial_hole_cards = std::array<std::array<std::uint8_t, 2>, 2>{{
        {core::card_to_int(11, 0), core::card_to_int(11, 1)},
        {core::card_to_int(10, 0), core::card_to_int(10, 1)},
    }};
    config->initial_pot = 1000;

    const auto tree = core::HUNLTree::build(config);

    ASSERT_EQ(tree.root, 0U);
    ASSERT_GT(tree.nodes.size(), 1U);
    EXPECT_EQ(tree.nodes[0].player, 1);
    EXPECT_EQ(tree.nodes[0].street, core::Street::Flop);
    EXPECT_EQ(tree.nodes[0].terminal_kind.tag, core::TerminalKindTag::NonTerminal);
    EXPECT_FALSE(tree.nodes[0].legal_actions.empty());
    ASSERT_TRUE(tree.nodes[0].infoset_key.has_value());
    EXPECT_GE(tree.max_actions, tree.nodes[0].num_actions);
}

TEST(HUNLTreeTest, RiverSubgameTreeBuildsAndHasLeaves) {
    auto config = std::make_shared<core::HUNLConfig>(core::default_tiny_subgame());
    const auto tree = core::HUNLTree::build(config);

    bool fold_seen = false;
    bool showdown_seen = false;
    for (const auto& node : tree.nodes) {
        if (node.terminal_kind.tag == core::TerminalKindTag::Fold) {
            fold_seen = true;
        }
        if (node.terminal_kind.tag == core::TerminalKindTag::Showdown) {
            showdown_seen = true;
        }
    }

    EXPECT_TRUE(fold_seen);
    EXPECT_TRUE(showdown_seen);
    EXPECT_LT(tree.nodes.size(), 100000U);
}

TEST(HUNLTreeTest, FoldedStateClassifiesAsFoldTerminal) {
    core::HUNLState state;
    state.street = core::Street::Turn;
    state.contributions = {800, 800};
    state.folded = {true, false};

    const auto kind = core::classify_terminal_kind(state);

    EXPECT_EQ(kind.tag, core::TerminalKindTag::Fold);
    EXPECT_EQ(kind.winner, 1);
    EXPECT_EQ(kind.contribution_loss, 800);
}

TEST(HUNLTreeTest, DoubleAllInClassifiesAsShowdownTerminal) {
    core::HUNLState state;
    state.street = core::Street::River;
    state.board = {
        core::card_to_int(14, 0), core::card_to_int(13, 1), core::card_to_int(12, 2),
        core::card_to_int(11, 3), core::card_to_int(10, 0)};
    state.all_in = {true, true};

    const auto kind = core::classify_terminal_kind(state);

    EXPECT_EQ(kind.tag, core::TerminalKindTag::Showdown);
    EXPECT_TRUE(kind.board_complete);
}

TEST(ExploitTest, FixedComboRiverStrategyProducesFiniteOutput) {
    auto config = core::default_tiny_subgame();
    auto shared = std::make_shared<const core::HUNLConfig>(config);
    const auto state = core::HUNLState::initial(shared);
    std::unordered_map<std::string, std::vector<double>> strategy;
    strategy.emplace(state.infoset_key(1), std::vector<double>(state.legal_actions().size(), 1.0 / state.legal_actions().size()));

    const auto output = core::compute_exploitability_and_value(config, strategy);

    EXPECT_TRUE(std::isfinite(output.exploitability));
    EXPECT_TRUE(std::isfinite(output.game_value));
}
