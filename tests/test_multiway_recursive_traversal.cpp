#include "solver/multiway_action_abstraction.hpp"
#include "solver/multiway_blueprint_config.hpp"
#include "solver/multiway_bucket_model.hpp"
#include "solver/multiway_continuation_selector.hpp"
#include "solver/multiway_model_identity.hpp"
#include "solver/multiway_public_builder.hpp"
#include "solver/multiway_traversal.hpp"
#include "test_harness.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <utility>
#include <vector>

namespace {

constexpr std::uint8_t card(std::uint8_t rank, std::uint8_t suit) {
    return texas::card_to_int(rank, suit);
}

const std::vector<std::uint8_t> kBoard = {
    card(2, 0), card(7, 1), card(9, 2), card(4, 3), card(6, 0),
};

std::vector<std::uint8_t> compact_bucket_board(const std::vector<std::uint8_t>& hunl_board) {
    std::vector<std::uint8_t> compact;
    compact.reserve(hunl_board.size());
    for (const auto card : hunl_board) compact.push_back(card);
    return compact;
}

std::vector<std::uint32_t> one_bucket_assignments(const std::vector<std::uint8_t>& compact_board) {
    std::vector<std::uint32_t> assignments(texas::MULTIWAY_HOLE_COMBINATION_COUNT, 0U);
    for (std::uint8_t first = 0; first < 52U; ++first) {
        for (std::uint8_t second = static_cast<std::uint8_t>(first + 1U); second < 52U; ++second) {
            const std::array<std::uint8_t, 2> hole = {first, second};
            for (const auto board_card : compact_board) {
                if (first == board_card || second == board_card) {
                    assignments[texas::MultiwayBucketTable::hole_index(hole)] =
                        texas::MULTIWAY_INVALID_BUCKET;
                }
            }
        }
    }
    return assignments;
}

texas::MultiwayRootSnapshot make_root(
    const texas::MultiwayActionAbstraction& abstraction,
    bool use_exact_private_hand = false) {
    texas::MultiwayGameConfig game;
    game.starting_stacks = {1000, 1000, 1000};
    game.initial_contributions = {0, 0, 0};
    game.initial_street_contributions = {0, 0, 0};
    game.first_player = 0;
    game.big_blind = 100;
    game.street = texas::Street::River;
    const auto betting = texas::MultiwayState::initial(game).snapshot();

    texas::MultiwayRootSnapshot root;
    root.public_state = texas::MultiwayPublicBuilder::make_root(
        betting, kBoard, abstraction.make_legal_actions(betting));
    root.root_infoset = {root.public_state.id, 0};
    root.root_bucket = 0U;
    root.root_uses_exact_private_hand = use_exact_private_hand;
    if (use_exact_private_hand) {
        root.root_bucket = static_cast<std::uint32_t>(
            texas::MultiwayBucketTable::hole_index({card(14, 0), card(13, 0)}));
    }
    root.seat_order = {0, 1, 2};
    root.next_street_first_seat = 0;
    root.odd_chip_first_seat = 0;
    root.private_ranges.board = kBoard;
    root.private_ranges.ranges = {
        {{{card(14, 0), card(13, 0)}, 1.0}},
        {{{card(12, 0), card(11, 0)}, 1.0}},
        {{{card(10, 0), card(8, 0)}, 1.0}},
    };
    root.action_abstraction_version = 1U;
    root.leaf_model_version = 1U;
    return root;
}

texas::MultiwaySolverLimits make_limits(
    std::size_t delta_capacity = 128U,
    std::uint32_t worker_count = 1U) {
    texas::MultiwaySolverLimits limits;
    limits.worker_count = worker_count;
    limits.trajectories_per_batch = 8U;
    limits.max_public_states = 128U;
    limits.max_sparse_rows = 32U;
    limits.max_sparse_values = 256U;
    limits.max_worker_delta_entries = delta_capacity;
    return limits;
}

texas::MultiwayCFRConfig make_cfr() {
    texas::MultiwayCFRConfig cfr;
    cfr.player_count = 3U;
    return cfr;
}

texas::MultiwayBucketRegistry make_buckets() {
    texas::MultiwayBlueprintConfig config;
    config.player_count = 3U;
    const auto compact_board = compact_bucket_board(kBoard);
    return texas::MultiwayBucketRegistry({texas::MultiwayBucketTable(
        texas::make_multiway_model_identity(config), texas::Street::River,
        compact_board, 1U, one_bucket_assignments(compact_board))});
}

struct LeafProbe {
    std::size_t calls = 0U;
    std::vector<texas::PlayerId> actors;
    std::vector<int> current_bets;
    std::vector<texas::MultiwayContinuationPolicyKind> policies;
    std::size_t requests_with_private_context = 0U;
};

texas::Value probe_leaf(
    const texas::MultiwayLeafEvaluationRequest& request,
    const void* context) noexcept {
    auto& probe = *const_cast<LeafProbe*>(static_cast<const LeafProbe*>(context));
    ++probe.calls;
    probe.actors.push_back(request.betting->current_player);
    probe.current_bets.push_back(request.betting->current_bet);
    probe.policies.push_back(request.continuation_policy);
    if (request.private_deal != nullptr && request.terminal_adapter != nullptr &&
        request.player_reaches != nullptr && request.player_count > 0U &&
        request.public_state.value != 0U && request.continuation_actor >= 0 &&
        request.action_abstraction_version != 0U && request.leaf_model_version != 0U) {
        ++probe.requests_with_private_context;
    }
    const auto traverser = static_cast<std::size_t>(request.traverser);
    return static_cast<texas::Value>(request.betting->contributions[traverser] -
                                    request.betting->current_bet);
}

texas::Value deterministic_leaf(
    const texas::MultiwayLeafEvaluationRequest& request,
    const void*) noexcept {
    const auto traverser = static_cast<std::size_t>(request.traverser);
    return static_cast<texas::Value>(
        request.betting->contributions[traverser] - request.betting->current_bet);
}

texas::Value policy_value_leaf(
    const texas::MultiwayLeafEvaluationRequest& request,
    const void*) noexcept {
    return static_cast<texas::Value>(request.continuation_policy);
}

struct ParallelRunnerFixture {
    explicit ParallelRunnerFixture(std::uint32_t worker_count)
        : root(make_root(abstraction)),
          request(root, make_cfr(), make_limits(1024U, worker_count)),
          coordinator(request),
          buckets(make_buckets()),
          evaluator{deterministic_leaf, nullptr},
          traversal(coordinator, request.root(), abstraction, buckets, &evaluator, 1U),
          runner(traversal, coordinator, worker_count, 1024U) {}

