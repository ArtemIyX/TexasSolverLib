#include "solver/multiway/continuation/multiway_rollout_leaf.hpp"
#include "test_harness.hpp"

#include <array>
#include <cmath>
#include <cstdint>

namespace {

struct RolloutFixture {
    texas::MultiwayFixedState state{};
    std::array<std::array<std::uint8_t, 2>, texas::kMultiwayFixedMaxSeats> holes{};
    std::array<std::uint8_t, 5> board = {0U, 5U, 10U, 0U, 0U};
    texas::MultiwayRolloutScratch scratch{};
    std::array<std::uint64_t, 3> seeds = {17U, 29U, 41U};
    mutable std::size_t input_calls = 0U;

    RolloutFixture() {
        state.seat_count = 2U;
        state.stacks[0] = 0;
        state.stacks[1] = 0;
        state.contributions[0] = 100;
        state.contributions[1] = 100;
        state.street_contributions[0] = 100;
        state.street_contributions[1] = 100;
        state.folded[0] = false;
        state.folded[1] = false;
        state.all_in[0] = true;
        state.all_in[1] = true;
        state.current_player = -1;
        state.last_aggressor = -1;
        state.current_bet = 100;
        state.last_full_raise_size = 100;
        state.big_blind = 100;
        state.street = texas::Street::Flop;
        holes[0] = {1U, 2U};
        holes[1] = {3U, 4U};
    }
};

bool provide_input(const texas::MultiwayLeafEvaluationRequest&, texas::MultiwayRolloutInput* output,
                   const void* context) noexcept {
    const auto& fixture = *static_cast<const RolloutFixture*>(context);
    ++fixture.input_calls;
    output->state = &fixture.state;
    output->holes = &fixture.holes;
    output->board = fixture.board.data();
    output->board_count = 3U;
    output->next_street_first_player = 0;
    output->odd_chip_first_seat = 0;
    return true;
}

bool unused_actions(const texas::MultiwayFixedState&, const std::uint8_t*, std::uint8_t,
                    texas::MultiwayRolloutActionMenu*, const void*) noexcept {
    return false;
}

texas::MultiwayRolloutLeafContext make_context(RolloutFixture& fixture) {
    texas::MultiwayRolloutLeafContext context;
    context.provide_input = provide_input;
    context.input_context = &fixture;
    context.provide_actions = unused_actions;
    context.seeds = fixture.seeds.data();
    context.seed_count = fixture.seeds.size();
    context.scratch = &fixture.scratch;
    context.limits.max_exact_runouts = 1'000U;
    return context;
}

texas::MultiwayLeafEvaluationRequest make_cacheable_request(std::uint64_t public_state = 11U) {
    texas::MultiwayLeafEvaluationRequest request;
    request.traverser = 0;
    request.public_state = {public_state};
    request.continuation_actor = 0;
    request.future_bucket = 7U;
    request.action_abstraction_version = 3U;
    request.leaf_model_version = 5U;
    request.range_context_identity = 13U;
    request.private_context_identity = 17U;
    return request;
}

}  // namespace

