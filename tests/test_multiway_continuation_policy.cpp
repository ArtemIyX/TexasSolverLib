#include "solver/multiway_continuation_policy.hpp"
#include "solver/multiway_continuation_calibration.hpp"
#include "test_harness.hpp"

#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <vector>

namespace {

constexpr std::array<texas::MultiwayActionDescriptor, 3> kActions = {{
    {texas::MultiwayAction::Fold, 0U, 0, 17U},
    {texas::MultiwayAction::Call, 1U, 100, 17U},
    {texas::MultiwayAction::Raise, 2U, 300, 17U},
}};

constexpr std::array<texas::Probability, 3> kBlueprint = {0.2, 0.3, 0.5};

struct ContinuationProviderProbe {
    texas::MultiwayContinuationLeafData data{};
    bool succeeds = true;
    std::size_t calls = 0U;
    texas::MultiwayLeafEvaluationRequest observed{};
};

bool provide_continuation_leaf(
    const texas::MultiwayLeafEvaluationRequest& request,
    texas::MultiwayContinuationLeafData* output,
    const void* context) noexcept {
    auto& probe = *const_cast<ContinuationProviderProbe*>(
        static_cast<const ContinuationProviderProbe*>(context));
    ++probe.calls;
    probe.observed = request;
    if (!probe.succeeds) return false;
    *output = probe.data;
    return true;
}

void expect_policy(
    texas::MultiwayContinuationPolicyKind kind,
    const std::array<texas::Probability, 3>& expected) {
    std::array<texas::Probability, 3> output{};
    EXPECT_TRUE(texas::MultiwayFixedContinuationPolicy::apply(
        kind, kActions.data(), kBlueprint.data(), kActions.size(), output.data()));
    texas::Probability total = 0.0;
    for (std::size_t action = 0; action < output.size(); ++action) {
        EXPECT_NEAR(output[action], expected[action], 1e-12);
        total += output[action];
    }
    EXPECT_NEAR(total, 1.0, 1e-12);
}

}  // namespace

TEST_CASE(multiway_continuation_policy_exposes_all_four_fixed_profiles) {
    EXPECT_EQ(texas::MULTIWAY_FIXED_CONTINUATION_POLICIES.size(), std::size_t{4});
    expect_policy(texas::MultiwayContinuationPolicyKind::Blueprint, {0.2, 0.3, 0.5});
    expect_policy(texas::MultiwayContinuationPolicyKind::FoldBiased, {1.0 / 1.8, 0.3 / 1.8, 0.5 / 1.8});
    expect_policy(texas::MultiwayContinuationPolicyKind::CallBiased, {0.2 / 2.2, 1.5 / 2.2, 0.5 / 2.2});
    expect_policy(texas::MultiwayContinuationPolicyKind::RaiseBiased, {0.2 / 3.0, 0.3 / 3.0, 2.5 / 3.0});
}

TEST_CASE(multiway_continuation_policy_groups_check_call_and_aggressive_actions) {
    const std::array<texas::MultiwayActionDescriptor, 5> actions = {{
        {texas::MultiwayAction::Check, 0U, 0, 17U},
        {texas::MultiwayAction::Call, 1U, 100, 17U},
        {texas::MultiwayAction::Bet, 2U, 100, 17U},
        {texas::MultiwayAction::Raise, 3U, 300, 17U},
        {texas::MultiwayAction::AllIn, 4U, 1000, 17U},
    }};
    const std::array<texas::Probability, 5> uniform = {0.2, 0.2, 0.2, 0.2, 0.2};
    std::array<texas::Probability, 5> output{};

    EXPECT_TRUE(texas::MultiwayFixedContinuationPolicy::apply(
        texas::MultiwayContinuationPolicyKind::CallBiased,
        actions.data(), uniform.data(), actions.size(), output.data()));
    EXPECT_NEAR(output[0], 5.0 / 13.0, 1e-12);
    EXPECT_NEAR(output[1], 5.0 / 13.0, 1e-12);
    EXPECT_NEAR(output[2], 1.0 / 13.0, 1e-12);

    EXPECT_TRUE(texas::MultiwayFixedContinuationPolicy::apply(
        texas::MultiwayContinuationPolicyKind::RaiseBiased,
        actions.data(), uniform.data(), actions.size(), output.data()));
    EXPECT_NEAR(output[0], 1.0 / 17.0, 1e-12);
    EXPECT_NEAR(output[1], 1.0 / 17.0, 1e-12);
    EXPECT_NEAR(output[2], 5.0 / 17.0, 1e-12);
    EXPECT_NEAR(output[3], 5.0 / 17.0, 1e-12);
    EXPECT_NEAR(output[4], 5.0 / 17.0, 1e-12);
}