    texas::MultiwayActionAbstraction abstraction;
    texas::MultiwayRootSnapshot root;
    texas::MultiwaySolveRequest request;
    texas::MultiwaySolverCoordinator coordinator;
    texas::MultiwayBucketRegistry buckets;
    texas::MultiwayLeafEvaluator evaluator;
    texas::MultiwayRootExternalSamplingTraversal traversal;
    texas::MultiwayRootBatchRunner runner;
};

struct TraversalFixture {
    explicit TraversalFixture(
        std::uint32_t max_depth,
        std::size_t delta_capacity = 128U,
        bool use_exact_private_hand = false)
        : root(make_root(abstraction, use_exact_private_hand)),
          request(root, make_cfr(), make_limits(delta_capacity)),
          coordinator(request),
          buckets(make_buckets()),
          evaluator{probe_leaf, &probe},
          traversal(coordinator, request.root(), abstraction, buckets, &evaluator, max_depth) {}

    texas::MultiwayActionAbstraction abstraction;
    texas::MultiwayRootSnapshot root;
    texas::MultiwaySolveRequest request;
    texas::MultiwaySolverCoordinator coordinator;
    texas::MultiwayBucketRegistry buckets;
    LeafProbe probe;
    texas::MultiwayLeafEvaluator evaluator;
    texas::MultiwayRootExternalSamplingTraversal traversal;
};

void expect_same_delta(
    const texas::MultiwayWorkerDelta& left,
    const texas::MultiwayWorkerDelta& right) {
    EXPECT_EQ(left.infoset, right.infoset);
    EXPECT_EQ(left.bucket, right.bucket);
    EXPECT_EQ(left.action, right.action);
    EXPECT_NEAR(left.regret, right.regret, 1e-12);
    EXPECT_NEAR(left.strategy_sum, right.strategy_sum, 1e-12);
    EXPECT_EQ(left.trajectory_id, right.trajectory_id);
}

}  // namespace

