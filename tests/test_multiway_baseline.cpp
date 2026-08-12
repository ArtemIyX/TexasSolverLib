#include "solver/multiway_baseline.hpp"
#include "solver/multiway_bucket_artifact.hpp"
#include "solver/multiway_bucket_model.hpp"
#include "solver/multiway_blueprint_config.hpp"
#include "solver/multiway_public_builder.hpp"
#include "test_harness.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

struct ResolverBaselineFixture {
    core::MultiwayModelIdentity identity;
    core::MultiwayPublicStateDescriptor root;
    core::MultiwayBucketRegistry buckets;

    ResolverBaselineFixture()
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

    core::MultiwayResolverConfig resolver_config() const {
        core::MultiwayResolverConfig config;
        config.buckets = &buckets;
        config.max_batches = 2U;
        config.trajectories_per_batch = 5U;
        return config;
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

constexpr std::uint8_t card(std::uint8_t rank, std::uint8_t suit) {
    return core::card_to_int(rank, suit);
}

const std::vector<std::uint8_t> kTraversalBoard = {
    card(2U, 0U), card(7U, 1U), card(9U, 2U), card(4U, 3U), card(6U, 0U),
};

std::vector<std::uint8_t> compact_bucket_board(const std::vector<std::uint8_t>& hunl_board) {
    std::vector<std::uint8_t> compact;
    compact.reserve(hunl_board.size());
    for (const auto card : hunl_board) compact.push_back(card - core::HUNL_CARD_FIRST);
    return compact;
}

std::vector<std::uint32_t> one_bucket_assignments(const std::vector<std::uint8_t>& compact_board) {
    std::vector<std::uint32_t> assignments(core::MULTIWAY_HOLE_COMBINATION_COUNT, 0U);
    for (std::uint8_t first = 0U; first < 52U; ++first) {
        for (std::uint8_t second = static_cast<std::uint8_t>(first + 1U); second < 52U; ++second) {
            for (const auto board_card : compact_board) {
                if (first == board_card || second == board_card) {
                    assignments[core::MultiwayBucketTable::hole_index({first, second})] =
                        core::MULTIWAY_INVALID_BUCKET;
                }
            }
        }
    }
    return assignments;
}

core::MultiwayRootSnapshot traversal_root(const core::MultiwayActionAbstraction& abstraction) {
    core::MultiwayGameConfig game;
    game.starting_stacks = {1'000, 1'000, 1'000};
    game.initial_contributions = {0, 0, 0};
    game.initial_street_contributions = {0, 0, 0};
    game.first_player = 0;
    game.big_blind = 100;
    game.street = core::Street::River;
    const auto betting = core::MultiwayState::initial(game).snapshot();

    core::MultiwayRootSnapshot root;
    root.public_state = core::MultiwayPublicBuilder::make_root(
        betting, kTraversalBoard, abstraction.make_legal_actions(betting, 77U));
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

core::MultiwaySolverLimits traversal_limits(std::size_t max_sparse_rows) {
    core::MultiwaySolverLimits limits;
    limits.worker_count = 1U;
    limits.trajectories_per_batch = 3U;
    limits.max_public_states = 128U;
    limits.max_sparse_rows = max_sparse_rows;
    limits.max_sparse_values = 256U;
    limits.max_worker_delta_entries = 128U;
    return limits;
}

core::MultiwayCFRConfig traversal_cfr() {
    core::MultiwayCFRConfig cfr;
    cfr.player_count = 3U;
    return cfr;
}

core::MultiwayBucketRegistry traversal_buckets() {
    core::MultiwayBlueprintConfig config;
    config.player_count = 3U;
    const auto compact_board = compact_bucket_board(kTraversalBoard);
    return core::MultiwayBucketRegistry({core::MultiwayBucketTable(
        core::make_multiway_model_identity(config), core::Street::River,
        compact_board, 1U, one_bucket_assignments(compact_board))});
}

core::Value traversal_leaf(
    const core::MultiwayLeafEvaluationRequest& request,
    const void*) noexcept {
    const auto seat = static_cast<std::size_t>(request.traverser);
    return static_cast<core::Value>(
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
              core::MultiwaySearchProfileMode::Checkpoints) {}

    core::MultiwayActionAbstraction abstraction;
    core::MultiwayRootSnapshot root;
    core::MultiwaySolveRequest request;
    core::MultiwaySolverCoordinator coordinator;
    core::MultiwayBucketRegistry buckets;
    core::MultiwayLeafEvaluator evaluator;
    core::MultiwayRootExternalSamplingTraversal traversal;
    core::MultiwayRootBatchRunner runner;
};

}  // namespace

TEST_CASE(multiway_baseline_fixture_harness_covers_required_resolver_categories) {
    ResolverBaselineFixture fixture;
    const core::MultiwayResolverBaselineFixtureHarness harness(fixture.resolver_config());

    const auto valid = harness.run({
        core::MultiwayBaselineFixtureKind::Valid, fixture.request(), {0U, 0U, 4'242U}});
    EXPECT_EQ(valid.fixture, core::MultiwayBaselineFixtureKind::Valid);
    EXPECT_EQ(valid.status, core::MultiwayResolverStatus::Solved);
    EXPECT_EQ(valid.fallback, core::MultiwayResolverFallbackKind::None);
    EXPECT_EQ(valid.completed_batches, 2U);
    EXPECT_EQ(valid.completed_trajectories, 10U);
    EXPECT_EQ(valid.measurements.observed_memory_bytes, 4'242U);

    auto invalid_request = fixture.request();
    invalid_request.hero_cards[0] = invalid_request.public_state.board[0];
    const auto invalid = harness.run({core::MultiwayBaselineFixtureKind::Invalid, invalid_request, {}});
    EXPECT_EQ(invalid.fixture, core::MultiwayBaselineFixtureKind::Invalid);
    EXPECT_EQ(invalid.status, core::MultiwayResolverStatus::InvalidRequest);
    EXPECT_TRUE(!invalid.has_sampled_action);

    auto off_tree_request = fixture.request();
    const auto state = core::MultiwayState::from_snapshot(fixture.root.betting);
    const auto off_tree_menu = core::MultiwayActionAbstraction::insert_exact_observed_action(
        state.snapshot(), fixture.root.legal_actions, core::MultiwayAction::Bet, 650, 91U);
    off_tree_request.public_state = core::MultiwayPublicBuilder::make_root(
        state.snapshot(), {8U, 13U, 17U}, off_tree_menu);
    const auto off_tree = harness.run({core::MultiwayBaselineFixtureKind::OffTree, off_tree_request, {}});
    EXPECT_EQ(off_tree.fixture, core::MultiwayBaselineFixtureKind::OffTree);
    EXPECT_EQ(off_tree.status, core::MultiwayResolverStatus::Solved);
    EXPECT_TRUE(contains_action(off_tree.policy, core::MultiwayAction::Bet, 650));

    const auto no_artifact = core::MultiwayResolverBaselineFixtureHarness().run({
        core::MultiwayBaselineFixtureKind::NoArtifact, fixture.request(), {}});
    EXPECT_EQ(no_artifact.fixture, core::MultiwayBaselineFixtureKind::NoArtifact);
    EXPECT_EQ(no_artifact.status, core::MultiwayResolverStatus::BucketUnavailable);
    EXPECT_EQ(no_artifact.fallback, core::MultiwayResolverFallbackKind::StaticLegal);

    auto expired_request = fixture.request();
    expired_request.deadline = std::chrono::steady_clock::now() - std::chrono::milliseconds(1);
    const auto deadline = harness.run({
        core::MultiwayBaselineFixtureKind::DeadlineExhausted, expired_request, {}});
    EXPECT_EQ(deadline.fixture, core::MultiwayBaselineFixtureKind::DeadlineExhausted);
    EXPECT_EQ(deadline.status, core::MultiwayResolverStatus::DeadlineFallback);
    EXPECT_EQ(deadline.fallback, core::MultiwayResolverFallbackKind::StaticLegal);
    EXPECT_TRUE(deadline.deadline_expired);
}

TEST_CASE(multiway_baseline_harness_isolates_stable_root_and_excludes_measurements_from_serialization) {
    ResolverBaselineFixture fixture;
    const core::MultiwayResolverBaselineFixtureHarness harness(fixture.resolver_config());
    const auto solved = harness.run({core::MultiwayBaselineFixtureKind::Valid, fixture.request(), {}});

    auto expired_request = fixture.request();
    expired_request.deadline = std::chrono::steady_clock::now() - std::chrono::milliseconds(1);
    const auto after_solved = harness.run({
        core::MultiwayBaselineFixtureKind::DeadlineExhausted, expired_request, {101U, 0U, 4'242U}});
    const auto fresh = core::MultiwayResolverBaselineFixtureHarness(fixture.resolver_config()).run({
        core::MultiwayBaselineFixtureKind::DeadlineExhausted, expired_request, {202U, 0U, 8'484U}});

    EXPECT_EQ(solved.status, core::MultiwayResolverStatus::Solved);
    EXPECT_EQ(after_solved.fallback, core::MultiwayResolverFallbackKind::StaticLegal);
    EXPECT_TRUE(core::equivalent_multiway_resolver_baseline(after_solved, fresh));
    EXPECT_EQ(
        core::serialize_multiway_resolver_baseline(after_solved),
        core::serialize_multiway_resolver_baseline(fresh));

    const auto serialized = core::serialize_multiway_resolver_baseline(after_solved);
    EXPECT_TRUE(serialized.find("elapsed_nanoseconds=") == std::string::npos);
    EXPECT_TRUE(serialized.find("observed_memory_bytes=") == std::string::npos);
    EXPECT_TRUE(serialized.find("hero_cards=") == std::string::npos);
    EXPECT_TRUE(serialized.find("opponent_ranges=") == std::string::npos);
    EXPECT_TRUE(serialized.find("sampling_seed=") == std::string::npos);
}

TEST_CASE(multiway_traversal_baseline_reports_per_run_deltas_and_repeatable_output) {
    TraversalBaselineFixture fixture;
    const auto first = core::record_multiway_traversal_baseline(
        core::MultiwayBaselineFixtureKind::Valid,
        fixture.runner,
        fixture.coordinator,
        0U,
        3U,
        0x5eedU,
        1.0,
        {11U, 0U, 101U});
    const auto second = core::record_multiway_traversal_baseline(
        core::MultiwayBaselineFixtureKind::Valid,
        fixture.runner,
        fixture.coordinator,
        0U,
        3U,
        0x5eedU,
        1.0,
        {22U, 0U, 202U});
    TraversalBaselineFixture repeat_fixture;
    const auto repeat = core::record_multiway_traversal_baseline(
        core::MultiwayBaselineFixtureKind::Valid,
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
    EXPECT_TRUE(core::equivalent_multiway_traversal_baseline(first, repeat));
    EXPECT_EQ(
        core::serialize_multiway_traversal_baseline(first),
        core::serialize_multiway_traversal_baseline(repeat));
    EXPECT_EQ(second.measurements.observed_memory_bytes, 202U);
    EXPECT_TRUE(first.search_profile.profiled());
    EXPECT_EQ(
        first.search_profile.checkpoint(core::MultiwaySearchProfileStage::PrivateDealSampling).calls,
        3U);
    EXPECT_TRUE(
        first.search_profile.checkpoint(core::MultiwaySearchProfileStage::PublicGraphAdmission).calls > 0U);
    EXPECT_TRUE(
        first.search_profile.checkpoint(core::MultiwaySearchProfileStage::RowLookup).calls > 0U);
    EXPECT_EQ(
        first.search_profile.checkpoint(core::MultiwaySearchProfileStage::DeltaMerge).calls,
        1U);
}

TEST_CASE(multiway_search_profile_ranks_checkpoint_time_without_text_keys) {
    core::MultiwaySearchProfile profile(core::MultiwaySearchProfileMode::Checkpoints);
    profile.add(core::MultiwaySearchProfileStage::RowLookup, 20U, 2U);
    profile.add(core::MultiwaySearchProfileStage::DeltaMerge, 90U, 1U);
    profile.add(core::MultiwaySearchProfileStage::ContinuationLeaf, 40U, 3U);

    const auto snapshot = profile.snapshot();
    const auto ranking = core::rank_multiway_search_profile(snapshot);
    EXPECT_TRUE(snapshot.profiled());
    EXPECT_EQ(ranking[0].stage, core::MultiwaySearchProfileStage::DeltaMerge);
    EXPECT_EQ(ranking[1].stage, core::MultiwaySearchProfileStage::ContinuationLeaf);
    EXPECT_EQ(ranking[2].stage, core::MultiwaySearchProfileStage::RowLookup);
    EXPECT_EQ(snapshot.checkpoint(core::MultiwaySearchProfileStage::RowLookup).calls, 2U);
}

TEST_CASE(multiway_traversal_baseline_marks_the_max_row_fixture_and_reuses_its_row) {
    TraversalBaselineFixture fixture(1U);
    const auto baseline = core::record_multiway_traversal_baseline(
        core::MultiwayBaselineFixtureKind::MaxRows,
        fixture.runner,
        fixture.coordinator,
        0U,
        1U,
        0x5eedU);

    EXPECT_EQ(baseline.fixture, core::MultiwayBaselineFixtureKind::MaxRows);
    EXPECT_EQ(baseline.sparse_rows_admitted, 1U);
    const auto repeat = core::record_multiway_traversal_baseline(
        core::MultiwayBaselineFixtureKind::MaxRows,
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
    const core::MultiwayResolverBaselineFixtureHarness harness(fixture.resolver_config());
    const auto report = harness.run({core::MultiwayBaselineFixtureKind::Valid, fixture.request(), {}});

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
    EXPECT_TRUE(core::equivalent_multiway_resolver_baseline(report, environmental_difference));
    EXPECT_EQ(
        core::serialize_multiway_resolver_baseline(report),
        core::serialize_multiway_resolver_baseline(environmental_difference));

    const auto serialized = core::serialize_multiway_resolver_baseline(report);
    EXPECT_TRUE(serialized.find("process_cpu_nanoseconds=") == std::string::npos);
    EXPECT_TRUE(serialized.find("peak_resident_memory_bytes=") == std::string::npos);
    EXPECT_TRUE(serialized.find("allocated_bytes=") == std::string::npos);
}