TEST_CASE(multiway_rollout_leaf_exact_all_in_is_deterministic_for_all_profiles) {
    RolloutFixture fixture;
    const auto context = make_context(fixture);
    texas::MultiwayRolloutProfileResult first;
    texas::MultiwayRolloutProfileResult second;
    const texas::MultiwayLeafEvaluationRequest request = {nullptr, nullptr, 0};

    EXPECT_TRUE(texas::evaluate_multiway_rollout_profiles(request, context, &first));
    EXPECT_TRUE(texas::evaluate_multiway_rollout_profiles(request, context, &second));
    EXPECT_EQ(first.status, texas::MultiwayRolloutStatus::Complete);
    EXPECT_EQ(first.runout_mode, texas::MultiwayRolloutRunoutMode::Exact);
    EXPECT_EQ(first.exact_runouts, 2'970U);
    EXPECT_EQ(first.seed_count, static_cast<std::uint32_t>(fixture.seeds.size()));
    for (std::size_t profile = 0; profile < first.values.size(); ++profile) {
        EXPECT_TRUE(std::isfinite(first.values[profile]));
        EXPECT_NEAR(first.values[profile], second.values[profile], 1e-12);
        EXPECT_NEAR(first.values[profile], first.values[0], 1e-12);
    }
}

TEST_CASE(multiway_rollout_leaf_uses_finite_seeded_fallback_when_exact_runouts_are_capped) {
    RolloutFixture fixture;
    auto context = make_context(fixture);
    context.limits.max_exact_runouts = 1U;
    texas::MultiwayRolloutProfileResult result;

    EXPECT_TRUE(texas::evaluate_multiway_rollout_profiles({nullptr, nullptr, 0}, context, &result));
    EXPECT_EQ(result.status, texas::MultiwayRolloutStatus::CappedFallback);
    EXPECT_EQ(result.runout_mode, texas::MultiwayRolloutRunoutMode::Seeded);
    for (const auto value : result.values) EXPECT_TRUE(std::isfinite(value));
}

TEST_CASE(multiway_rollout_leaf_rejects_invalid_caller_contexts) {
    RolloutFixture fixture;
    auto context = make_context(fixture);
    texas::MultiwayRolloutProfileResult result;
    context.seed_count = 0U;
    EXPECT_TRUE(!texas::evaluate_multiway_rollout_profiles({nullptr, nullptr, 0}, context, &result));

    context = make_context(fixture);
    fixture.board[1] = fixture.board[0];
    EXPECT_TRUE(!texas::evaluate_multiway_rollout_profiles({nullptr, nullptr, 0}, context, &result));

    EXPECT_TRUE(!texas::make_multiway_rollout_leaf_evaluator(nullptr).valid());
}

TEST_CASE(multiway_rollout_leaf_adapter_retains_leaf_callback_compatibility) {
    RolloutFixture fixture;
    auto context = make_context(fixture);
    context.selected_policy = texas::MultiwayContinuationPolicyKind::RaiseBiased;
    const auto evaluator = texas::make_multiway_rollout_leaf_evaluator(&context);
    const auto value = evaluator({nullptr, nullptr, 0});
    EXPECT_TRUE(evaluator.valid());
    EXPECT_TRUE(std::isfinite(value));
}

TEST_CASE(multiway_rollout_leaf_cache_reuses_same_seed_context_and_records_diagnostics) {
    RolloutFixture fixture;
    auto context = make_context(fixture);
    texas::MultiwayContinuationCache cache(2U);
    texas::MultiwayContinuationDiagnostics diagnostics;
    context.cache = &cache;
    context.diagnostics = &diagnostics;
    context.selected_policy = texas::MultiwayContinuationPolicyKind::CallBiased;
    const auto request = make_cacheable_request();
    texas::MultiwayRolloutProfileResult first;
    texas::MultiwayRolloutProfileResult second;

    EXPECT_TRUE(texas::evaluate_multiway_rollout_profiles(request, context, &first));
    EXPECT_TRUE(texas::evaluate_multiway_rollout_profiles(request, context, &second));
    EXPECT_EQ(fixture.input_calls, std::size_t{1});
    EXPECT_EQ(cache.size(), std::size_t{1});
    EXPECT_EQ(diagnostics.cache_misses, std::uint64_t{1});
    EXPECT_EQ(diagnostics.cache_hits, std::uint64_t{1});
    EXPECT_EQ(diagnostics.leaf_count, std::uint64_t{2});
    EXPECT_EQ(diagnostics.invalid_count, std::uint64_t{0});
    EXPECT_EQ(diagnostics.sample_count, std::uint64_t{6});
    EXPECT_EQ(diagnostics.seed, fixture.seeds[0]);
    EXPECT_EQ(diagnostics.policy_mode, texas::MultiwayContinuationPolicyKind::Blueprint);
    EXPECT_EQ(diagnostics.runout_mode, texas::MultiwayRolloutRunoutMode::Exact);
    EXPECT_EQ(diagnostics.repeated_seed_pairs, std::uint64_t{0});
    for (std::size_t policy = 0; policy < first.values.size(); ++policy) {
        EXPECT_NEAR(first.values[policy], second.values[policy], 1e-12);
        EXPECT_NEAR(diagnostics.mean_policy_values[policy], first.values[policy], 1e-12);
    }
}

TEST_CASE(multiway_rollout_leaf_reports_different_seed_pair_variance) {
    RolloutFixture fixture;
    auto context = make_context(fixture);
    context.limits.max_exact_runouts = 1U;
    context.seed_count = 1U;
    texas::MultiwayContinuationCache cache(4U);
    texas::MultiwayContinuationDiagnostics diagnostics;
    context.cache = &cache;
    context.diagnostics = &diagnostics;
    const auto request = make_cacheable_request();
    texas::MultiwayRolloutProfileResult first;
    texas::MultiwayRolloutProfileResult second;

    context.seeds = &fixture.seeds[0];
    EXPECT_TRUE(texas::evaluate_multiway_rollout_profiles(request, context, &first));
    context.seeds = &fixture.seeds[1];
    EXPECT_TRUE(texas::evaluate_multiway_rollout_profiles(request, context, &second));

    EXPECT_EQ(fixture.input_calls, std::size_t{2});
    EXPECT_EQ(cache.size(), std::size_t{2});
    EXPECT_EQ(diagnostics.cache_misses, std::uint64_t{2});
    EXPECT_EQ(diagnostics.repeated_seed_pairs, std::uint64_t{1});
    EXPECT_EQ(diagnostics.capped_count, std::uint64_t{2});
    EXPECT_EQ(diagnostics.seed, fixture.seeds[1]);
    EXPECT_EQ(diagnostics.runout_mode, texas::MultiwayRolloutRunoutMode::Seeded);
    for (std::size_t policy = 0; policy < first.values.size(); ++policy) {
        const auto difference = second.values[policy] - first.values[policy];
        EXPECT_NEAR(diagnostics.repeated_seed_variance[policy],
                    0.5 * difference * difference, 1e-12);
    }
}

TEST_CASE(multiway_rollout_leaf_cache_separates_range_and_private_contexts) {
    RolloutFixture fixture;
    auto context = make_context(fixture);
    texas::MultiwayContinuationCache cache(3U);
    texas::MultiwayContinuationDiagnostics diagnostics;
    context.cache = &cache;
    context.diagnostics = &diagnostics;
    texas::MultiwayRolloutProfileResult result;
    auto request = make_cacheable_request();

    EXPECT_TRUE(texas::evaluate_multiway_rollout_profiles(request, context, &result));
    ++request.range_context_identity;
    EXPECT_TRUE(texas::evaluate_multiway_rollout_profiles(request, context, &result));
    ++request.private_context_identity;
    EXPECT_TRUE(texas::evaluate_multiway_rollout_profiles(request, context, &result));

    EXPECT_EQ(cache.size(), std::size_t{3});
    EXPECT_EQ(fixture.input_calls, std::size_t{3});
    EXPECT_EQ(diagnostics.cache_misses, std::uint64_t{3});
    EXPECT_EQ(diagnostics.cache_hits, std::uint64_t{0});
}

TEST_CASE(multiway_rollout_leaf_cache_stops_admission_at_byte_cap) {
    RolloutFixture fixture;
    auto context = make_context(fixture);
    texas::MultiwayContinuationCache cache(
        4U, texas::MultiwayContinuationCache::entry_bytes());
    texas::MultiwayContinuationDiagnostics diagnostics;
    context.cache = &cache;
    context.diagnostics = &diagnostics;
    texas::MultiwayRolloutProfileResult result;

    EXPECT_TRUE(texas::evaluate_multiway_rollout_profiles(
        make_cacheable_request(11U), context, &result));
    EXPECT_TRUE(texas::evaluate_multiway_rollout_profiles(
        make_cacheable_request(12U), context, &result));
    EXPECT_TRUE(texas::evaluate_multiway_rollout_profiles(
        make_cacheable_request(12U), context, &result));

    EXPECT_EQ(cache.capacity(), std::size_t{1});
    EXPECT_EQ(cache.size(), std::size_t{1});
    EXPECT_EQ(cache.memory_bytes(), texas::MultiwayContinuationCache::entry_bytes());
    EXPECT_EQ(fixture.input_calls, std::size_t{3});
    EXPECT_EQ(diagnostics.cache_hits, std::uint64_t{0});
    EXPECT_EQ(diagnostics.cache_misses, std::uint64_t{3});
    EXPECT_EQ(diagnostics.cache_admission_rejections, std::uint64_t{2});
}

TEST_CASE(multiway_rollout_leaf_diagnostics_count_invalid_uncached_contexts) {
    RolloutFixture fixture;
    auto context = make_context(fixture);
    texas::MultiwayContinuationDiagnostics diagnostics;
    texas::MultiwayRolloutProfileResult result;
    context.diagnostics = &diagnostics;
    context.seed_count = 0U;

    EXPECT_TRUE(!texas::evaluate_multiway_rollout_profiles(
        make_cacheable_request(), context, &result));
    EXPECT_EQ(diagnostics.leaf_count, std::uint64_t{1});
    EXPECT_EQ(diagnostics.invalid_count, std::uint64_t{1});
    EXPECT_EQ(diagnostics.sample_count, std::uint64_t{0});
}