TEST_CASE(multiway_recursive_traversal_rejects_unsupported_depth_and_invalid_seat) {
    texas::MultiwayActionAbstraction abstraction;
    const auto root = make_root(abstraction);
    const texas::MultiwaySolveRequest request(root, make_cfr(), make_limits());
    texas::MultiwaySolverCoordinator coordinator(request);
    const auto buckets = make_buckets();
    LeafProbe probe;
    const texas::MultiwayLeafEvaluator evaluator = {probe_leaf, &probe};

    EXPECT_THROW(
        texas::MultiwayRootExternalSamplingTraversal(
            coordinator, request.root(), abstraction, buckets, &evaluator, 0U),
        std::invalid_argument);
    EXPECT_THROW(
        texas::MultiwayRootExternalSamplingTraversal(
            coordinator, request.root(), abstraction, buckets, &evaluator, 65U),
        std::invalid_argument);

    texas::MultiwayRootExternalSamplingTraversal traversal(
        coordinator, request.root(), abstraction, buckets, &evaluator, 1U);
    texas::MultiwayWorkerDeltaStream stream(0U, 128U);
    EXPECT_THROW(traversal.run(-1, 1U, 7U, stream), std::invalid_argument);
    EXPECT_THROW(traversal.run(3, 1U, 7U, stream), std::invalid_argument);
}

TEST_CASE(multiway_recursive_traversal_accepts_a_non_root_opponent_traverser) {
    TraversalFixture fixture(2U);
    texas::MultiwayWorkerDeltaStream stream(0U, 128U);

    EXPECT_TRUE(fixture.traversal.run(1, 1U, 7U, stream));
    EXPECT_TRUE(stream.size() > 0U);
    for (const auto& delta : stream.deltas()) {
        EXPECT_EQ(delta.infoset.seat, 1);
        EXPECT_EQ(delta.trajectory_id, 1U);
    }
}

TEST_CASE(multiway_recursive_traversal_depth_one_cuts_off_at_typed_leaves) {
    TraversalFixture fixture(1U);
    texas::MultiwayWorkerDeltaStream stream(0U, 128U);

    EXPECT_TRUE(fixture.traversal.run(0, 10U, 123U, stream));
    EXPECT_EQ(fixture.probe.calls, fixture.request.root().public_state.legal_actions.size());
    EXPECT_EQ(fixture.coordinator.storage().row_count(), std::size_t{1});
    EXPECT_EQ(
        fixture.coordinator.diagnostics().public_states_admitted,
        1U + fixture.request.root().public_state.legal_actions.size());
    EXPECT_EQ(stream.size(), fixture.request.root().public_state.legal_actions.size());
}

TEST_CASE(multiway_recursive_traversal_selects_a_public_information_set_continuation_policy) {
    TraversalFixture fixture(1U);
    const texas::MultiwayFixedContinuationSelector selector(
        texas::MultiwayContinuationPolicyKind::CallBiased);
    fixture.traversal = texas::MultiwayRootExternalSamplingTraversal(
        fixture.coordinator, fixture.request.root(), fixture.abstraction, fixture.buckets,
        &fixture.evaluator, 1U, 0U, nullptr, &selector);
    texas::MultiwayWorkerDeltaStream stream(0U, 128U);

    EXPECT_TRUE(fixture.traversal.run(0, 14U, 0x55U, stream));
    EXPECT_EQ(fixture.probe.policies.size(),
        fixture.request.root().public_state.legal_actions.size() *
            texas::MULTIWAY_FIXED_CONTINUATION_POLICIES.size());
    EXPECT_EQ(fixture.probe.requests_with_private_context, fixture.probe.policies.size());
    for (std::size_t policy = 0U;
         policy < texas::MULTIWAY_FIXED_CONTINUATION_POLICIES.size(); ++policy) {
        std::size_t count = 0U;
        for (const auto observed : fixture.probe.policies) {
            if (observed == texas::MULTIWAY_FIXED_CONTINUATION_POLICIES[policy]) ++count;
        }
        EXPECT_EQ(count, fixture.request.root().public_state.legal_actions.size());
    }
}

