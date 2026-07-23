#include "solver/multiway_cfr.hpp"
#include "test_harness.hpp"

#include <cmath>
#include <limits>
#include <numeric>
#include <stdexcept>

TEST_CASE(multiway_cfr_config_accepts_two_through_six_players) {
    for (std::uint8_t players = 2; players <= 6; ++players) {
        core::MultiwayCFRConfig config;
        config.player_count = players;
        config.validate();
    }
}

TEST_CASE(multiway_cfr_config_rejects_unsupported_player_counts) {
    core::MultiwayCFRConfig config;
    config.player_count = 1;
    EXPECT_THROW(config.validate(), std::invalid_argument);
    config.player_count = 7;
    EXPECT_THROW(config.validate(), std::invalid_argument);
}

TEST_CASE(multiway_cfr_config_rejects_unknown_algorithm_and_metric) {
    core::MultiwayCFRConfig config;
    config.algorithm = static_cast<core::MultiwayCFRAlgorithm>(99);
    EXPECT_THROW(config.validate(), std::invalid_argument);
    config.algorithm = core::MultiwayCFRAlgorithm::ExternalSamplingMCCFR;
    config.quality_metric = static_cast<core::MultiwayQualityMetric>(99);
    EXPECT_THROW(config.validate(), std::invalid_argument);
    config.quality_metric = core::MultiwayQualityMetric::NashConv;
    config.deterministic_trajectory_merges = false;
    EXPECT_THROW(config.validate(), std::invalid_argument);
}

TEST_CASE(multiway_cfr_config_accepts_named_full_tree_and_external_sampling_modes) {
    core::MultiwayCFRConfig config;
    config.algorithm = core::MultiwayCFRAlgorithm::FullTreeCFR;
    config.validate();
    config.algorithm = core::MultiwayCFRAlgorithm::ExternalSamplingMCCFR;
    config.validate();
}

TEST_CASE(multiway_cfr_counterfactual_reach_multiplies_every_opponent) {
    const auto reach = core::multiway_counterfactual_reach({0.2, 0.3, 0.4, 0.5}, 2, 0.25);
    EXPECT_NEAR(reach, 0.2 * 0.3 * 0.5 * 0.25, 1e-12);
}

TEST_CASE(multiway_cfr_counterfactual_reach_excludes_only_the_traverser) {
    EXPECT_NEAR(core::multiway_counterfactual_reach({0.2, 0.3, 0.4}, 0), 0.12, 1e-12);
    EXPECT_NEAR(core::multiway_counterfactual_reach({0.2, 0.3, 0.4}, 1), 0.08, 1e-12);
    EXPECT_NEAR(core::multiway_counterfactual_reach({0.2, 0.3, 0.4}, 2), 0.06, 1e-12);
}

TEST_CASE(multiway_cfr_counterfactual_reach_rejects_invalid_probability_or_seat) {
    EXPECT_THROW(core::multiway_counterfactual_reach({0.2}, 0), std::invalid_argument);
    EXPECT_THROW(core::multiway_counterfactual_reach({0.2, 1.1}, 0), std::invalid_argument);
    EXPECT_THROW(core::multiway_counterfactual_reach({0.2, 0.3}, 2), std::invalid_argument);
}

TEST_CASE(multiway_cfr_regret_matching_uses_positive_regrets_only) {
    const auto strategy = core::multiway_regret_matching({-2.0, 0.0, 3.0, 1.0});
    EXPECT_NEAR(strategy[0], 0.0, 1e-12);
    EXPECT_NEAR(strategy[1], 0.0, 1e-12);
    EXPECT_NEAR(strategy[2], 0.75, 1e-12);
    EXPECT_NEAR(strategy[3], 0.25, 1e-12);
}

TEST_CASE(multiway_cfr_regret_matching_is_uniform_without_positive_regret) {
    const auto strategy = core::multiway_regret_matching({-1.0, 0.0, -4.0});
    EXPECT_NEAR(strategy[0], 1.0 / 3.0, 1e-12);
    EXPECT_NEAR(strategy[1], 1.0 / 3.0, 1e-12);
    EXPECT_NEAR(strategy[2], 1.0 / 3.0, 1e-12);
}

TEST_CASE(multiway_cfr_update_uses_opponent_product_for_regret) {
    const auto update = core::make_multiway_cfr_update(
        {0.5, 0.25, 0.8}, 1, 0.5, {0.25, 0.75}, {4.0, 0.0});
    EXPECT_NEAR(update.node_value, 1.0, 1e-12);
    EXPECT_NEAR(update.counterfactual_reach, 0.2, 1e-12);
    EXPECT_NEAR(update.regret_deltas[0], 0.6, 1e-12);
    EXPECT_NEAR(update.regret_deltas[1], -0.2, 1e-12);
}

