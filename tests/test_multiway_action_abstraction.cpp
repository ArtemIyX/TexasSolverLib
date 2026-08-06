#include "solver/multiway_action_abstraction.hpp"
#include "test_harness.hpp"

namespace {

bool contains(
    const std::vector<core::MultiwayActionDescriptor>& actions,
    core::MultiwayAction action,
    int target) {
    for (const auto& candidate : actions) {
        if (candidate.action == action && candidate.target_street_contribution == target) return true;
    }
    return false;
}

std::size_t count_action(
    const std::vector<core::MultiwayActionDescriptor>& actions,
    core::MultiwayAction action) {
    std::size_t count = 0;
    for (const auto& candidate : actions) {
        if (candidate.action == action) ++count;
    }
    return count;
}

core::MultiwayState preflop_root() {
    core::MultiwayGameConfig config;
    config.starting_stacks = {10000, 10000, 10000, 10000, 10000, 10000};
    config.initial_contributions = {0, 0, 0, 0, 50, 100};
    config.initial_street_contributions = {0, 0, 0, 0, 50, 100};
    config.first_player = 0;
    config.big_blind = 100;
    config.street = core::Street::Preflop;
    return core::MultiwayState::initial(config);
}

core::MultiwayState flop_root(std::size_t seats, int starting_stack = 10000, int contribution = 100) {
    core::MultiwayGameConfig config;
    config.starting_stacks.assign(seats, starting_stack);
    config.initial_contributions.assign(seats, contribution);
    config.initial_street_contributions.assign(seats, 0);
    config.first_player = 0;
    config.big_blind = 100;
    config.street = core::Street::Flop;
    return core::MultiwayState::initial(config);
}

}  // namespace

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

TEST_CASE(multiway_action_abstraction_uses_default_preflop_templates) {
    const auto state = preflop_root();
    const auto actions = core::MultiwayActionAbstraction().make_legal_actions(state.snapshot(), 31U);

    EXPECT_TRUE(contains(actions, core::MultiwayAction::Fold, 0));
    EXPECT_TRUE(contains(actions, core::MultiwayAction::Call, 100));
    EXPECT_TRUE(contains(actions, core::MultiwayAction::Bet, 225));
    EXPECT_TRUE(contains(actions, core::MultiwayAction::Bet, 300));
    EXPECT_TRUE(contains(actions, core::MultiwayAction::Bet, 450));
    EXPECT_TRUE(contains(actions, core::MultiwayAction::AllIn, 10000));
    EXPECT_TRUE(actions.size() <= core::MULTIWAY_MAX_ABSTRACTED_ACTIONS);
}

TEST_CASE(multiway_action_abstraction_uses_position_and_squeeze_templates) {
    const auto opened = preflop_root().apply(core::MultiwayAction::Bet, 300);
    const core::MultiwayActionAbstraction abstraction;
    const auto out_of_position = abstraction.make_legal_actions(
        opened.snapshot(), 32U,
        {core::MultiwayPreflopSituation::FacingSingleOpen,
            core::MultiwayRelativePosition::OutOfPosition});
    EXPECT_TRUE(contains(out_of_position, core::MultiwayAction::Raise, 1050));

    const auto squeeze = abstraction.make_legal_actions(
        opened.snapshot(), 33U,
        {core::MultiwayPreflopSituation::FacingOpenAndCallers,
            core::MultiwayRelativePosition::Unknown});
    EXPECT_TRUE(contains(squeeze, core::MultiwayAction::Raise, 500));
    EXPECT_TRUE(contains(squeeze, core::MultiwayAction::Raise, 1050));
}

TEST_CASE(multiway_action_abstraction_prunes_contextual_postflop_menus) {
    const core::MultiwayActionAbstraction abstraction;
    const core::MultiwayActionAbstractionContext contextual = {
        core::MultiwayPreflopSituation::Auto,
        core::MultiwayRelativePosition::Unknown,
        core::MultiwayPostflopSizingMode::Contextual,
    };
    const auto heads_up = abstraction.make_legal_actions(flop_root(2U, 10000, 700).snapshot(), 34U, contextual);
    const auto multiway = abstraction.make_legal_actions(flop_root(4U, 10000, 700).snapshot(), 35U, contextual);
    const auto short_stack = abstraction.make_legal_actions(flop_root(3U, 1000, 700).snapshot(), 36U, contextual);

    EXPECT_EQ(count_action(heads_up, core::MultiwayAction::Bet), 4U);
    EXPECT_EQ(count_action(multiway, core::MultiwayAction::Bet), 2U);
    EXPECT_TRUE(count_action(short_stack, core::MultiwayAction::Bet) <= 1U);
    EXPECT_TRUE(contains(heads_up, core::MultiwayAction::Check, 0));
    EXPECT_TRUE(contains(heads_up, core::MultiwayAction::AllIn, 9300));
}

TEST_CASE(multiway_action_abstraction_preserves_facing_bet_basics_and_exact_raise) {
    const auto facing_bet = flop_root(3U, 2000, 100).apply(core::MultiwayAction::Bet, 300);
    const core::MultiwayActionAbstraction abstraction;
    const auto menu = abstraction.make_legal_actions(facing_bet.snapshot(), 37U);
    const auto inserted = core::MultiwayActionAbstraction::insert_exact_observed_action(
        facing_bet.snapshot(), menu, core::MultiwayAction::Raise, 600, 37U);

    EXPECT_TRUE(contains(inserted, core::MultiwayAction::Fold, 0));
    EXPECT_TRUE(contains(inserted, core::MultiwayAction::Call, 300));
    EXPECT_TRUE(contains(inserted, core::MultiwayAction::Raise, 600));
    EXPECT_TRUE(contains(inserted, core::MultiwayAction::AllIn, 1900));
}

TEST_CASE(multiway_action_abstraction_deduplicates_and_compacts_exact_insertions) {
    const auto state = flop_root(3U, 2000, 100);
    std::vector<core::MultiwayActionDescriptor> menu = {
        {core::MultiwayAction::Check, 0U, 0, 38U},
        {core::MultiwayAction::Bet, 1U, 100, 38U},
        {core::MultiwayAction::Bet, 2U, 200, 38U},
        {core::MultiwayAction::Bet, 3U, 300, 38U},
        {core::MultiwayAction::Bet, 4U, 400, 38U},
        {core::MultiwayAction::Bet, 5U, 500, 38U},
        {core::MultiwayAction::Bet, 6U, 600, 38U},
        {core::MultiwayAction::AllIn, 7U, 1900, 38U},
    };
    const auto inserted = core::MultiwayActionAbstraction::insert_exact_observed_action(
        state.snapshot(), std::move(menu), core::MultiwayAction::Bet, 650, 38U);

    EXPECT_EQ(inserted.size(), core::MULTIWAY_MAX_ABSTRACTED_ACTIONS);
    EXPECT_TRUE(contains(inserted, core::MultiwayAction::Bet, 650));
    EXPECT_TRUE(contains(inserted, core::MultiwayAction::Bet, 600));
    EXPECT_TRUE(contains(inserted, core::MultiwayAction::Check, 0));
    EXPECT_TRUE(contains(inserted, core::MultiwayAction::AllIn, 1900));
    for (std::size_t index = 0; index < inserted.size(); ++index) {
        EXPECT_EQ(inserted[index].action_index, static_cast<std::uint32_t>(index));
        const auto next = state.apply(inserted[index].action, inserted[index].target_street_contribution);
        EXPECT_EQ(next.street_contributions()[0], inserted[index].target_street_contribution);
    }
}
