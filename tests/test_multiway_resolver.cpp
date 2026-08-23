#include "solver/multiway_bucket_artifact.hpp"
#include "solver/multiway_blueprint_config.hpp"
#include "solver/multiway_artifact.hpp"
#include "solver/multiway_public_builder.hpp"
#include "solver/multiway_resolver.hpp"
#include "solver/multiway_runtime_session.hpp"
#include "test_harness.hpp"

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <stdexcept>

namespace {

struct ResolverFixture {
    texas::MultiwayModelIdentity identity;
    texas::MultiwayPublicStateDescriptor root;
    texas::MultiwayBucketRegistry buckets;

    ResolverFixture()
        : identity(texas::make_multiway_model_identity(texas::MultiwayBlueprintConfig{})),
          root(make_root()),
          buckets(texas::build_multiway_baseline_bucket_registry(
              identity, {{texas::Street::Flop, {8U, 13U, 17U}}})) {}

    static texas::MultiwayPublicStateDescriptor make_root() {
        texas::MultiwayGameConfig config;
        config.starting_stacks = {2'000, 2'000, 2'000};
        config.initial_contributions = {100, 100, 100};
        config.initial_street_contributions = {0, 0, 0};
        config.first_player = 0;
        config.big_blind = 100;
        config.street = texas::Street::Flop;
        const auto state = texas::MultiwayState::initial(config);
        const auto menu = texas::MultiwayActionAbstraction().make_legal_actions(state.snapshot());
        return texas::MultiwayPublicBuilder::make_root(state.snapshot(), {8U, 13U, 17U}, menu);
    }

    texas::MultiwayResolverRequest request() const {
        texas::MultiwayResolverRequest request;
        request.blueprint_identity = identity;
        request.public_state = root;
        request.hero_seat = 0;
        request.hero_cards = {24U, 31U};
        request.hero_range = {{{24U, 31U}, 1.0}};
        request.deadline = std::chrono::steady_clock::now() + std::chrono::seconds(1);
        request.sampling_seed = 73U;
        return request;
    }

    texas::MultiwayResolver resolver() {
        texas::MultiwayResolverConfig config;
        config.buckets = &buckets;
        config.search_limits.max_batches = 2U;
        config.search_limits.trajectories_per_batch = 5U;
        config.search_mode = texas::MultiwayResolverSearchMode::FallbackOnly;
        return texas::MultiwayResolver(config);
    }
};

bool contains_action(
    const std::vector<texas::MultiwayResolverActionProbability>& policy,
    texas::MultiwayAction action,
    int target_street_contribution) {
    return std::any_of(policy.begin(), policy.end(), [action, target_street_contribution](const auto& entry) {
        return entry.action.action == action &&
            entry.action.target_street_contribution == target_street_contribution;
    });
}

bool is_legal_output(
    const texas::MultiwayResolverResult& result,
    const std::vector<texas::MultiwayActionDescriptor>& menu) {
    return result.has_sampled_action && std::find(menu.begin(), menu.end(), result.sampled_action) != menu.end();
}

texas::Value resolver_search_leaf(
    const texas::MultiwayLeafEvaluationRequest& request,
    const void*) noexcept {
    const auto seat = static_cast<std::size_t>(request.traverser);
    return static_cast<texas::Value>(
        request.betting->contributions[seat] - request.betting->current_bet);
}

texas::MultiwayResolverConfig search_config(const ResolverFixture& fixture) {
    texas::MultiwayResolverConfig config;
    config.buckets = &fixture.buckets;
    config.stable_root_cache = std::make_shared<texas::MultiwayStableRootPolicyCache>();
    config.search_limits.trajectories_per_batch = 2U;
    config.search_limits.max_batches = 1U;
    config.search_mode = texas::MultiwayResolverSearchMode::SearchActive;
    config.continuation_selector = std::make_shared<texas::MultiwayFixedContinuationSelector>(
        texas::MultiwayContinuationPolicyKind::Blueprint);
    config.search_limits.worker_count = 1U;
    config.search_limits.max_public_states = 32U;
    config.search_limits.max_sparse_rows = 16U;
    config.search_limits.max_sparse_values = 2'048U;
    config.search_limits.max_worker_delta_entries = 128U;
    static const texas::MultiwayLeafEvaluator evaluator = {resolver_search_leaf, nullptr};
    config.leaf_evaluator = &evaluator;
    return config;
}

texas::MultiwayVerifiedBlueprintArtifact verified_root_artifact(const ResolverFixture& fixture) {
    texas::MultiwayVerifiedBlueprintArtifact artifact;
    artifact.snapshot.identity = fixture.identity;
    artifact.snapshot.public_state = fixture.root.id;
    artifact.snapshot.infoset = {fixture.root.id, 0};
    artifact.snapshot.trajectories = 1U;
    artifact.snapshot.training.trajectories = 1U;
    artifact.snapshot.actions = {{fixture.root.legal_actions.front(), 65535U}};
    artifact.snapshot.validate();
    artifact.manifest.identity = fixture.identity;
    artifact.manifest.snapshot_hash = texas::MultiwayBlueprintArtifacts::snapshot_hash(artifact.snapshot);
    artifact.manifest.validate();
    return artifact;
}

void add_complete_search_ranges(texas::MultiwayResolverRequest* request) {
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

    EXPECT_EQ(result.diagnostics.status, texas::MultiwayResolverStatus::Solved);
    EXPECT_EQ(result.diagnostics.completed_batches, 0U);
    EXPECT_EQ(result.diagnostics.completed_trajectories, 0U);
    EXPECT_EQ(result.diagnostics.policy_provenance, texas::MultiwayPolicyProvenance::StaticLegalFallback);
    EXPECT_EQ(result.diagnostics.search_engine, texas::MultiwayResolverEngine::NoRuntimeSearch);
    EXPECT_EQ(
        result.diagnostics.search_engine_version,
        texas::MULTIWAY_NO_RUNTIME_SEARCH_ENGINE_VERSION);
    const auto log = texas::make_multiway_public_decision_log(fixture.request(), result, 1U);
    log.validate();
    EXPECT_EQ(log.search_engine, texas::MultiwayResolverEngine::NoRuntimeSearch);
    EXPECT_EQ(log.search_engine_version, texas::MULTIWAY_NO_RUNTIME_SEARCH_ENGINE_VERSION);
    EXPECT_TRUE(result.diagnostics.policy_normalized);
    EXPECT_TRUE(is_legal_output(result, result.policy.empty() ? std::vector<texas::MultiwayActionDescriptor>{} :
        fixture.root.legal_actions));
    double total = 0.0;
    for (const auto& entry : result.policy) total += entry.probability;
    EXPECT_NEAR(total, 1.0, 1e-12);
}

TEST_CASE(multiway_resolver_begins_a_request_local_runtime_session) {
    ResolverFixture fixture;
    auto request = fixture.request();
    add_complete_search_ranges(&request);
    auto resolver = fixture.resolver();

    const auto runtime = resolver.begin_runtime_session(request);
    EXPECT_TRUE(runtime != nullptr);
    EXPECT_EQ(runtime->root_revision(), 1U);
    EXPECT_EQ(runtime->round().root_metadata().public_state, fixture.root.id);
    EXPECT_EQ(runtime->round().belief(0).metadata().source, texas::MultiwayRangeBeliefSource::Supplied);
}

TEST_CASE(multiway_resolver_uses_the_same_range_contract_for_multiple_opponents) {
    ResolverFixture fixture;
    auto request = fixture.request();
    request.opponent_ranges.resize(2U);
    request.opponent_ranges[0].seat = 1;
    request.opponent_ranges[0].hands.push_back({{36U, 37U}, 1.0});
    request.opponent_ranges[1].seat = 2;
    request.opponent_ranges[1].hands.push_back({{40U, 41U}, 1.0});
    auto resolver = fixture.resolver();
    const auto result = resolver.resolve(request);
    EXPECT_EQ(result.diagnostics.admitted_range_entries, 3U);
    EXPECT_TRUE(is_legal_output(result, fixture.root.legal_actions));
}

TEST_CASE(multiway_resolver_preserves_an_exact_off_tree_root_action) {
    ResolverFixture fixture;
    const auto state = texas::MultiwayState::from_snapshot(fixture.root.betting);
    const auto menu = texas::MultiwayActionAbstraction::insert_exact_observed_action(
        state.snapshot(), fixture.root.legal_actions, texas::MultiwayAction::Bet, 650);
    fixture.root = texas::MultiwayPublicBuilder::make_root(state.snapshot(), {8U, 13U, 17U}, menu);
    auto resolver = fixture.resolver();
    const auto result = resolver.resolve(fixture.request());

    EXPECT_EQ(result.diagnostics.status, texas::MultiwayResolverStatus::Solved);
    EXPECT_TRUE(contains_action(result.policy, texas::MultiwayAction::Bet, 650));
    EXPECT_TRUE(is_legal_output(result, result.policy.empty() ? std::vector<texas::MultiwayActionDescriptor>{} : menu));
}

TEST_CASE(multiway_resolver_uses_a_legal_deadline_fallback) {
    ResolverFixture fixture;
    auto request = fixture.request();
    request.deadline = std::chrono::steady_clock::now() - std::chrono::milliseconds(1);
    auto resolver = fixture.resolver();
    const auto result = resolver.resolve(request);

    EXPECT_EQ(result.diagnostics.status, texas::MultiwayResolverStatus::DeadlineFallback);
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
    EXPECT_EQ(invalid.diagnostics.status, texas::MultiwayResolverStatus::InvalidRequest);
    EXPECT_TRUE(!invalid.has_sampled_action);

    auto missing_hero_range = fixture.request();
    missing_hero_range.hero_range.clear();
    const auto missing_range = resolver.resolve(missing_hero_range);
    EXPECT_EQ(missing_range.diagnostics.status, texas::MultiwayResolverStatus::InvalidRequest);

    texas::MultiwayResolver no_bucket;
    const auto missing_bucket = no_bucket.resolve(fixture.request());
    EXPECT_EQ(missing_bucket.diagnostics.status, texas::MultiwayResolverStatus::BucketUnavailable);
    EXPECT_TRUE(missing_bucket.diagnostics.used_static_fallback);
    EXPECT_TRUE(is_legal_output(missing_bucket, fixture.root.legal_actions));
}

TEST_CASE(multiway_resolver_never_exports_an_illegal_blueprint_action) {
    ResolverFixture fixture;
    auto blueprint = std::make_shared<texas::MultiwayVerifiedBlueprintArtifact>(verified_root_artifact(fixture));
    blueprint->snapshot.actions = {{{texas::MultiwayAction::Raise, 0U, 1'999, 91U}, 65535U}};
    blueprint->snapshot.validate();
    blueprint->manifest.snapshot_hash = texas::MultiwayBlueprintArtifacts::snapshot_hash(blueprint->snapshot);

    texas::MultiwayResolverConfig config;
    config.buckets = &fixture.buckets;
    config.verified_blueprint = std::move(blueprint);
    config.search_limits.max_batches = 1U;
    texas::MultiwayResolver resolver(config);
    const auto result = resolver.resolve(fixture.request());

    EXPECT_TRUE(is_legal_output(result, fixture.root.legal_actions));
    for (const auto& entry : result.policy) {
        EXPECT_TRUE(std::find(fixture.root.legal_actions.begin(), fixture.root.legal_actions.end(), entry.action) !=
            fixture.root.legal_actions.end());
    }
}

TEST_CASE(multiway_resolver_reports_static_provenance_and_safe_invalid_identity) {
    ResolverFixture fixture;
    auto resolver = fixture.resolver();
    const auto solved = resolver.resolve(fixture.request());

    EXPECT_EQ(solved.diagnostics.status, texas::MultiwayResolverStatus::Solved);
    EXPECT_EQ(
        solved.diagnostics.policy_provenance,
        texas::MultiwayPolicyProvenance::StaticLegalFallback);
    EXPECT_EQ(
        solved.diagnostics.search_engine,
        texas::MultiwayResolverEngine::NoRuntimeSearch);
    EXPECT_EQ(
        solved.diagnostics.search_engine_version,
        texas::MULTIWAY_NO_RUNTIME_SEARCH_ENGINE_VERSION);
    EXPECT_TRUE(solved.diagnostics.has_artifact_identity);
    EXPECT_EQ(solved.diagnostics.artifact_identity, fixture.identity);

    auto invalid_request = fixture.request();
    invalid_request.hero_cards[0] = invalid_request.public_state.board[0];
    const auto invalid = resolver.resolve(invalid_request);
    EXPECT_EQ(invalid.diagnostics.status, texas::MultiwayResolverStatus::InvalidRequest);
    EXPECT_TRUE(invalid.diagnostics.has_artifact_identity);
    EXPECT_EQ(invalid.diagnostics.artifact_identity, fixture.identity);
    EXPECT_TRUE(!invalid.has_sampled_action);
    EXPECT_TRUE(invalid.policy.empty());
}

TEST_CASE(multiway_resolver_reports_static_stable_and_blueprint_fallback_provenance) {
    ResolverFixture fixture;
    auto expired_request = fixture.request();
    expired_request.deadline = std::chrono::steady_clock::now() - std::chrono::milliseconds(1);

    const auto static_fallback = texas::MultiwayResolver().resolve(expired_request);
    EXPECT_EQ(static_fallback.diagnostics.status, texas::MultiwayResolverStatus::BucketUnavailable);
    EXPECT_EQ(
        static_fallback.diagnostics.policy_provenance,
        texas::MultiwayPolicyProvenance::StaticLegalFallback);
    EXPECT_TRUE(static_fallback.diagnostics.used_static_fallback);

    auto stable_request = fixture.request();
    add_complete_search_ranges(&stable_request);
    texas::MultiwayResolver stable_resolver(search_config(fixture));
    (void)stable_resolver.resolve(stable_request);
    const auto stable_fallback = stable_resolver.resolve(expired_request);
    EXPECT_EQ(stable_fallback.diagnostics.status, texas::MultiwayResolverStatus::DeadlineFallback);
    EXPECT_EQ(
        stable_fallback.diagnostics.policy_provenance,
        texas::MultiwayPolicyProvenance::StableRootFallback);
    EXPECT_TRUE(stable_fallback.diagnostics.used_latest_stable_root);

    auto uncached_config = search_config(fixture);
    uncached_config.stable_root_cache.reset();
    texas::MultiwayResolver uncached_resolver(uncached_config);
    (void)uncached_resolver.resolve(stable_request);
    const auto uncached_fallback = uncached_resolver.resolve(expired_request);
    EXPECT_TRUE(uncached_fallback.diagnostics.policy_provenance !=
        texas::MultiwayPolicyProvenance::StableRootFallback);

    auto blueprint = std::make_shared<texas::MultiwayVerifiedBlueprintArtifact>(verified_root_artifact(fixture));
    blueprint->snapshot.actions = {{fixture.root.legal_actions.front(), 65535U}};
    blueprint->snapshot.validate();
    blueprint->manifest.snapshot_hash = texas::MultiwayBlueprintArtifacts::snapshot_hash(blueprint->snapshot);
    texas::MultiwayResolverConfig blueprint_config;
    blueprint_config.buckets = &fixture.buckets;
    blueprint_config.verified_blueprint = std::move(blueprint);
    texas::MultiwayResolver blueprint_resolver(blueprint_config);
    const auto blueprint_fallback = blueprint_resolver.resolve(expired_request);
    EXPECT_EQ(blueprint_fallback.diagnostics.status, texas::MultiwayResolverStatus::DeadlineFallback);
    EXPECT_EQ(
        blueprint_fallback.diagnostics.policy_provenance,
        texas::MultiwayPolicyProvenance::BlueprintFallback);
    EXPECT_TRUE(blueprint_fallback.diagnostics.used_blueprint_fallback);
}

TEST_CASE(multiway_resolver_default_mode_runs_a_clean_root_search_when_configured) {
    ResolverFixture fixture;
    auto request = fixture.request();
    add_complete_search_ranges(&request);
    const auto artifact = verified_root_artifact(fixture);
    const auto full_blueprint = std::make_shared<texas::MultiwayBlueprintStore>(fixture.identity, std::vector<texas::MultiwayBlueprintRow>{});
    auto config = search_config(fixture);
    config.search_mode = texas::MultiwayResolverSearchMode::ReleaseDefault;
    config.verified_blueprint = std::make_shared<texas::MultiwayVerifiedBlueprintArtifact>(artifact);
    config.full_blueprint = full_blueprint;
    texas::MultiwayResolver resolver(config);

    const auto result = resolver.resolve(request);

    EXPECT_EQ(result.diagnostics.status, texas::MultiwayResolverStatus::Solved);
    EXPECT_EQ(result.diagnostics.policy_provenance, texas::MultiwayPolicyProvenance::RuntimeSearch);
    EXPECT_EQ(result.diagnostics.search_engine, texas::MultiwayResolverEngine::RootExternalSamplingMCCFR);
    EXPECT_EQ(result.diagnostics.search_engine_version, texas::MULTIWAY_ROOT_SEARCH_RESOLVER_ENGINE_VERSION);
    EXPECT_EQ(result.diagnostics.search_eligibility, texas::MultiwayResolverSearchEligibility::Eligible);
    EXPECT_EQ(result.diagnostics.completed_batches, 1U);
    EXPECT_EQ(result.diagnostics.completed_trajectories, 2U);
    EXPECT_TRUE(result.diagnostics.search_merged_delta_entries > 0U);
    EXPECT_EQ(result.diagnostics.search_first_trajectory_id, 0U);
    EXPECT_EQ(result.diagnostics.search_trajectory_count, 2U);
    EXPECT_EQ(result.diagnostics.search_root_revision, 1U);
    EXPECT_EQ(result.diagnostics.search_worker_count, 1U);
    EXPECT_TRUE(result.diagnostics.search_admitted_rows > 0U);
    EXPECT_TRUE(result.diagnostics.search_admitted_values > 0U);
    EXPECT_TRUE(result.diagnostics.search_schedule_fingerprint != 0U);
    EXPECT_TRUE(result.diagnostics.search_merged_stream_fingerprint != 0U);
    EXPECT_TRUE(result.diagnostics.search_bitwise_deterministic);
    EXPECT_TRUE(result.diagnostics.policy_normalized);
    EXPECT_TRUE(is_legal_output(result, fixture.root.legal_actions));
}

TEST_CASE(multiway_resolver_default_mode_uses_fallback_without_release_search_configuration) {
    ResolverFixture fixture;
    auto request = fixture.request();
    add_complete_search_ranges(&request);
    texas::MultiwayResolverConfig config;
    config.buckets = &fixture.buckets;
    texas::MultiwayResolver resolver(config);

    const auto result = resolver.resolve(request);

    EXPECT_EQ(result.diagnostics.status, texas::MultiwayResolverStatus::RejectedByBudget);
    EXPECT_EQ(result.diagnostics.policy_provenance, texas::MultiwayPolicyProvenance::StaticLegalFallback);
    EXPECT_EQ(result.diagnostics.search_eligibility, texas::MultiwayResolverSearchEligibility::NotRequested);
    EXPECT_TRUE(result.diagnostics.used_static_fallback);
    EXPECT_TRUE(is_legal_output(result, fixture.root.legal_actions));
}

TEST_CASE(multiway_resolver_search_mode_falls_back_without_complete_live_ranges) {
    ResolverFixture fixture;
    texas::MultiwayResolver resolver(search_config(fixture));

    const auto result = resolver.resolve(fixture.request());

    EXPECT_EQ(result.diagnostics.status, texas::MultiwayResolverStatus::RejectedByBudget);
    EXPECT_EQ(
        result.diagnostics.search_eligibility,
        texas::MultiwayResolverSearchEligibility::IncompleteRanges);
    EXPECT_TRUE(result.diagnostics.used_fallback);
    EXPECT_TRUE(is_legal_output(result, fixture.root.legal_actions));
}

TEST_CASE(multiway_resolver_active_search_respects_controlled_seat_eligibility) {
    ResolverFixture fixture;
    auto request = fixture.request();
    add_complete_search_ranges(&request);
    auto config = search_config(fixture);
    config.active_search_max_seats = 2U;
    texas::MultiwayResolver resolver(config);

    const auto result = resolver.resolve(request);

    EXPECT_EQ(result.diagnostics.status, texas::MultiwayResolverStatus::RejectedByBudget);
    EXPECT_EQ(result.diagnostics.search_eligibility, texas::MultiwayResolverSearchEligibility::SeatCount);
    EXPECT_TRUE(result.diagnostics.used_fallback);
    EXPECT_TRUE(is_legal_output(result, fixture.root.legal_actions));
}

TEST_CASE(multiway_resolver_rejects_invalid_active_search_eligibility_limits) {
    ResolverFixture fixture;
    auto config = search_config(fixture);
    config.active_search_min_seats = 4U;
    config.active_search_max_seats = 3U;

    EXPECT_THROW(texas::MultiwayResolver(config), std::invalid_argument);
}

TEST_CASE(multiway_resolver_search_budget_rejects_an_expired_request_before_batch_start) {
    ResolverFixture fixture;
    auto request = fixture.request();
    add_complete_search_ranges(&request);
    request.deadline = std::chrono::steady_clock::now() - std::chrono::milliseconds(1);
    texas::MultiwayResolver resolver(search_config(fixture));

    const auto result = resolver.resolve(request);

    EXPECT_EQ(result.diagnostics.status, texas::MultiwayResolverStatus::DeadlineFallback);
    EXPECT_TRUE(result.diagnostics.deadline_expired);
    EXPECT_EQ(result.diagnostics.completed_batches, 0U);
    EXPECT_EQ(result.diagnostics.completed_trajectories, 0U);
    EXPECT_TRUE(result.diagnostics.used_fallback);
    EXPECT_TRUE(is_legal_output(result, fixture.root.legal_actions));
}

TEST_CASE(multiway_resolver_rejects_a_root_search_without_a_clean_batch) {
    ResolverFixture fixture;
    auto request = fixture.request();
    add_complete_search_ranges(&request);
    auto config = search_config(fixture);
    config.search_limits.max_worker_delta_entries = 1U;
    texas::MultiwayResolver resolver(config);

    const auto result = resolver.resolve(request);

    EXPECT_EQ(result.diagnostics.status, texas::MultiwayResolverStatus::ResourceExhausted);
    EXPECT_TRUE(result.diagnostics.used_fallback);
    EXPECT_EQ(result.diagnostics.completed_batches, 0U);
    EXPECT_EQ(result.diagnostics.completed_trajectories, 0U);
    EXPECT_EQ(
        result.diagnostics.search_failure,
        texas::MultiwayResolverSearchFailure::NoCleanBatch);
    EXPECT_TRUE(is_legal_output(result, fixture.root.legal_actions));
}

TEST_CASE(multiway_resolver_rejects_memory_before_runtime_search_allocation) {
    ResolverFixture fixture;
    auto request = fixture.request();
    add_complete_search_ranges(&request);
    auto config = search_config(fixture);
    config.search_memory_budget = texas::MultiwayMemoryBudget{1U, 3U, 2U};
    texas::MultiwayResolver resolver(config);

    const auto result = resolver.resolve(request);

    EXPECT_EQ(result.diagnostics.status, texas::MultiwayResolverStatus::RejectedByBudget);
    EXPECT_EQ(result.diagnostics.search_memory_status, texas::MultiwayMemoryStatus::Rejected);
    EXPECT_EQ(result.diagnostics.search_memory_stage, texas::MultiwayMemoryAdmissionStage::None);
    EXPECT_EQ(
        result.diagnostics.search_failure,
        texas::MultiwayResolverSearchFailure::MemoryRejected);
    EXPECT_EQ(result.diagnostics.search_admitted_memory_bytes, 0U);
    EXPECT_TRUE(result.diagnostics.search_estimated_memory_bytes > 0U);
    EXPECT_EQ(result.diagnostics.completed_batches, 0U);
    EXPECT_TRUE(result.diagnostics.used_fallback);
}

TEST_CASE(multiway_runtime_session_rejects_memory_before_constructing_the_round) {
    ResolverFixture fixture;
    auto request = fixture.request();
    add_complete_search_ranges(&request);
    auto config = search_config(fixture);
    config.search_memory_budget = texas::MultiwayMemoryBudget{1U, 3U, 2U};
    texas::MultiwayResolver resolver(config);

    EXPECT_THROW(resolver.begin_runtime_session(request), std::length_error);
}

TEST_CASE(multiway_resolver_shadow_mode_reports_clean_search_comparison) {
    ResolverFixture fixture;
    auto request = fixture.request();
    add_complete_search_ranges(&request);
    auto config = search_config(fixture);
    config.search_mode = texas::MultiwayResolverSearchMode::SearchShadow;
    texas::MultiwayResolver resolver(config);

    const auto result = resolver.resolve(request);

    EXPECT_EQ(result.diagnostics.status, texas::MultiwayResolverStatus::Solved);
    EXPECT_EQ(
        result.diagnostics.policy_provenance,
        texas::MultiwayPolicyProvenance::StaticLegalFallback);
    EXPECT_EQ(result.diagnostics.search_engine, texas::MultiwayResolverEngine::NoRuntimeSearch);
    EXPECT_EQ(
        result.diagnostics.search_engine_version,
        texas::MULTIWAY_NO_RUNTIME_SEARCH_ENGINE_VERSION);
    EXPECT_EQ(result.diagnostics.search_eligibility, texas::MultiwayResolverSearchEligibility::Eligible);
    EXPECT_TRUE(result.diagnostics.shadow_search_completed);
    EXPECT_EQ(result.diagnostics.shadow_completed_batches, 1U);
    EXPECT_EQ(result.diagnostics.shadow_completed_trajectories, 2U);
    EXPECT_TRUE(result.diagnostics.shadow_search_merged_delta_entries > 0U);
    EXPECT_TRUE(result.diagnostics.shadow_search_elapsed_nanoseconds > 0U);
    EXPECT_TRUE(result.diagnostics.shadow_policy_l1_distance >= 0.0);
    EXPECT_TRUE(is_legal_output(result, fixture.root.legal_actions));
}
