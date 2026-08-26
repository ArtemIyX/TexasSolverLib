#include "solver/multiway_baseline.hpp"
#include "solver/multiway_bucket_artifact.hpp"
#include "solver/multiway_bucket_model.hpp"
#include "solver/multiway_blueprint_config.hpp"
#include "solver/multiway_public_builder.hpp"
#include "solver/multiway_continuation_selector.hpp"
#include "test_harness.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>
#include <memory>

namespace {

struct ResolverBaselineFixture {
    texas::MultiwayModelIdentity identity;
    texas::MultiwayPublicStateDescriptor root;
    texas::MultiwayBucketRegistry buckets;

    ResolverBaselineFixture()
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
        request.opponent_ranges = {
            {1, {{{36U, 37U}, 1.0}}},
            {2, {{{40U, 41U}, 1.0}}},
        };
        request.deadline = std::chrono::steady_clock::now() + std::chrono::seconds(1);
        request.sampling_seed = 73U;
        return request;
    }

    texas::MultiwayResolverConfig resolver_config() const {
        texas::MultiwayResolverConfig config;
        config.buckets = &buckets;
        config.search_mode = texas::MultiwayResolverSearchMode::SearchActive;
        config.continuation_selector = std::make_shared<texas::MultiwayFixedContinuationSelector>(
            texas::MultiwayContinuationPolicyKind::Blueprint);
        static const texas::MultiwayLeafEvaluator evaluator = {
            [](const texas::MultiwayLeafEvaluationRequest& request, const void*) noexcept -> texas::Value {
                const auto seat = static_cast<std::size_t>(request.traverser);
                return static_cast<texas::Value>(
                    request.betting->contributions[seat] - request.betting->current_bet);
            }, nullptr};
        config.leaf_evaluator = &evaluator;
        config.runtime_limits.solver.max_batches = 2U;
        config.runtime_limits.solver.trajectories_per_batch = 5U;
        return config;
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

constexpr std::uint8_t card(std::uint8_t rank, std::uint8_t suit) {
    return texas::card_to_int(rank, suit);
}

const std::vector<std::uint8_t> kTraversalBoard = {
    card(2U, 0U), card(7U, 1U), card(9U, 2U), card(4U, 3U), card(6U, 0U),
};

std::vector<std::uint8_t> compact_bucket_board(const std::vector<std::uint8_t>& hunl_board) {
    std::vector<std::uint8_t> compact;
    compact.reserve(hunl_board.size());
    for (const auto card : hunl_board) compact.push_back(card);
    return compact;
}

std::vector<std::uint32_t> one_bucket_assignments(const std::vector<std::uint8_t>& compact_board) {
    std::vector<std::uint32_t> assignments(texas::MULTIWAY_HOLE_COMBINATION_COUNT, 0U);
    for (std::uint8_t first = 0U; first < 52U; ++first) {
        for (std::uint8_t second = static_cast<std::uint8_t>(first + 1U); second < 52U; ++second) {
            for (const auto board_card : compact_board) {
                if (first == board_card || second == board_card) {
                    assignments[texas::MultiwayBucketTable::hole_index({first, second})] =
                        texas::MULTIWAY_INVALID_BUCKET;
                }
            }
        }
    }
    return assignments;
}

texas::MultiwayRootSnapshot traversal_root(const texas::MultiwayActionAbstraction& abstraction) {
    texas::MultiwayGameConfig game;
    game.starting_stacks = {1'000, 1'000, 1'000};
    game.initial_contributions = {0, 0, 0};
    game.initial_street_contributions = {0, 0, 0};
    game.first_player = 0;
    game.big_blind = 100;
    game.street = texas::Street::River;
    const auto betting = texas::MultiwayState::initial(game).snapshot();

    texas::MultiwayRootSnapshot root;
    root.public_state = texas::MultiwayPublicBuilder::make_root(
        betting, kTraversalBoard, abstraction.make_legal_actions(betting));
    root.root_infoset = {root.public_state.id, 0};
    root.root_bucket = 0U;
    root.seat_order = {0, 1, 2};
    root.next_street_first_seat = 0;
    root.odd_chip_first_seat = 0;
    root.private_ranges.board = kTraversalBoard;
    root.private_ranges.ranges = {
        {{{card(14U, 0U), card(13U, 0U)}, 1.0}},
        {{{card(12U, 0U), card(11U, 0U)}, 1.0}},
        {{{card(10U, 0U), card(8U, 0U)}, 1.0}},
    };
    root.action_abstraction_version = 1U;
    root.leaf_model_version = 1U;
    return root;
}

texas::MultiwaySolverLimits traversal_limits(std::size_t max_sparse_rows) {
    texas::MultiwaySolverLimits limits;
    limits.worker_count = 1U;
    limits.trajectories_per_batch = 3U;
    limits.max_public_states = 128U;
    limits.max_sparse_rows = max_sparse_rows;
    limits.max_sparse_values = 256U;
    limits.max_worker_delta_entries = 128U;
    return limits;
}

texas::MultiwayCFRConfig traversal_cfr() {
    texas::MultiwayCFRConfig cfr;
    cfr.player_count = 3U;
    return cfr;
}

texas::MultiwayBucketRegistry traversal_buckets() {
    texas::MultiwayBlueprintConfig config;
    config.player_count = 3U;
    const auto compact_board = compact_bucket_board(kTraversalBoard);
    return texas::MultiwayBucketRegistry({texas::MultiwayBucketTable(
        texas::make_multiway_model_identity(config), texas::Street::River,
        compact_board, 1U, one_bucket_assignments(compact_board))});
}

texas::Value traversal_leaf(
    const texas::MultiwayLeafEvaluationRequest& request,
    const void*) noexcept {
    const auto seat = static_cast<std::size_t>(request.traverser);
    return static_cast<texas::Value>(
        request.betting->contributions[seat] - request.betting->current_bet);
}

struct TraversalBaselineFixture {
    explicit TraversalBaselineFixture(std::size_t max_sparse_rows = 8U)
        : root(traversal_root(abstraction)),
          request(root, traversal_cfr(), traversal_limits(max_sparse_rows)),
          coordinator(request),
          buckets(traversal_buckets()),
          evaluator{traversal_leaf, nullptr},
          traversal(coordinator, request.root(), abstraction, buckets, &evaluator, 1U),
          runner(
              traversal,
              coordinator,
              1U,
              128U,
              texas::MultiwaySearchProfileMode::Checkpoints) {}

    texas::MultiwayActionAbstraction abstraction;
    texas::MultiwayRootSnapshot root;
    texas::MultiwaySolveRequest request;
    texas::MultiwaySolverCoordinator coordinator;
    texas::MultiwayBucketRegistry buckets;
    texas::MultiwayLeafEvaluator evaluator;
    texas::MultiwayRootExternalSamplingTraversal traversal;
    texas::MultiwayRootBatchRunner runner;
};

}  // namespace

TEST_CASE(multiway_baseline_fixture_harness_covers_required_resolver_categories) {
    ResolverBaselineFixture fixture;
    const texas::MultiwayResolverBaselineFixtureHarness harness(fixture.resolver_config());

    const auto valid = harness.run({
        texas::MultiwayBaselineFixtureKind::Valid, fixture.request(), {0U, 0U, 4'242U}});
    EXPECT_EQ(valid.fixture, texas::MultiwayBaselineFixtureKind::Valid);
    EXPECT_EQ(valid.status, texas::MultiwayResolverStatus::Solved);
    EXPECT_EQ(valid.fallback, texas::MultiwayResolverFallbackKind::None);
    EXPECT_EQ(valid.completed_batches, 2U);
    EXPECT_EQ(valid.completed_trajectories, 10U);
    EXPECT_EQ(valid.measurements.observed_memory_bytes, 4'242U);

    auto invalid_request = fixture.request();
    invalid_request.hero_cards[0] = invalid_request.public_state.board[0];
    const auto invalid = harness.run({texas::MultiwayBaselineFixtureKind::Invalid, invalid_request, {}});
    EXPECT_EQ(invalid.fixture, texas::MultiwayBaselineFixtureKind::Invalid);
    EXPECT_EQ(invalid.status, texas::MultiwayResolverStatus::InvalidRequest);
    EXPECT_TRUE(!invalid.has_sampled_action);

    auto off_tree_request = fixture.request();
    const auto state = texas::MultiwayState::from_snapshot(fixture.root.betting);
    const auto off_tree_menu = texas::MultiwayActionAbstraction::insert_exact_observed_action(
        state.snapshot(), fixture.root.legal_actions, texas::MultiwayAction::Bet, 650);
    off_tree_request.public_state = texas::MultiwayPublicBuilder::make_root(
        state.snapshot(), {8U, 13U, 17U}, off_tree_menu);
    const auto off_tree = harness.run({texas::MultiwayBaselineFixtureKind::OffTree, off_tree_request, {}});
    EXPECT_EQ(off_tree.fixture, texas::MultiwayBaselineFixtureKind::OffTree);
    EXPECT_EQ(off_tree.status, texas::MultiwayResolverStatus::Solved);
    EXPECT_TRUE(contains_action(off_tree.policy, texas::MultiwayAction::Bet, 650));

    const auto no_artifact = texas::MultiwayResolverBaselineFixtureHarness().run({
        texas::MultiwayBaselineFixtureKind::NoArtifact, fixture.request(), {}});
    EXPECT_EQ(no_artifact.fixture, texas::MultiwayBaselineFixtureKind::NoArtifact);
    EXPECT_EQ(no_artifact.status, texas::MultiwayResolverStatus::BucketUnavailable);
    EXPECT_EQ(no_artifact.fallback, texas::MultiwayResolverFallbackKind::StaticLegal);

    auto expired_request = fixture.request();
    expired_request.deadline = std::chrono::steady_clock::now() - std::chrono::milliseconds(1);
    const auto deadline = harness.run({
        texas::MultiwayBaselineFixtureKind::DeadlineExhausted, expired_request, {}});
    EXPECT_EQ(deadline.fixture, texas::MultiwayBaselineFixtureKind::DeadlineExhausted);
    EXPECT_EQ(deadline.status, texas::MultiwayResolverStatus::DeadlineFallback);
    EXPECT_EQ(deadline.fallback, texas::MultiwayResolverFallbackKind::StaticLegal);
    EXPECT_TRUE(deadline.deadline_expired);
}

TEST_CASE(multiway_baseline_harness_isolates_stable_root_and_excludes_measurements_from_serialization) {
    ResolverBaselineFixture fixture;
    const texas::MultiwayResolverBaselineFixtureHarness harness(fixture.resolver_config());
    const auto solved = harness.run({texas::MultiwayBaselineFixtureKind::Valid, fixture.request(), {}});

    auto expired_request = fixture.request();
    expired_request.deadline = std::chrono::steady_clock::now() - std::chrono::milliseconds(1);
    const auto after_solved = harness.run({
        texas::MultiwayBaselineFixtureKind::DeadlineExhausted, expired_request, {101U, 0U, 4'242U}});
    const auto fresh = texas::MultiwayResolverBaselineFixtureHarness(fixture.resolver_config()).run({
        texas::MultiwayBaselineFixtureKind::DeadlineExhausted, expired_request, {202U, 0U, 8'484U}});

    EXPECT_EQ(solved.status, texas::MultiwayResolverStatus::Solved);
    EXPECT_EQ(after_solved.fallback, texas::MultiwayResolverFallbackKind::StaticLegal);
    EXPECT_TRUE(texas::equivalent_multiway_resolver_baseline(after_solved, fresh));
    EXPECT_EQ(
        texas::serialize_multiway_resolver_baseline(after_solved),
        texas::serialize_multiway_resolver_baseline(fresh));

    const auto serialized = texas::serialize_multiway_resolver_baseline(after_solved);
    EXPECT_TRUE(serialized.find("elapsed_nanoseconds=") == std::string::npos);
    EXPECT_TRUE(serialized.find("observed_memory_bytes=") == std::string::npos);
    EXPECT_TRUE(serialized.find("hero_cards=") == std::string::npos);
    EXPECT_TRUE(serialized.find("opponent_ranges=") == std::string::npos);
    EXPECT_TRUE(serialized.find("sampling_seed=") == std::string::npos);
}

TEST_CASE(multiway_traversal_baseline_reports_per_run_deltas_and_repeatable_output) {
    TraversalBaselineFixture fixture;
    const auto first = texas::record_multiway_traversal_baseline(
        texas::MultiwayBaselineFixtureKind::Valid,
        fixture.runner,
        fixture.coordinator,
        0U,
        3U,
        0x5eedU,
        1.0,
        {11U, 0U, 101U});
    const auto second = texas::record_multiway_traversal_baseline(
        texas::MultiwayBaselineFixtureKind::Valid,
        fixture.runner,
        fixture.coordinator,
        0U,
        3U,
        0x5eedU,
        1.0,
        {22U, 0U, 202U});
    TraversalBaselineFixture repeat_fixture;
    const auto repeat = texas::record_multiway_traversal_baseline(
        texas::MultiwayBaselineFixtureKind::Valid,
        repeat_fixture.runner,
        repeat_fixture.coordinator,
        0U,
        3U,
        0x5eedU,
        1.0,
        {33U, 0U, 303U});

    EXPECT_EQ(first.batch.trajectories_attempted, 3U);
    EXPECT_EQ(first.batch.trajectories_accepted, 3U);
    EXPECT_EQ(first.worker_delta_entries_merged, first.batch.delta_entries_merged);
    EXPECT_EQ(second.worker_delta_entries_merged, second.batch.delta_entries_merged);
    EXPECT_TRUE(texas::equivalent_multiway_traversal_baseline(first, repeat));
    EXPECT_EQ(
        texas::serialize_multiway_traversal_baseline(first),
        texas::serialize_multiway_traversal_baseline(repeat));
    EXPECT_EQ(second.measurements.observed_memory_bytes, 202U);
    EXPECT_TRUE(first.search_profile.profiled());
    EXPECT_EQ(
        first.search_profile.checkpoint(texas::MultiwaySearchProfileStage::PrivateDealSampling).calls,
        3U);
    EXPECT_TRUE(
        first.search_profile.checkpoint(texas::MultiwaySearchProfileStage::PublicGraphAdmission).calls > 0U);
    EXPECT_TRUE(
        first.search_profile.checkpoint(texas::MultiwaySearchProfileStage::RowLookup).calls > 0U);
    EXPECT_EQ(
        first.search_profile.checkpoint(texas::MultiwaySearchProfileStage::DeltaMerge).calls,
        1U);
}

TEST_CASE(multiway_search_profile_ranks_checkpoint_time_without_text_keys) {
    texas::MultiwaySearchProfile profile(texas::MultiwaySearchProfileMode::Checkpoints);
    profile.add(texas::MultiwaySearchProfileStage::RowLookup, 20U, 2U);
    profile.add(texas::MultiwaySearchProfileStage::DeltaMerge, 90U, 1U);
    profile.add(texas::MultiwaySearchProfileStage::ContinuationLeaf, 40U, 3U);

    const auto snapshot = profile.snapshot();
    const auto ranking = texas::rank_multiway_search_profile(snapshot);
    EXPECT_TRUE(snapshot.profiled());
    EXPECT_EQ(ranking[0].stage, texas::MultiwaySearchProfileStage::DeltaMerge);
    EXPECT_EQ(ranking[1].stage, texas::MultiwaySearchProfileStage::ContinuationLeaf);
    EXPECT_EQ(ranking[2].stage, texas::MultiwaySearchProfileStage::RowLookup);
    EXPECT_EQ(snapshot.checkpoint(texas::MultiwaySearchProfileStage::RowLookup).calls, 2U);
}

TEST_CASE(multiway_traversal_baseline_marks_the_max_row_fixture_and_reuses_its_row) {
    TraversalBaselineFixture fixture(1U);
    const auto baseline = texas::record_multiway_traversal_baseline(
        texas::MultiwayBaselineFixtureKind::MaxRows,
        fixture.runner,
        fixture.coordinator,
        0U,
        1U,
        0x5eedU);

    EXPECT_EQ(baseline.fixture, texas::MultiwayBaselineFixtureKind::MaxRows);
    EXPECT_EQ(baseline.sparse_rows_admitted, 1U);
    const auto repeat = texas::record_multiway_traversal_baseline(
        texas::MultiwayBaselineFixtureKind::MaxRows,
        fixture.runner,
        fixture.coordinator,
        1U,
        1U,
        0x5eedU);
    EXPECT_EQ(repeat.sparse_rows_admitted, 0U);
    EXPECT_EQ(repeat.batch.trajectories_accepted, 1U);
}

TEST_CASE(multiway_baseline_records_environment_metrics_without_affecting_deterministic_output) {
    ResolverBaselineFixture fixture;
    const texas::MultiwayResolverBaselineFixtureHarness harness(fixture.resolver_config());
    const auto report = harness.run({texas::MultiwayBaselineFixtureKind::Valid, fixture.request(), {}});

    EXPECT_TRUE(report.measurements.elapsed_nanoseconds > 0U);
    EXPECT_TRUE(!report.measurements.allocation_bytes_available);
    EXPECT_EQ(report.measurements.allocated_bytes, 0U);
    EXPECT_EQ(
        report.measurements.peak_resident_memory_available,
        report.measurements.peak_resident_memory_bytes != 0U);

    auto environmental_difference = report;
    environmental_difference.measurements.elapsed_nanoseconds = 1U;
    environmental_difference.measurements.process_cpu_nanoseconds = 2U;
    environmental_difference.measurements.observed_memory_bytes = 3U;
    environmental_difference.measurements.peak_resident_memory_bytes = 4U;
    environmental_difference.measurements.peak_resident_memory_available = true;
    environmental_difference.measurements.allocated_bytes = 5U;
    environmental_difference.measurements.allocation_bytes_available = true;
    EXPECT_TRUE(texas::equivalent_multiway_resolver_baseline(report, environmental_difference));
    EXPECT_EQ(
        texas::serialize_multiway_resolver_baseline(report),
        texas::serialize_multiway_resolver_baseline(environmental_difference));

    const auto serialized = texas::serialize_multiway_resolver_baseline(report);
    EXPECT_TRUE(serialized.find("process_cpu_nanoseconds=") == std::string::npos);
    EXPECT_TRUE(serialized.find("peak_resident_memory_bytes=") == std::string::npos);
    EXPECT_TRUE(serialized.find("allocated_bytes=") == std::string::npos);
}