TEST_CASE(multiway_recursive_traversal_mixes_and_learns_all_continuation_policies) {
    TraversalFixture fixture(1U);
    fixture.evaluator = {policy_value_leaf, nullptr};
    texas::MultiwayFixedContinuationSelector selector(
        texas::MultiwayContinuationPolicyKind::Blueprint);
    fixture.traversal = texas::MultiwayRootExternalSamplingTraversal(
        fixture.coordinator, fixture.request.root(), fixture.abstraction, fixture.buckets,
        &fixture.evaluator, 1U, 0U, nullptr, &selector);
    texas::MultiwayWorkerDeltaStream stream(0U, 128U);

    EXPECT_TRUE(fixture.traversal.run(0, 15U, 0x66U, stream));
    EXPECT_EQ(stream.size(), fixture.request.root().public_state.legal_actions.size());
}

TEST_CASE(multiway_recursive_traversal_keeps_exact_private_hand_ids_on_root_street) {
    TraversalFixture fixture(2U, 128U, true);
    texas::MultiwayWorkerDeltaStream stream(0U, 128U);

    EXPECT_TRUE(fixture.traversal.run(1, 16U, 0x77U, stream));
    const auto exact_bucket = static_cast<std::uint32_t>(
        texas::MultiwayBucketTable::hole_index({card(12, 0), card(11, 0)}));
    EXPECT_TRUE(stream.size() > 0U);
    for (const auto& delta : stream.deltas()) EXPECT_EQ(delta.bucket, exact_bucket);
}

TEST_CASE(multiway_recursive_traversal_lazily_admits_sampled_opponent_children_and_rows) {
    TraversalFixture fixture(2U);
    texas::MultiwayWorkerDeltaStream stream(0U, 128U);

    EXPECT_TRUE(fixture.traversal.run(0, 11U, 456U, stream));
    const auto root_action_count = fixture.request.root().public_state.legal_actions.size();
    EXPECT_EQ(fixture.coordinator.storage().row_count(), 1U + root_action_count);
    EXPECT_EQ(fixture.coordinator.diagnostics().sparse_rows_admitted, 1U + root_action_count);
    EXPECT_EQ(fixture.coordinator.diagnostics().public_states_admitted, 1U + 2U * root_action_count);
    EXPECT_EQ(stream.size(), root_action_count);
    EXPECT_TRUE(fixture.probe.calls < root_action_count * 5U);
}

TEST_CASE(multiway_recursive_traversal_is_deterministic_for_the_same_seed) {
    TraversalFixture first(2U);
    TraversalFixture second(2U);
    texas::MultiwayWorkerDeltaStream first_stream(0U, 128U);
    texas::MultiwayWorkerDeltaStream second_stream(0U, 128U);

    EXPECT_TRUE(first.traversal.run(0, 99U, 0x12345678U, first_stream));
    EXPECT_TRUE(second.traversal.run(0, 99U, 0x12345678U, second_stream));
    EXPECT_EQ(first_stream.size(), second_stream.size());
    for (std::size_t index = 0; index < first_stream.size(); ++index) {
        expect_same_delta(first_stream.deltas()[index], second_stream.deltas()[index]);
    }
    EXPECT_EQ(first.probe.actors, second.probe.actors);
    EXPECT_EQ(first.probe.current_bets, second.probe.current_bets);
}

