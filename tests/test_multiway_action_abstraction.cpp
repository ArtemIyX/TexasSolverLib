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

namespace {

bool has_exact_legal_targets(
    const core::MultiwayState& state,
    const std::vector<core::MultiwayActionDescriptor>& actions) {
    const auto actor = static_cast<std::size_t>(state.current_player());
    for (const auto& action : actions) {
        const auto successor = state.apply(action.action, action.target_street_contribution);
        if (successor.street_contributions()[actor] != action.target_street_contribution) return false;
    }
    return true;
}

}  // namespace

TEST_CASE(multiway_action_abstraction_profiles_context_and_translates_small_off_tree_bets) {
    const auto state = flop_root(3U, 2000, 100);
    const core::MultiwayActionAbstraction abstraction;
    const auto compatibility = abstraction.make_legal_actions(state.snapshot(), 39U);
    const auto contextual = abstraction.make_legal_actions(
        state.snapshot(), 40U, {core::MultiwayPreflopSituation::Auto,
            core::MultiwayRelativePosition::Unknown, core::MultiwayPostflopSizingMode::Contextual});

    EXPECT_TRUE(abstraction.menu_profile_identity() != abstraction.menu_profile_identity(
        {core::MultiwayPreflopSituation::Auto, core::MultiwayRelativePosition::Unknown,
            core::MultiwayPostflopSizingMode::Contextual}));
    EXPECT_TRUE(contains(compatibility, core::MultiwayAction::Bet, 100));
    const auto translated = abstraction.translate_observed_action(
        state.snapshot(), compatibility, core::MultiwayAction::Bet, 105);
    EXPECT_EQ(translated.status, core::MultiwayActionTranslationStatus::Translated);
    EXPECT_EQ(translated.observed_action.target_street_contribution, 105);
    EXPECT_EQ(translated.translated_action.target_street_contribution, 100);
    EXPECT_TRUE(translated.menu_profile_identity != 0U);
    EXPECT_EQ(translated.policy_version, 1U);
    EXPECT_TRUE(translated.policy_identity != 0U);

    const auto too_large = abstraction.translate_observed_action(
        state.snapshot(), compatibility, core::MultiwayAction::Bet, 125);
    EXPECT_EQ(too_large.status, core::MultiwayActionTranslationStatus::DeviationTooLarge);
    const auto all_in = abstraction.translate_observed_action(
        state.snapshot(), compatibility, core::MultiwayAction::AllIn, 1900);
    EXPECT_EQ(all_in.status, core::MultiwayActionTranslationStatus::ExactMenuAction);
    EXPECT_THROW(abstraction.translate_observed_action(
        state.snapshot(), contextual, core::MultiwayAction::Raise, 100), std::invalid_argument);
}

TEST_CASE(multiway_action_abstraction_profile_is_deterministic_and_legal_by_context) {
    const core::MultiwayActionAbstraction abstraction;
    const auto unopened = preflop_root();
    const auto facing_open = unopened.apply(core::MultiwayAction::Bet, 300);
    const auto three_way = flop_root(3U, 2000, 100);
    const std::array<core::MultiwayActionAbstractionContext, 4> contexts = {{
        {core::MultiwayPreflopSituation::Unopened, core::MultiwayRelativePosition::Unknown},
        {core::MultiwayPreflopSituation::FacingSingleOpen, core::MultiwayRelativePosition::InPosition},
        {core::MultiwayPreflopSituation::FacingSingleOpen, core::MultiwayRelativePosition::OutOfPosition},
        {core::MultiwayPreflopSituation::Auto, core::MultiwayRelativePosition::Unknown,
            core::MultiwayPostflopSizingMode::Contextual},
    }};
    const std::array<core::MultiwayState, 4> states = {{unopened, facing_open, facing_open, three_way}};

    for (std::size_t index = 0U; index < contexts.size(); ++index) {
        const auto first = abstraction.make_legal_actions(states[index].snapshot(), 41U + index, contexts[index]);
        const auto second = abstraction.make_legal_actions(states[index].snapshot(), 91U + index, contexts[index]);
        EXPECT_EQ(first, second);
        EXPECT_TRUE(!first.empty());
        EXPECT_TRUE(has_exact_legal_targets(states[index], first));
        EXPECT_TRUE(first.size() <= core::MULTIWAY_MAX_ABSTRACTED_ACTIONS);
    }

    core::MultiwayActionAbstractionConfig changed = {};
    ++changed.menu_profile_version;
    const core::MultiwayActionAbstraction changed_profile(changed);
    EXPECT_TRUE(abstraction.menu_profile_identity(contexts[3]) !=
        changed_profile.menu_profile_identity(contexts[3]));
}

TEST_CASE(multiway_action_abstraction_translation_respects_pseudo_harmonic_boundaries) {
    const auto state = flop_root(3U, 2000, 100);
    const core::MultiwayActionAbstraction abstraction;
    const auto menu = abstraction.make_legal_actions(state.snapshot(), 45U);

    const auto lower_inside = abstraction.translate_observed_action(
        state.snapshot(), menu, core::MultiwayAction::Bet, 110);
    const auto lower_outside = abstraction.translate_observed_action(
        state.snapshot(), menu, core::MultiwayAction::Bet, 111);
    const auto upper_inside = abstraction.translate_observed_action(
        state.snapshot(), menu, core::MultiwayAction::Bet, 204);
    const auto upper_outside = abstraction.translate_observed_action(
        state.snapshot(), menu, core::MultiwayAction::Bet, 203);
    EXPECT_EQ(lower_inside.status, core::MultiwayActionTranslationStatus::Translated);
    EXPECT_EQ(lower_inside.translated_action.target_street_contribution, 100);
    EXPECT_EQ(lower_outside.status, core::MultiwayActionTranslationStatus::DeviationTooLarge);
    EXPECT_EQ(upper_inside.status, core::MultiwayActionTranslationStatus::Translated);
    EXPECT_EQ(upper_inside.translated_action.target_street_contribution, 225);
    EXPECT_EQ(upper_outside.status, core::MultiwayActionTranslationStatus::DeviationTooLarge);

    core::MultiwayActionAbstractionConfig relaxed_config = {};
    relaxed_config.translation_max_pseudo_harmonic_distance_basis_points = 2223U;
    const core::MultiwayActionAbstraction relaxed(relaxed_config);
    const auto relaxed_translation = relaxed.translate_observed_action(
        state.snapshot(), menu, core::MultiwayAction::Bet, 125);
    EXPECT_EQ(relaxed_translation.status, core::MultiwayActionTranslationStatus::Translated);
    EXPECT_EQ(relaxed_translation.translated_action.target_street_contribution, 100);
    EXPECT_TRUE(relaxed_translation.policy_identity !=
        abstraction.translate_observed_action(state.snapshot(), menu, core::MultiwayAction::Bet, 125).policy_identity);

    core::MultiwayActionAbstractionConfig invalid_version = {};
    invalid_version.translation_policy_version = 0U;
    EXPECT_THROW(core::MultiwayActionAbstraction(invalid_version), std::invalid_argument);
    core::MultiwayActionAbstractionConfig invalid_threshold = {};
    invalid_threshold.translation_max_pseudo_harmonic_distance_basis_points = 20001U;
    EXPECT_THROW(core::MultiwayActionAbstraction(invalid_threshold), std::invalid_argument);
}
