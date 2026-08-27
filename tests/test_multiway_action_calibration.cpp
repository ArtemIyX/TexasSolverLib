#include "solver/multiway_action_calibration.hpp"
#include "solver/multiway_action_abstraction.hpp"
#include "solver/multiway_evaluation.hpp"
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

double score_by_branching(const texas::MultiwayActionCalibrationResult& result, const void*) noexcept {
    return 10.0 - result.mean_branching_factor();
}

texas::MultiwayActionCalibrationQuality quality_by_branching(
    const texas::MultiwayActionCalibrationResult& result, const void*) noexcept {
    return {10.0 - result.mean_branching_factor(), 0.0};
}

bool no_artifact(const texas::MultiwayActionCalibrationCase&, const std::vector<texas::MultiwayActionDescriptor>&, const void*) noexcept {
    return false;
}
std::uint64_t slow_profile(const texas::MultiwayActionCalibrationCase&, const std::vector<texas::MultiwayActionDescriptor>&, const void*) noexcept {
    return 200U;
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

TEST_CASE(multiway_action_calibration_adapts_f4_evaluation_quality) {
    texas::MultiwayEvaluationResult evaluation;
    evaluation.reduced_game_nash_conv.value = 1.25;
    evaluation.metrics.confidence_interval_half_width = 0.2;
    const auto quality = texas::multiway_action_quality_from_evaluation(evaluation);
    EXPECT_NEAR(quality.value, -1.25, 1e-12);
    EXPECT_NEAR(quality.standard_error, 0.2, 1e-12);
}

TEST_CASE(multiway_action_calibration_gates_artifact_coverage) {
    const auto betting = state(texas::Street::Flop, 3U, 1000);
    const std::vector<texas::MultiwayActionCalibrationCase> cases = {
        {betting, texas::MultiwayAction::Check, 0, {}}};
    texas::MultiwayActionCalibrationLimits limits;
    limits.maximum_artifact_miss_rate = 0.0;
    const auto result = texas::calibrate_multiway_action_abstraction(
        cases, {}, {}, limits, no_artifact);
    EXPECT_EQ(result.artifact_coverage_cases, std::size_t{1U});
    EXPECT_EQ(result.artifact_miss_cases, std::size_t{1U});
    EXPECT_TRUE(!result.within_limits);
}

TEST_CASE(multiway_action_calibration_gates_latency_estimate) {
    const auto betting = state(texas::Street::River, 2U, 1000);
    const std::vector<texas::MultiwayActionCalibrationCase> cases = {
        {betting, texas::MultiwayAction::Check, 0, {}}};
    texas::MultiwayActionCalibrationLimits limits;
    limits.maximum_estimated_decision_nanoseconds = 100U;
    const auto result = texas::calibrate_multiway_action_abstraction(
        cases, {}, {}, limits, nullptr, nullptr, slow_profile);
    EXPECT_EQ(result.maximum_estimated_decision_nanoseconds, 200U);
    EXPECT_TRUE(!result.within_limits);
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

TEST_CASE(multiway_action_calibration_selects_only_profile_above_frozen_baseline) {
    const auto betting = state(texas::Street::Flop, 6U, 10000);
    const std::vector<texas::MultiwayActionCalibrationCase> cases = {
        {betting, texas::MultiwayAction::Check, 0, {}}};
    texas::MultiwayActionAbstractionConfig candidate;
    auto rejected = candidate;
    rejected.menu_profile_version = 2U;
    const auto selected = texas::select_multiway_action_profile(
        {candidate, rejected}, cases, 0.0, score_by_branching);
    EXPECT_TRUE(selected.selected);
    EXPECT_TRUE(selected.candidate_value > selected.baseline_value);
}

TEST_CASE(multiway_action_calibration_requires_statistical_improvement) {
    const auto betting = state(texas::Street::Flop, 6U, 10000);
    const std::vector<texas::MultiwayActionCalibrationCase> cases = {
        {betting, texas::MultiwayAction::Check, 0, {}}};
    const auto selected = texas::select_multiway_action_profile_statistically(
        {{}}, cases, {0.0, 0.0}, quality_by_branching);
    EXPECT_TRUE(selected.selected);
    const auto rejected = texas::select_multiway_action_profile_statistically(
        {{}}, cases, {10.0, 0.0}, quality_by_branching);
    EXPECT_TRUE(!rejected.selected);
}