TEST_CASE(multiway_recursive_traversal_reaches_both_terminal_and_leaf_paths) {
    bool found_mixed_paths = false;
    for (std::uint64_t seed = 1U; seed <= 64U && !found_mixed_paths; ++seed) {
        TraversalFixture fixture(3U);
        texas::MultiwayWorkerDeltaStream stream(0U, 128U);
        EXPECT_TRUE(fixture.traversal.run(0, 12U, seed, stream));
        const auto root_action_count = fixture.request.root().public_state.legal_actions.size();
        found_mixed_paths = fixture.probe.calls > 0U && fixture.probe.calls < root_action_count;
        for (const auto actor : fixture.probe.actors) {
            EXPECT_TRUE(actor >= 0);
        }
    }
    EXPECT_TRUE(found_mixed_paths);
}

TEST_CASE(multiway_recursive_traversal_rolls_back_trajectory_deltas_on_capacity_exhaustion) {
    TraversalFixture fixture(2U, 1U);
    texas::MultiwayWorkerDeltaStream stream(0U, 1U);
    texas::MultiwayWorkerDelta sentinel;
    sentinel.infoset = {{1U}, 0};
    sentinel.trajectory_id = 1U;
    EXPECT_TRUE(stream.try_append(sentinel));

    EXPECT_TRUE(!fixture.traversal.run(0, 13U, 999U, stream));
    EXPECT_EQ(stream.size(), std::size_t{1});
    EXPECT_EQ(stream.deltas()[0].trajectory_id, 1U);
}

TEST_CASE(multiway_recursive_batch_rotates_traversers_deterministically_across_seats) {
    TraversalFixture fixture(3U);
    EXPECT_EQ(fixture.traversal.traverser_for_trajectory(0U), 0);
    EXPECT_EQ(fixture.traversal.traverser_for_trajectory(1U), 1);
    EXPECT_EQ(fixture.traversal.traverser_for_trajectory(2U), 2);
    EXPECT_EQ(fixture.traversal.traverser_for_trajectory(3U), 0);

    texas::MultiwayRootBatchRunner runner(
        fixture.traversal, fixture.coordinator, 1U, 128U);
    const auto result = runner.run(0U, 3U, 0x5eedU);
    EXPECT_EQ(result.trajectories_attempted, 3U);
    EXPECT_EQ(result.trajectories_accepted, 3U);
    EXPECT_EQ(result.trajectories_discarded, 0U);
    EXPECT_TRUE(result.delta_entries_merged > 0U);
    EXPECT_TRUE(result.clean);
    EXPECT_TRUE(fixture.coordinator.storage().row_count() >= 3U);
    EXPECT_EQ(
        fixture.coordinator.diagnostics().worker_delta_entries_merged,
        result.delta_entries_merged);
}

TEST_CASE(multiway_root_batch_runner_partitions_workers_and_merges_in_fixed_order) {
    ParallelRunnerFixture fixture(3U);
    const auto result = fixture.runner.run(100U, 7U, 0x5eedU);

    EXPECT_EQ(result.trajectories_attempted, 7U);
    EXPECT_EQ(result.trajectories_accepted + result.trajectories_discarded, 7U);
    EXPECT_TRUE(result.delta_entries_merged > 0U);
    EXPECT_TRUE(result.clean);
    EXPECT_EQ(
        fixture.coordinator.diagnostics().worker_delta_entries_merged,
        result.delta_entries_merged);
}

