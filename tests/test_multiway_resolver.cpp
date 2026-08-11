#include "solver/multiway_bucket_artifact.hpp"
#include "solver/multiway_blueprint_config.hpp"
#include "solver/multiway_public_builder.hpp"
#include "solver/multiway_resolver.hpp"
#include "test_harness.hpp"

#include <algorithm>
#include <chrono>
#include <cstddef>

namespace {

struct ResolverFixture {
    core::MultiwayModelIdentity identity;
    core::MultiwayPublicStateDescriptor root;
    core::MultiwayBucketRegistry buckets;

    ResolverFixture()
        : identity(core::make_multiway_model_identity(core::MultiwayBlueprintConfig{})),
          root(make_root()),
          buckets(core::build_multiway_baseline_bucket_registry(
              identity, {{core::Street::Flop, {0U, 5U, 9U}}})) {}

    static core::MultiwayPublicStateDescriptor make_root() {
        core::MultiwayGameConfig config;
        config.starting_stacks = {2'000, 2'000, 2'000};
        config.initial_contributions = {100, 100, 100};
        config.initial_street_contributions = {0, 0, 0};
        config.first_player = 0;
        config.big_blind = 100;
        config.street = core::Street::Flop;
        const auto state = core::MultiwayState::initial(config);
        const auto menu = core::MultiwayActionAbstraction().make_legal_actions(state.snapshot(), 91U);
        return core::MultiwayPublicBuilder::make_root(state.snapshot(), {8U, 13U, 17U}, menu);
    }

    core::MultiwayResolverRequest request() const {
        core::MultiwayResolverRequest request;
        request.blueprint_identity = identity;
        request.public_state = root;
        request.hero_seat = 0;
        request.hero_cards = {24U, 31U};
        request.deadline = std::chrono::steady_clock::now() + std::chrono::seconds(1);
        request.sampling_seed = 73U;
        return request;
    }

    core::MultiwayResolver resolver() {
        core::MultiwayResolverConfig config;
        config.buckets = &buckets;
        config.max_batches = 2U;
        config.trajectories_per_batch = 5U;
        return core::MultiwayResolver(config);
    }
};

bool contains_action(
    const std::vector<core::MultiwayResolverActionProbability>& policy,
    core::MultiwayAction action,
    int target_street_contribution) {
    return std::any_of(policy.begin(), policy.end(), [action, target_street_contribution](const auto& entry) {
        return entry.action.action == action &&
            entry.action.target_street_contribution == target_street_contribution;
    });
}

bool is_legal_output(
    const core::MultiwayResolverResult& result,
    const std::vector<core::MultiwayActionDescriptor>& menu) {
    return result.has_sampled_action && std::find(menu.begin(), menu.end(), result.sampled_action) != menu.end();
}

core::Value resolver_search_leaf(
    const core::MultiwayLeafEvaluationRequest& request,
    const void*) noexcept {
    const auto seat = static_cast<std::size_t>(request.traverser);
    return static_cast<core::Value>(
        request.betting->contributions[seat] - request.betting->current_bet);
}

core::MultiwayResolverConfig search_config(const ResolverFixture& fixture) {
    core::MultiwayResolverConfig config;
    config.buckets = &fixture.buckets;
    config.trajectories_per_batch = 2U;
    config.max_batches = 1U;
    config.search_mode = core::MultiwayResolverSearchMode::SearchActive;
    config.search_limits.worker_count = 1U;
    config.search_limits.trajectories_per_batch = config.trajectories_per_batch;
    config.search_limits.max_public_states = 32U;
    config.search_limits.max_sparse_rows = 16U;
    config.search_limits.max_sparse_values = 2'048U;
    config.search_limits.max_worker_delta_entries = 128U;
    static const core::MultiwayLeafEvaluator evaluator = {resolver_search_leaf, nullptr};
    config.leaf_evaluator = &evaluator;
    return config;
}

void add_complete_search_ranges(core::MultiwayResolverRequest* request) {
    request->opponent_ranges = {
        {1, {{{36U, 37U}, 1.0}}},
        {2, {{{40U, 41U}, 1.0}}},
    };
}

}  // namespace

TEST_CASE(multiway_resolver_returns_a_normalized_legal_deadline_safe_decision) {
    ResolverFixture fixture;
    auto resolver = fixture.resolver();
    const auto result = resolver.resolve(fixture.request());

    EXPECT_EQ(result.diagnostics.status, core::MultiwayResolverStatus::Solved);
    EXPECT_EQ(result.diagnostics.completed_batches, 2U);
    EXPECT_EQ(result.diagnostics.completed_trajectories, 10U);
    EXPECT_TRUE(result.diagnostics.policy_normalized);
    EXPECT_TRUE(is_legal_output(result, result.policy.empty() ? std::vector<core::MultiwayActionDescriptor>{} :
        fixture.root.legal_actions));
    double total = 0.0;
    for (const auto& entry : result.policy) total += entry.probability;
    EXPECT_NEAR(total, 1.0, 1e-12);
}

TEST_CASE(multiway_resolver_distinguishes_anonymous_and_blockers_only_ranges) {
    ResolverFixture fixture;
    auto request = fixture.request();
    request.opponent_ranges.resize(2U);
    request.opponent_ranges[0].seat = 1;
    request.opponent_ranges[0].hands.push_back({{36U, 37U}, 1.0});
    request.opponent_ranges[1].seat = 2;
    request.opponent_ranges[1].hands.push_back({{40U, 41U}, 1.0});
    auto resolver = fixture.resolver();
    const auto anonymous = resolver.resolve(request);
    request.inference_mode = core::MultiwayInferenceMode::BlockersOnly;
    const auto blockers_only = resolver.resolve(request);

    EXPECT_TRUE(anonymous.diagnostics.anonymous_ranges_merged);
    EXPECT_TRUE(!blockers_only.diagnostics.anonymous_ranges_merged);
    EXPECT_EQ(anonymous.diagnostics.admitted_range_entries, 2U);
    EXPECT_EQ(blockers_only.diagnostics.admitted_range_entries, 2U);
    EXPECT_TRUE(is_legal_output(anonymous, fixture.root.legal_actions));
    EXPECT_TRUE(is_legal_output(blockers_only, fixture.root.legal_actions));
}

TEST_CASE(multiway_resolver_preserves_an_exact_off_tree_root_action) {
    ResolverFixture fixture;
    const auto state = core::MultiwayState::from_snapshot(fixture.root.betting);
    const auto menu = core::MultiwayActionAbstraction::insert_exact_observed_action(
        state.snapshot(), fixture.root.legal_actions, core::MultiwayAction::Bet, 650, 91U);
    fixture.root = core::MultiwayPublicBuilder::make_root(state.snapshot(), {8U, 13U, 17U}, menu);
    auto resolver = fixture.resolver();
    const auto result = resolver.resolve(fixture.request());

    EXPECT_EQ(result.diagnostics.status, core::MultiwayResolverStatus::Solved);
    EXPECT_TRUE(contains_action(result.policy, core::MultiwayAction::Bet, 650));
    EXPECT_TRUE(is_legal_output(result, result.policy.empty() ? std::vector<core::MultiwayActionDescriptor>{} : menu));
}

TEST_CASE(multiway_resolver_uses_a_legal_deadline_fallback) {
    ResolverFixture fixture;
    auto request = fixture.request();
    request.deadline = std::chrono::steady_clock::now() - std::chrono::milliseconds(1);
    auto resolver = fixture.resolver();
    const auto result = resolver.resolve(request);

    EXPECT_EQ(result.diagnostics.status, core::MultiwayResolverStatus::DeadlineFallback);
    EXPECT_TRUE(result.diagnostics.deadline_expired);
    EXPECT_TRUE(result.diagnostics.used_fallback);
    EXPECT_TRUE(is_legal_output(result, fixture.root.legal_actions));
}

TEST_CASE(multiway_resolver_rejects_malformed_requests_and_missing_buckets) {
    ResolverFixture fixture;
    auto malformed = fixture.request();
    malformed.hero_cards[0] = malformed.public_state.board[0];
    auto resolver = fixture.resolver();
    const auto invalid = resolver.resolve(malformed);
    EXPECT_EQ(invalid.diagnostics.status, core::MultiwayResolverStatus::InvalidRequest);
    EXPECT_TRUE(!invalid.has_sampled_action);

    core::MultiwayResolver no_bucket;
    const auto missing_bucket = no_bucket.resolve(fixture.request());
    EXPECT_EQ(missing_bucket.diagnostics.status, core::MultiwayResolverStatus::BucketUnavailable);
    EXPECT_TRUE(missing_bucket.diagnostics.used_static_fallback);
    EXPECT_TRUE(is_legal_output(missing_bucket, fixture.root.legal_actions));
}

TEST_CASE(multiway_resolver_never_exports_an_illegal_blueprint_action) {
    ResolverFixture fixture;
    core::MultiwayBlueprintSnapshot blueprint;
    blueprint.identity = fixture.identity;
    blueprint.public_state = fixture.root.id;
    blueprint.infoset = {fixture.root.id, 0};
    blueprint.trajectories = 1U;
    blueprint.training.trajectories = 1U;
    blueprint.actions = {{{core::MultiwayAction::Raise, 0U, 1'999, 91U}, 65535U}};
    blueprint.validate();

    core::MultiwayResolverConfig config;
    config.buckets = &fixture.buckets;
    config.blueprint = &blueprint;
    config.max_batches = 1U;
    core::MultiwayResolver resolver(config);
    const auto result = resolver.resolve(fixture.request());

    EXPECT_TRUE(is_legal_output(result, fixture.root.legal_actions));
    for (const auto& entry : result.policy) {
        EXPECT_TRUE(std::find(fixture.root.legal_actions.begin(), fixture.root.legal_actions.end(), entry.action) !=
            fixture.root.legal_actions.end());
    }
}

TEST_CASE(multiway_resolver_reports_provenance_engine_and_safe_invalid_identity) {
    ResolverFixture fixture;
    auto resolver = fixture.resolver();
    const auto solved = resolver.resolve(fixture.request());

    EXPECT_EQ(solved.diagnostics.status, core::MultiwayResolverStatus::Solved);
    EXPECT_EQ(
        solved.diagnostics.policy_provenance,
        core::MultiwayPolicyProvenance::LegacyDeterministicAdjustment);
    EXPECT_EQ(
        solved.diagnostics.search_engine,
        core::MultiwayResolverEngine::LegacyDeterministicAdjustment);
    EXPECT_EQ(
        solved.diagnostics.search_engine_version,
        core::MULTIWAY_LEGACY_RESOLVER_ENGINE_VERSION);
    EXPECT_TRUE(solved.diagnostics.has_artifact_identity);
    EXPECT_EQ(solved.diagnostics.artifact_identity, fixture.identity);

    auto invalid_request = fixture.request();
    invalid_request.hero_cards[0] = invalid_request.public_state.board[0];
    const auto invalid = resolver.resolve(invalid_request);
    EXPECT_EQ(invalid.diagnostics.status, core::MultiwayResolverStatus::InvalidRequest);
    EXPECT_TRUE(invalid.diagnostics.has_artifact_identity);
    EXPECT_EQ(invalid.diagnostics.artifact_identity, fixture.identity);
    EXPECT_TRUE(!invalid.has_sampled_action);
    EXPECT_TRUE(invalid.policy.empty());
}

TEST_CASE(multiway_resolver_reports_static_stable_and_blueprint_fallback_provenance) {
    ResolverFixture fixture;
    auto expired_request = fixture.request();
    expired_request.deadline = std::chrono::steady_clock::now() - std::chrono::milliseconds(1);

    const auto static_fallback = core::MultiwayResolver().resolve(expired_request);
    EXPECT_EQ(static_fallback.diagnostics.status, core::MultiwayResolverStatus::BucketUnavailable);
    EXPECT_EQ(
        static_fallback.diagnostics.policy_provenance,
        core::MultiwayPolicyProvenance::StaticLegalFallback);
    EXPECT_TRUE(static_fallback.diagnostics.used_static_fallback);

    auto stable_resolver = fixture.resolver();
    (void)stable_resolver.resolve(fixture.request());
    const auto stable_fallback = stable_resolver.resolve(expired_request);
    EXPECT_EQ(stable_fallback.diagnostics.status, core::MultiwayResolverStatus::DeadlineFallback);
    EXPECT_EQ(
        stable_fallback.diagnostics.policy_provenance,
        core::MultiwayPolicyProvenance::StableRootFallback);
    EXPECT_TRUE(stable_fallback.diagnostics.used_latest_stable_root);

    core::MultiwayBlueprintSnapshot blueprint;
    blueprint.identity = fixture.identity;
    blueprint.public_state = fixture.root.id;
    blueprint.infoset = {fixture.root.id, 0};
    blueprint.trajectories = 1U;
    blueprint.training.trajectories = 1U;
    blueprint.actions = {{fixture.root.legal_actions.front(), 65535U}};
    blueprint.validate();
    core::MultiwayResolverConfig blueprint_config;
    blueprint_config.buckets = &fixture.buckets;
    blueprint_config.blueprint = &blueprint;
    core::MultiwayResolver blueprint_resolver(blueprint_config);
    const auto blueprint_fallback = blueprint_resolver.resolve(expired_request);
    EXPECT_EQ(blueprint_fallback.diagnostics.status, core::MultiwayResolverStatus::DeadlineFallback);
    EXPECT_EQ(
        blueprint_fallback.diagnostics.policy_provenance,
        core::MultiwayPolicyProvenance::BlueprintFallback);
    EXPECT_TRUE(blueprint_fallback.diagnostics.used_blueprint_fallback);
}

TEST_CASE(multiway_resolver_runs_a_clean_root_search_when_enabled) {
    ResolverFixture fixture;
    auto request = fixture.request();
    add_complete_search_ranges(&request);
    core::MultiwayResolver resolver(search_config(fixture));

    const auto result = resolver.resolve(request);

    EXPECT_EQ(result.diagnostics.status, core::MultiwayResolverStatus::Solved);
    EXPECT_EQ(result.diagnostics.policy_provenance, core::MultiwayPolicyProvenance::RuntimeSearch);
    EXPECT_EQ(result.diagnostics.search_engine, core::MultiwayResolverEngine::RootExternalSamplingMCCFR);
    EXPECT_EQ(result.diagnostics.search_engine_version, core::MULTIWAY_ROOT_SEARCH_RESOLVER_ENGINE_VERSION);
    EXPECT_EQ(result.diagnostics.completed_batches, 1U);
    EXPECT_EQ(result.diagnostics.completed_trajectories, 2U);
    EXPECT_TRUE(result.diagnostics.policy_normalized);
    EXPECT_TRUE(is_legal_output(result, fixture.root.legal_actions));
}

TEST_CASE(multiway_resolver_search_mode_falls_back_without_complete_live_ranges) {
    ResolverFixture fixture;
    core::MultiwayResolver resolver(search_config(fixture));

    const auto result = resolver.resolve(fixture.request());

    EXPECT_EQ(result.diagnostics.status, core::MultiwayResolverStatus::RejectedByBudget);
    EXPECT_TRUE(result.diagnostics.used_fallback);
    EXPECT_TRUE(is_legal_output(result, fixture.root.legal_actions));
}

TEST_CASE(multiway_resolver_rejects_a_root_search_without_a_clean_batch) {
    ResolverFixture fixture;
    auto request = fixture.request();
    add_complete_search_ranges(&request);
    auto config = search_config(fixture);
    config.search_limits.max_worker_delta_entries = 1U;
    core::MultiwayResolver resolver(config);

    const auto result = resolver.resolve(request);

    EXPECT_EQ(result.diagnostics.status, core::MultiwayResolverStatus::ResourceExhausted);
    EXPECT_TRUE(result.diagnostics.used_fallback);
    EXPECT_EQ(result.diagnostics.completed_batches, 0U);
    EXPECT_EQ(result.diagnostics.completed_trajectories, 0U);
    EXPECT_TRUE(is_legal_output(result, fixture.root.legal_actions));
}

TEST_CASE(multiway_resolver_shadow_mode_reports_clean_search_comparison) {
    ResolverFixture fixture;
    auto request = fixture.request();
    add_complete_search_ranges(&request);
    auto config = search_config(fixture);
    config.search_mode = core::MultiwayResolverSearchMode::SearchShadow;
    core::MultiwayResolver resolver(config);

    const auto result = resolver.resolve(request);

    EXPECT_EQ(result.diagnostics.status, core::MultiwayResolverStatus::Solved);
    EXPECT_EQ(
        result.diagnostics.policy_provenance,
        core::MultiwayPolicyProvenance::LegacyDeterministicAdjustment);
    EXPECT_TRUE(result.diagnostics.shadow_search_completed);
    EXPECT_EQ(result.diagnostics.shadow_completed_batches, 1U);
    EXPECT_EQ(result.diagnostics.shadow_completed_trajectories, 2U);
    EXPECT_TRUE(result.diagnostics.shadow_policy_l1_distance >= 0.0);
    EXPECT_TRUE(is_legal_output(result, fixture.root.legal_actions));
}