TEST_CASE(multiway_cfr_update_uses_own_reach_for_average_strategy) {
    const auto update = core::make_multiway_cfr_update(
        {0.5, 0.25, 0.8}, 1, 0.5, {0.25, 0.75}, {4.0, 0.0});
    EXPECT_NEAR(update.average_strategy_weight, 0.25, 1e-12);
    EXPECT_NEAR(update.strategy_deltas[0], 0.0625, 1e-12);
    EXPECT_NEAR(update.strategy_deltas[1], 0.1875, 1e-12);
}

TEST_CASE(multiway_external_sampling_update_applies_the_explicit_importance_ratio) {
    core::MultiwayExternalSamplingRequest request;
    request.player_reaches = {0.5, 0.25, 0.8};
    request.traverser = 1;
    request.chance_reach = 0.5;
    request.sampling_reach = 0.1;
    request.traverser_reach = 0.25;
    request.strategy = {0.25, 0.75};
    request.sampled_action_values = {4.0, 0.0};
    const auto update = core::make_multiway_external_sampling_cfr_update(request);
    EXPECT_NEAR(update.node_value, 1.0, 1e-12);
    EXPECT_NEAR(update.counterfactual_reach, 0.2, 1e-12);
    EXPECT_NEAR(update.regret_deltas[0], 6.0, 1e-12);
    EXPECT_NEAR(update.regret_deltas[1], -2.0, 1e-12);
    EXPECT_NEAR(update.average_strategy_weight, 2.5, 1e-12);
}

TEST_CASE(multiway_external_sampling_update_rejects_zero_or_ambiguous_sampling_contract) {
    core::MultiwayExternalSamplingRequest request;
    request.player_reaches = {1.0, 1.0};
    request.traverser = 0;
    request.strategy = {0.5, 0.5};
    request.sampled_action_values = {1.0, 0.0};
    request.sampling_reach = 0.0;
    EXPECT_THROW(core::make_multiway_external_sampling_cfr_update(request), std::invalid_argument);
    request.sampling_reach = 1.0;
    request.traverser_reach = 0.5;
    EXPECT_THROW(core::make_multiway_external_sampling_cfr_update(request), std::invalid_argument);
}

TEST_CASE(multiway_external_sampling_update_rejects_nonfinite_importance_weights_transactionally) {
    core::MultiwayExternalSamplingRequest request;
    request.player_reaches = {1.0, 1.0};
    request.traverser = 0;
    request.sampling_reach = std::numeric_limits<double>::denorm_min();
    request.strategy = {0.5, 0.5};
    request.sampled_action_values = {1.0, 0.0};
    EXPECT_THROW(core::make_multiway_external_sampling_cfr_update(request), std::overflow_error);

    core::MultiwayCFRUpdate update;
    update.regret_deltas = {std::numeric_limits<double>::infinity()};
    update.strategy_deltas = {0.0};
    std::vector<double> regrets = {1.0};
    std::vector<double> strategy = {2.0};
    EXPECT_THROW(core::apply_multiway_cfr_update(regrets, strategy, update), std::overflow_error);
    EXPECT_NEAR(regrets[0], 1.0, 1e-12);
    EXPECT_NEAR(strategy[0], 2.0, 1e-12);
}

TEST_CASE(multiway_cfr_update_applies_to_matching_row_shapes) {
    const auto update = core::make_multiway_cfr_update(
        {1.0, 0.5, 0.5}, 0, 1.0, {0.5, 0.5}, {3.0, 1.0});
    std::vector<double> regrets = {1.0, 2.0};
    std::vector<double> strategy_sum = {4.0, 5.0};
    core::apply_multiway_cfr_update(regrets, strategy_sum, update);
    EXPECT_NEAR(regrets[0], 1.25, 1e-12);
    EXPECT_NEAR(regrets[1], 1.75, 1e-12);
    EXPECT_NEAR(strategy_sum[0], 4.5, 1e-12);
    EXPECT_NEAR(strategy_sum[1], 5.5, 1e-12);
}

TEST_CASE(multiway_cfr_update_rejects_invalid_strategy_and_row_shapes) {
    EXPECT_THROW(
        core::make_multiway_cfr_update({1.0, 1.0}, 0, 1.0, {0.4, 0.4}, {1.0, 2.0}),
        std::invalid_argument);
    const auto update = core::make_multiway_cfr_update(
        {1.0, 1.0}, 0, 1.0, {0.5, 0.5}, {1.0, 2.0});
    std::vector<double> regrets = {0.0};
    std::vector<double> strategies = {0.0};
    EXPECT_THROW(core::apply_multiway_cfr_update(regrets, strategies, update), std::invalid_argument);
}

