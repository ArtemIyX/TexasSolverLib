#include "solver/multiway_action_calibration.hpp"
#include "solver/multiway_action_abstraction.hpp"
#include "test_harness.hpp"

namespace {
texas::MultiwayBettingSnapshot state(texas::Street street, std::size_t seats, int stack) {
    texas::MultiwayGameConfig config;
    config.starting_stacks.assign(seats, stack);
    config.initial_contributions.assign(seats, street == texas::Street::Preflop ? 0 : 100);
    config.initial_street_contributions.assign(seats, street == texas::Street::Preflop ? 0 : 100);
    config.first_player = 0;
    config.big_blind = 100;
    config.street = street;
    return texas::MultiwayState::initial(config).snapshot();
}
}

TEST_CASE(multiway_action_calibration_reports_representative_branching_and_identity) {
    const auto preflop = state(texas::Street::Preflop, 6U, 10000);
    const auto flop = state(texas::Street::Flop, 3U, 1000);
    const std::vector<texas::MultiwayActionCalibrationCase> cases = {
        {preflop, texas::MultiwayAction::Bet, 325, {texas::MultiwayPreflopSituation::Unopened}},
        {flop, texas::MultiwayAction::Check, 0, {}}};
    const auto result = texas::calibrate_multiway_action_abstraction(cases);
    EXPECT_EQ(result.cases, std::size_t{2U});
    EXPECT_TRUE(result.total_actions > 0U);
    EXPECT_TRUE(result.max_actions <= texas::MULTIWAY_MAX_ABSTRACTED_ACTIONS);
    EXPECT_TRUE(result.profile_identity != 0U);
    EXPECT_TRUE(result.translated_actions + result.exact_translations + result.rejected_translations == 2U);
}

TEST_CASE(multiway_action_calibration_threshold_sweep_changes_rejection_result) {
    const auto betting = state(texas::Street::Preflop, 6U, 10000);
    const std::vector<texas::MultiwayActionCalibrationCase> cases = {
        {betting, texas::MultiwayAction::Bet, 1000, {texas::MultiwayPreflopSituation::Unopened}}};
    texas::MultiwayActionAbstractionConfig permissive;
    permissive.translation_max_pseudo_harmonic_distance_basis_points = 20000U;
    auto strict = permissive;
    strict.translation_max_pseudo_harmonic_distance_basis_points = 1U;
    const auto accepted = texas::calibrate_multiway_action_abstraction(cases, permissive);
    const auto rejected = texas::calibrate_multiway_action_abstraction(cases, strict);
    EXPECT_TRUE(accepted.rejected_translations <= rejected.rejected_translations);
}

TEST_CASE(multiway_action_calibration_covers_short_stack_and_multiway_profiles) {
    const auto short_stack = state(texas::Street::Flop, 3U, 200);
    const auto multiway = state(texas::Street::Flop, 6U, 10000);
    const std::vector<texas::MultiwayActionCalibrationCase> cases = {
        {short_stack, texas::MultiwayAction::Check, 0, {texas::MultiwayPreflopSituation::Auto,
            texas::MultiwayRelativePosition::Unknown, texas::MultiwayPostflopSizingMode::Contextual}},
        {multiway, texas::MultiwayAction::Check, 0, {texas::MultiwayPreflopSituation::Auto,
            texas::MultiwayRelativePosition::Unknown, texas::MultiwayPostflopSizingMode::Contextual}}};
    const auto result = texas::calibrate_multiway_action_abstraction(cases);
    EXPECT_TRUE(result.within_limits);
    EXPECT_TRUE(result.max_actions <= texas::MULTIWAY_MAX_ABSTRACTED_ACTIONS);
    EXPECT_TRUE(result.estimated_menu_bytes > 0U);
}

TEST_CASE(multiway_action_calibration_rejects_excessive_menu_budget) {
    const auto betting = state(texas::Street::Flop, 6U, 10000);
    const std::vector<texas::MultiwayActionCalibrationCase> cases = {
        {betting, texas::MultiwayAction::Check, 0, {}}};
    texas::MultiwayActionCalibrationLimits limits;
    limits.maximum_estimated_menu_bytes = sizeof(texas::MultiwayActionDescriptor);
    const auto result = texas::calibrate_multiway_action_abstraction(cases, {}, {}, limits);
    EXPECT_TRUE(!result.within_limits);
}