TEST_CASE(multiway_continuation_policy_leaves_an_absent_class_unchanged_and_rejects_zero_rows) {
    const std::array<texas::MultiwayActionDescriptor, 2> actions = {{
        {texas::MultiwayAction::Check, 0U, 0, 17U},
        {texas::MultiwayAction::Call, 1U, 100, 17U},
    }};
    const std::array<texas::Probability, 2> blueprint = {0.25, 0.75};
    const std::array<texas::Probability, 2> zero = {0.0, 0.0};
    std::array<texas::Probability, 2> output{};

    EXPECT_TRUE(texas::MultiwayFixedContinuationPolicy::apply(
        texas::MultiwayContinuationPolicyKind::RaiseBiased,
        actions.data(), blueprint.data(), actions.size(), output.data()));
    EXPECT_NEAR(output[0], blueprint[0], 1e-12);
    EXPECT_NEAR(output[1], blueprint[1], 1e-12);
    EXPECT_TRUE(!texas::MultiwayFixedContinuationPolicy::apply(
        texas::MultiwayContinuationPolicyKind::Blueprint,
        actions.data(), zero.data(), zero.size(), output.data()));
}

TEST_CASE(multiway_continuation_policy_normalizes_in_place_and_evaluates_the_same_expectation) {
    std::array<texas::Probability, 3> aliased = kBlueprint;
    EXPECT_TRUE(texas::MultiwayFixedContinuationPolicy::apply(
        texas::MultiwayContinuationPolicyKind::RaiseBiased,
        kActions.data(), aliased.data(), aliased.size(), aliased.data(), 2.0));
    EXPECT_NEAR(aliased[0] + aliased[1] + aliased[2], 1.0, 1e-12);

    const std::array<texas::Value, 3> values = {-4.0, 2.0, 10.0};
    texas::Value evaluated = 0.0;
    EXPECT_TRUE(texas::MultiwayFixedContinuationPolicy::evaluate_leaf(
        texas::MultiwayContinuationPolicyKind::RaiseBiased,
        kActions.data(), kBlueprint.data(), values.data(), values.size(), &evaluated, 2.0));
    const auto expected = aliased[0] * values[0] + aliased[1] * values[1] + aliased[2] * values[2];
    EXPECT_NEAR(evaluated, expected, 1e-12);
}

TEST_CASE(multiway_continuation_policy_rejects_invalid_inputs_without_throwing) {
    std::array<texas::Probability, 3> output{};
    auto invalid_blueprint = kBlueprint;
    invalid_blueprint[1] = -0.1;
    auto invalid_actions = kActions;
    invalid_actions[1].action = static_cast<texas::MultiwayAction>(255U);
    const auto invalid_kind = static_cast<texas::MultiwayContinuationPolicyKind>(255U);
    const auto nan = std::numeric_limits<texas::Probability>::quiet_NaN();
    const std::array<texas::Value, 3> values = {1.0, 2.0, 3.0};
    texas::Value value = 0.0;

    EXPECT_TRUE(!texas::MultiwayFixedContinuationPolicy::apply(
        invalid_kind, kActions.data(), kBlueprint.data(), kBlueprint.size(), output.data()));
    EXPECT_TRUE(!texas::MultiwayFixedContinuationPolicy::apply(
        texas::MultiwayContinuationPolicyKind::Blueprint,
        nullptr, kBlueprint.data(), kBlueprint.size(), output.data()));
    EXPECT_TRUE(!texas::MultiwayFixedContinuationPolicy::apply(
        texas::MultiwayContinuationPolicyKind::Blueprint,
        kActions.data(), invalid_blueprint.data(), invalid_blueprint.size(), output.data()));
    EXPECT_TRUE(!texas::MultiwayFixedContinuationPolicy::apply(
        texas::MultiwayContinuationPolicyKind::Blueprint,
        invalid_actions.data(), kBlueprint.data(), invalid_actions.size(), output.data()));
    EXPECT_TRUE(!texas::MultiwayFixedContinuationPolicy::apply(
        texas::MultiwayContinuationPolicyKind::Blueprint,
        kActions.data(), kBlueprint.data(), 0U, output.data()));
    EXPECT_TRUE(!texas::MultiwayFixedContinuationPolicy::apply(
        texas::MultiwayContinuationPolicyKind::Blueprint,
        kActions.data(), kBlueprint.data(), kBlueprint.size(), output.data(), nan));
    EXPECT_TRUE(!texas::MultiwayFixedContinuationPolicy::evaluate_leaf(
        texas::MultiwayContinuationPolicyKind::Blueprint,
        kActions.data(), kBlueprint.data(), values.data(), values.size(), nullptr));
    EXPECT_TRUE(!texas::MultiwayFixedContinuationPolicy::evaluate_leaf(
        texas::MultiwayContinuationPolicyKind::Blueprint,
        kActions.data(), kBlueprint.data(), values.data(), values.size(), &value, 0.0));
}