TEST_CASE(multiway_root_batch_runner_is_deterministic_across_worker_partitions) {
    ParallelRunnerFixture single_worker(1U);
    ParallelRunnerFixture repeated_single_worker(1U);
    ParallelRunnerFixture two_workers(2U);
    ParallelRunnerFixture four_workers(4U);

    const auto expected = single_worker.runner.run(50U, 6U, 0x1234U);
    const auto repeat = repeated_single_worker.runner.run(50U, 6U, 0x1234U);
    const auto two = two_workers.runner.run(50U, 6U, 0x1234U);
    const auto four = four_workers.runner.run(50U, 6U, 0x1234U);
    EXPECT_EQ(expected.trajectories_attempted, repeat.trajectories_attempted);
    EXPECT_EQ(expected.trajectories_accepted, two.trajectories_accepted);
    EXPECT_EQ(expected.trajectories_discarded, four.trajectories_discarded);
    EXPECT_EQ(expected.delta_entries_merged, two.delta_entries_merged);
    EXPECT_EQ(expected.delta_entries_merged, four.delta_entries_merged);
    EXPECT_EQ(expected.run.schedule_fingerprint, repeat.run.schedule_fingerprint);
    EXPECT_TRUE(expected.run.schedule_fingerprint != two.run.schedule_fingerprint);
    EXPECT_TRUE(two.run.schedule_fingerprint != four.run.schedule_fingerprint);
    EXPECT_EQ(expected.run.merged_stream_fingerprint, repeat.run.merged_stream_fingerprint);
    EXPECT_EQ(expected.run.merged_stream_fingerprint, two.run.merged_stream_fingerprint);
    EXPECT_EQ(expected.run.merged_stream_fingerprint, four.run.merged_stream_fingerprint);
    EXPECT_TRUE(expected.run.bitwise_deterministic);
    EXPECT_EQ(expected.run.partition_version, texas::MULTIWAY_PARTITION_VERSION);
    EXPECT_EQ(expected.run.action_sampling_version, texas::MULTIWAY_ACTION_SAMPLING_VERSION);
    EXPECT_EQ(expected.run.public_chance_order_version, texas::MULTIWAY_PUBLIC_CHANCE_ORDER_VERSION);
    EXPECT_EQ(expected.run.merge_order_version, texas::MULTIWAY_MERGE_ORDER_VERSION);

    const auto expected_policy = single_worker.coordinator.export_root_policy();
    const auto repeat_policy = repeated_single_worker.coordinator.export_root_policy();
    const auto two_policy = two_workers.coordinator.export_root_policy();
    const auto four_policy = four_workers.coordinator.export_root_policy();
    EXPECT_EQ(expected_policy.actions.size(), two_policy.actions.size());
    for (std::size_t action = 0; action < expected_policy.actions.size(); ++action) {
        EXPECT_EQ(expected_policy.actions[action].action, repeat_policy.actions[action].action);
        EXPECT_EQ(expected_policy.actions[action].action, two_policy.actions[action].action);
        EXPECT_EQ(expected_policy.actions[action].action, four_policy.actions[action].action);
        EXPECT_EQ(expected_policy.actions[action].probability, repeat_policy.actions[action].probability);
        EXPECT_EQ(expected_policy.actions[action].probability, two_policy.actions[action].probability);
        EXPECT_EQ(expected_policy.actions[action].probability, four_policy.actions[action].probability);
    }
}

TEST_CASE(multiway_recursive_workers_publish_deltas_without_mutating_shared_row_values) {
    TraversalFixture fixture(2U);
    texas::MultiwayWorkerDeltaStream stream(0U, 128U);

    EXPECT_TRUE(fixture.traversal.run(0, 99U, 0x12345678U, stream));
    EXPECT_TRUE(stream.size() > 0U);
    const auto strategy_sums = fixture.coordinator.export_root_strategy_sums();
    EXPECT_TRUE(!strategy_sums.empty());
    for (const auto value : strategy_sums) EXPECT_EQ(value, 0.0);
    EXPECT_EQ(fixture.coordinator.diagnostics().worker_delta_entries_merged, 0U);
    EXPECT_EQ(fixture.coordinator.diagnostics().last_merged_stream_fingerprint, 0U);
}

TEST_CASE(multiway_root_batch_runner_one_worker_remains_compatible) {
    ParallelRunnerFixture fixture(1U);
    const auto result = fixture.runner.run(0U, 3U, 0x77U);

    EXPECT_EQ(result.trajectories_attempted, 3U);
    EXPECT_EQ(result.trajectories_accepted + result.trajectories_discarded, 3U);
    EXPECT_TRUE(result.clean);
    EXPECT_EQ(
        fixture.coordinator.diagnostics().worker_delta_entries_merged,
        result.delta_entries_merged);
}
