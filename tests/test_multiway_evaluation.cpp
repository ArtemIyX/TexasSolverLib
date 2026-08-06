#include "solver/multiway_evaluation.hpp"
#include "test_harness.hpp"

#include <array>
#include <stdexcept>
#include <vector>

namespace {

struct EvaluationContext {
    std::vector<std::uint8_t> rotations;
};

bool evaluate_match(
    const core::MultiwayEvaluationMatchRequest& request,
    core::MultiwayEvaluationSample* output,
    const void* context) noexcept {
    if (request.deal == nullptr || request.candidate_ids_by_seat == nullptr || output == nullptr) return false;
    auto* state = static_cast<EvaluationContext*>(const_cast<void*>(context));
    state->rotations.push_back(request.rotation);
    output->profile_values.seat_count = request.seat_count;
    output->best_response_values.seat_count = request.seat_count;
    for (std::size_t seat = 0; seat < request.seat_count; ++seat) {
        output->profile_values.values[seat] =
            static_cast<core::Value>(request.candidate_ids_by_seat[seat]) / 100.0;
        output->best_response_values.values[seat] = output->profile_values.values[seat] +
            static_cast<core::Value>(request.deal->duplicate_index + 1U);
    }
    output->elapsed_nanoseconds = 100U + request.rotation;
    output->resident_memory_bytes = 4096U;
    output->normalization_error = 0.0;
    output->leaf_variance = 0.25;
    output->pruned_negative_regrets = 3U;
    output->worker_imbalance = 0.1;
    return true;
}

bool evaluate_local_best_response(
    const core::MultiwayLocalBestResponseRequest&,
    core::MultiwayLocalBestResponseResult* output,
    const void*) noexcept {
    output->profile_value = 4.0;
    output->response_value = 5.0;
    return true;
}

bool evaluate_legal_off_tree(
    const core::MultiwayOffTreeGauntlet& request,
    core::MultiwayOffTreeGauntletResult* output,
    const void*) noexcept {
    output->sampled_action = request.observed_action;
    output->has_sampled_action = true;
    output->policy_total = 1.0;
    return true;
}

bool evaluate_illegal_off_tree(
    const core::MultiwayOffTreeGauntlet&,
    core::MultiwayOffTreeGauntletResult* output,
    const void*) noexcept {
    output->sampled_action = {core::MultiwayAction::Fold, 1U, 0, 71U};
    output->has_sampled_action = true;
    output->policy_total = 1.0;
    return true;
}

core::MultiwayOffTreeGauntlet make_gauntlet() {
    const core::MultiwayActionDescriptor check = {core::MultiwayAction::Check, 0U, 0, 71U};
    core::MultiwayOffTreeGauntlet gauntlet;
    gauntlet.id = 7U;
    gauntlet.public_state.legal_actions = {check};
    gauntlet.observed_action = check;
    return gauntlet;
}

core::MultiwayEvaluationConfig make_config(EvaluationContext* context) {
    core::MultiwayEvaluationConfig config;
    config.seat_count = 3U;
    config.duplicate_deals = 2U;
    config.seed = 91U;
    config.candidates = {{11U}, {29U}};
    config.evaluate_match = &evaluate_match;
    config.evaluate_local_best_response = &evaluate_local_best_response;
    config.evaluate_off_tree = &evaluate_legal_off_tree;
    config.callback_context = context;
    config.local_best_response_scenarios = {{3U, 11U, 1, 0.5}};
    config.off_tree_gauntlets = {make_gauntlet()};
    return config;
}

}  // namespace

TEST_CASE(multiway_evaluation_rotates_duplicate_deals_and_aggregates_cross_play) {
    EvaluationContext context;
    const auto result = core::evaluate_multiway_candidates(make_config(&context));

    EXPECT_TRUE(result.passed());
    EXPECT_EQ(result.cross_play.size(), 4U);
    for (const auto& cell : result.cross_play) EXPECT_EQ(cell.samples, 6U);
    EXPECT_EQ(result.metrics.samples, 30U);
    EXPECT_EQ(result.local_best_response.size(), 1U);
    EXPECT_TRUE(result.local_best_response.front().passed);
    EXPECT_EQ(result.off_tree_gauntlets.size(), 1U);
    EXPECT_TRUE(result.off_tree_gauntlets.front().passed);
    EXPECT_EQ(context.rotations.front(), 0U);
    EXPECT_EQ(context.rotations[1], 1U);
    EXPECT_EQ(context.rotations[2], 2U);
}

TEST_CASE(multiway_evaluation_reports_reduced_game_nashconv_and_confidence) {
    EvaluationContext context;
    const auto result = core::evaluate_multiway_candidates(make_config(&context));

    EXPECT_EQ(result.reduced_game_nash_conv.profile_values.size(), 3U);
    EXPECT_NEAR(result.reduced_game_nash_conv.value, 4.5, 1e-12);
    EXPECT_EQ(result.reduced_game_nash_conv.diagnostics.sample_count, 6U);
    EXPECT_EQ(result.reduced_game_nash_conv.diagnostics.confidence_level, 0.95);
    EXPECT_TRUE(result.metrics.confidence_interval_half_width > 0.0);
}

TEST_CASE(multiway_evaluation_rejects_unsupported_confidence_contracts) {
    EvaluationContext context;
    auto config = make_config(&context);
    config.confidence_level = 0.87;
    EXPECT_THROW(core::evaluate_multiway_candidates(config), std::invalid_argument);
}

TEST_CASE(multiway_evaluation_rejects_illegal_off_tree_boundary_actions) {
    EvaluationContext context;
    auto config = make_config(&context);
    config.evaluate_off_tree = &evaluate_illegal_off_tree;
    const auto result = core::evaluate_multiway_candidates(config);

    EXPECT_TRUE(!result.passed());
    EXPECT_EQ(result.off_tree_gauntlets.front().failure, core::MultiwayEvaluationFailure::OffTreeIllegalAction);
}

TEST_CASE(multiway_evaluation_is_reproducible_and_exposes_failure_fixtures) {
    EvaluationContext first_context;
    EvaluationContext second_context;
    const auto first = core::evaluate_multiway_candidates(make_config(&first_context));
    const auto second = core::evaluate_multiway_candidates(make_config(&second_context));

    EXPECT_EQ(first.cross_play.size(), second.cross_play.size());
    for (std::size_t index = 0; index < first.cross_play.size(); ++index) {
        EXPECT_EQ(first.cross_play[index].focal_candidate_id, second.cross_play[index].focal_candidate_id);
        EXPECT_EQ(first.cross_play[index].opponent_candidate_id, second.cross_play[index].opponent_candidate_id);
        EXPECT_NEAR(first.cross_play[index].mean_focal_value, second.cross_play[index].mean_focal_value, 0.0);
    }
    EXPECT_NEAR(first.reduced_game_nash_conv.value, second.reduced_game_nash_conv.value, 0.0);
    EXPECT_EQ(core::multiway_evaluation_failure_fixtures().size(), 10U);
}