TEST_CASE(multiway_nash_conv_sums_unilateral_improvements) {
    const auto result = core::compute_multiway_nash_conv({10.0, -3.0, 2.0}, {12.5, -1.0, 3.0});
    EXPECT_NEAR(result.unilateral_improvements[0], 2.5, 1e-12);
    EXPECT_NEAR(result.unilateral_improvements[1], 2.0, 1e-12);
    EXPECT_NEAR(result.unilateral_improvements[2], 1.0, 1e-12);
    EXPECT_NEAR(result.value, 5.5, 1e-12);
}

TEST_CASE(multiway_exact_nash_conv_rejects_a_worse_than_profile_best_response) {
    EXPECT_THROW(
        core::compute_multiway_nash_conv({1.0, 2.0}, {0.5, 2.0}),
        std::invalid_argument);
}

TEST_CASE(multiway_nash_conv_preserves_estimator_metadata_and_negative_noise) {
    core::MultiwayQualityDiagnostics diagnostics;
    diagnostics.method = core::MultiwayMetricMethod::SampledEstimate;
    diagnostics.units = core::MultiwayValueUnits::BigBlinds;
    diagnostics.sample_count = 400;
    diagnostics.seed = 99;
    diagnostics.standard_error = 0.125;
    diagnostics.per_seat_sample_counts = {200, 200};
    diagnostics.per_seat_standard_errors = {0.1, 0.1};
    diagnostics.per_seat_confidence_intervals = {0.2, 0.2};
    diagnostics.confidence_level = 0.95;
    diagnostics.confidence_interval_definition = "normal-approximation two-sided half-width";
    diagnostics.policy_version = "blueprint-v1";
    diagnostics.model_version = "value-v2";
    const auto result = core::compute_multiway_nash_conv({3.0, 4.0}, {2.5, 5.0}, diagnostics);
    EXPECT_NEAR(result.unilateral_improvements[0], -0.5, 1e-12);
    EXPECT_NEAR(result.value, 0.5, 1e-12);
    EXPECT_EQ(result.diagnostics.sample_count, 400U);
    EXPECT_EQ(result.diagnostics.seed, 99U);
}

TEST_CASE(multiway_nash_conv_rejects_sampled_metric_without_confidence_contract) {
    core::MultiwayQualityDiagnostics diagnostics;
    diagnostics.method = core::MultiwayMetricMethod::SampledEstimate;
    EXPECT_THROW(core::compute_multiway_nash_conv({1.0, 2.0}, {1.0, 2.0}, diagnostics), std::invalid_argument);
    diagnostics.sample_count = 1;
    diagnostics.standard_error = -1.0;
    EXPECT_THROW(core::compute_multiway_nash_conv({1.0, 2.0}, {1.0, 2.0}, diagnostics), std::invalid_argument);
}

TEST_CASE(multiway_nash_conv_requires_complete_per_seat_sampled_uncertainty) {
    for (std::size_t seats = 2; seats <= 6; ++seats) {
        for (std::size_t reported = 0; reported < seats; ++reported) {
            core::MultiwayQualityDiagnostics diagnostics;
            diagnostics.method = core::MultiwayMetricMethod::SampledEstimate;
            diagnostics.sample_count = 100;
            diagnostics.policy_version = "policy-v1";
            diagnostics.confidence_level = 0.95;
            diagnostics.confidence_interval_definition = "normal";
            diagnostics.per_seat_sample_counts.assign(reported, 20);
            diagnostics.per_seat_standard_errors.assign(reported, 0.1);
            diagnostics.per_seat_confidence_intervals.assign(reported, 0.2);
            std::vector<double> values(seats, 1.0);
            EXPECT_THROW(core::compute_multiway_nash_conv(values, values, diagnostics), std::invalid_argument);
        }
    }
}

TEST_CASE(multiway_nash_conv_is_zero_for_a_no_improvement_profile) {
    const auto result = core::compute_multiway_nash_conv({1.0, 2.0, 3.0, 4.0}, {1.0, 2.0, 3.0, 4.0});
    EXPECT_NEAR(result.value, 0.0, 1e-12);
}

TEST_CASE(multiway_nash_conv_rejects_bad_dimensions_and_nonfinite_values) {
    EXPECT_THROW(core::compute_multiway_nash_conv({1.0}, {1.0}), std::invalid_argument);
    EXPECT_THROW(core::compute_multiway_nash_conv({1.0, 2.0}, {1.0}), std::invalid_argument);
    EXPECT_THROW(
        core::compute_multiway_nash_conv({1.0, 2.0}, {1.0, std::numeric_limits<double>::infinity()}),
        std::invalid_argument);
}