TEST_CASE(multiway_continuation_leaf_adapter_uses_caller_owned_views) {
    std::array<texas::Probability, 3> blueprint = kBlueprint;
    std::array<texas::Value, 3> action_values = {-4.0, 2.0, 10.0};
    ContinuationProviderProbe provider;
    provider.data = {
        kActions.data(), blueprint.data(), action_values.data(), action_values.size(),
    };
    texas::MultiwayContinuationLeafContext context;
    context.policy = texas::MultiwayContinuationPolicyKind::RaiseBiased;
    context.provide = provide_continuation_leaf;
    context.provider_context = &provider;
    context.bias_factor = 2.0;
    const auto evaluator = texas::make_multiway_fixed_continuation_leaf_evaluator(&context);
    texas::MultiwayBettingSnapshot betting;
    std::vector<std::uint8_t> board = {1U, 2U, 3U};
    const texas::MultiwayLeafEvaluationRequest request = {
        &betting, &board, 2,
    };

    EXPECT_TRUE(evaluator.valid());
    EXPECT_NEAR(evaluator(request), 9.8 / 1.5, 1e-12);
    EXPECT_EQ(provider.calls, std::size_t{1});
    EXPECT_EQ(provider.observed.betting, request.betting);
    EXPECT_EQ(provider.observed.board, request.board);
    EXPECT_EQ(provider.observed.traverser, 2);

    action_values[2] = 16.0;
    EXPECT_NEAR(evaluator(request), 15.8 / 1.5, 1e-12);
    EXPECT_EQ(provider.calls, std::size_t{2});
}

TEST_CASE(multiway_continuation_leaf_adapter_rejects_provider_failure_and_nonfinite_data) {
    std::array<texas::Value, 3> action_values = {-4.0, 2.0, 10.0};
    ContinuationProviderProbe provider;
    provider.data = {
        kActions.data(), kBlueprint.data(), action_values.data(), action_values.size(),
    };
    texas::MultiwayContinuationLeafContext context;
    context.policy = texas::MultiwayContinuationPolicyKind::Blueprint;
    context.provide = provide_continuation_leaf;
    context.provider_context = &provider;
    const auto evaluator = texas::make_multiway_fixed_continuation_leaf_evaluator(&context);
    const texas::MultiwayLeafEvaluationRequest request = {};

    provider.succeeds = false;
    EXPECT_TRUE(std::isnan(evaluator(request)));
    provider.succeeds = true;
    action_values[1] = std::numeric_limits<texas::Value>::infinity();
    EXPECT_TRUE(std::isnan(evaluator(request)));
    EXPECT_TRUE(std::isnan(texas::evaluate_multiway_fixed_continuation_leaf(request, nullptr)));

    context.provide = nullptr;
    const auto missing_provider = texas::make_multiway_fixed_continuation_leaf_evaluator(&context);
    EXPECT_TRUE(std::isnan(missing_provider(request)));

    EXPECT_TRUE(!texas::make_multiway_fixed_continuation_leaf_evaluator(nullptr).valid());
}

TEST_CASE(multiway_continuation_calibration_reports_holdout_error_and_identity) {
    texas::MultiwayContinuationCalibrationConfig config;
    config.policy = texas::MultiwayContinuationPolicyKind::FoldBiased;
    const std::vector<texas::MultiwayContinuationCalibrationCase> cases = {
        {{{1.0, 2.0, 3.0, 4.0}}, 1.5, true},
        {{{1.0, 2.0, 3.0, 4.0}}, 1.5, false}};
    const auto result = texas::calibrate_multiway_continuation(config, cases);
    EXPECT_TRUE(result.identity != 0U);
    EXPECT_NEAR(result.held_out_error, 0.5, 1e-12);
    EXPECT_TRUE(result.within_limits);
}
