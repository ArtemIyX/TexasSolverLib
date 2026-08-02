#include "solver/multiway_action_abstraction.hpp"
#include "solver/multiway_blueprint_config.hpp"
#include "solver/multiway_bucket_model.hpp"
#include "solver/multiway_model_identity.hpp"
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
    return core::card_to_int(rank, suit);
}

const std::vector<std::uint8_t> kBoard = {
    card(2, 0), card(7, 1), card(9, 2), card(4, 3), card(6, 0),
};

std::vector<std::uint32_t> one_bucket_assignments() {
    std::vector<std::uint32_t> assignments(core::MULTIWAY_HOLE_COMBINATION_COUNT, 0U);
    for (std::uint8_t first = 0; first < 52U; ++first) {
        for (std::uint8_t second = static_cast<std::uint8_t>(first + 1U); second < 52U; ++second) {
            const std::array<std::uint8_t, 2> hole = {first, second};
            for (const auto board_card : kBoard) {
                if (first == board_card || second == board_card) {
                    assignments[core::MultiwayBucketTable::hole_index(hole)] =
                        core::MULTIWAY_INVALID_BUCKET;
                }
            }
        }
    }
    return assignments;
}

core::MultiwayRootSnapshot make_root(const core::MultiwayActionAbstraction& abstraction) {
    core::MultiwayGameConfig game;
    game.starting_stacks = {1000, 1000, 1000};
    game.initial_contributions = {0, 0, 0};
    game.initial_street_contributions = {0, 0, 0};
    game.first_player = 0;
    game.big_blind = 100;
    game.street = core::Street::River;
    const auto betting = core::MultiwayState::initial(game).snapshot();

    core::MultiwayRootSnapshot root;
    root.public_state.id = {1U};
    root.public_state.canonical_history_id = 1U;
    root.public_state.betting = betting;
    root.public_state.board = kBoard;
    root.public_state.board_runout.remaining_board_cards = 0U;
    root.public_state.legal_actions = abstraction.make_legal_actions(betting, 77U);
    root.root_infoset = {{1U}, 0};
    root.root_bucket = 0U;
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

core::MultiwaySolverLimits make_limits(std::size_t delta_capacity = 128U) {
    core::MultiwaySolverLimits limits;
    limits.worker_count = 1U;
    limits.trajectories_per_batch = 8U;
    limits.max_public_states = 128U;
    limits.max_sparse_rows = 32U;
    limits.max_sparse_values = 256U;
    limits.max_worker_delta_entries = delta_capacity;
    return limits;
}

core::MultiwayCFRConfig make_cfr() {
    core::MultiwayCFRConfig cfr;
    cfr.player_count = 3U;
    return cfr;
}

core::MultiwayBucketRegistry make_buckets() {
    core::MultiwayBlueprintConfig config;
    config.player_count = 3U;
    return core::MultiwayBucketRegistry({core::MultiwayBucketTable(
        core::make_multiway_model_identity(config), core::Street::River,
        kBoard, 1U, one_bucket_assignments())});
}

struct LeafProbe {
    std::size_t calls = 0U;
    std::vector<core::PlayerId> actors;
    std::vector<int> current_bets;
};

core::Value probe_leaf(
    const core::MultiwayLeafEvaluationRequest& request,
    const void* context) noexcept {
    auto& probe = *const_cast<LeafProbe*>(static_cast<const LeafProbe*>(context));
    ++probe.calls;
    probe.actors.push_back(request.betting->current_player);
    probe.current_bets.push_back(request.betting->current_bet);
    const auto traverser = static_cast<std::size_t>(request.traverser);
    return static_cast<core::Value>(request.betting->contributions[traverser] -
                                    request.betting->current_bet);
}

struct TraversalFixture {
    explicit TraversalFixture(
        std::uint32_t max_depth,
        std::size_t delta_capacity = 128U)
        : root(make_root(abstraction)),
          request(root, make_cfr(), make_limits(delta_capacity)),
          coordinator(request),
          buckets(make_buckets()),
          evaluator{probe_leaf, &probe},
          traversal(coordinator, request.root(), abstraction, buckets, &evaluator, max_depth) {}

    core::MultiwayActionAbstraction abstraction;
    core::MultiwayRootSnapshot root;
    core::MultiwaySolveRequest request;
    core::MultiwaySolverCoordinator coordinator;
    core::MultiwayBucketRegistry buckets;
    LeafProbe probe;
    core::MultiwayLeafEvaluator evaluator;
    core::MultiwayRootExternalSamplingTraversal traversal;
};

void expect_same_delta(
    const core::MultiwayWorkerDelta& left,
    const core::MultiwayWorkerDelta& right) {
    EXPECT_EQ(left.infoset, right.infoset);
    EXPECT_EQ(left.bucket, right.bucket);
    EXPECT_EQ(left.action, right.action);
    EXPECT_NEAR(left.regret, right.regret, 1e-12);
    EXPECT_NEAR(left.strategy_sum, right.strategy_sum, 1e-12);
    EXPECT_EQ(left.trajectory_id, right.trajectory_id);
}

}  // namespace

TEST_CASE(multiway_recursive_traversal_rejects_unsupported_depth_and_invalid_seat) {
    core::MultiwayActionAbstraction abstraction;
    const auto root = make_root(abstraction);
    const core::MultiwaySolveRequest request(root, make_cfr(), make_limits());
    core::MultiwaySolverCoordinator coordinator(request);
    const auto buckets = make_buckets();
    LeafProbe probe;
    const core::MultiwayLeafEvaluator evaluator = {probe_leaf, &probe};

    EXPECT_THROW(
        core::MultiwayRootExternalSamplingTraversal(
            coordinator, request.root(), abstraction, buckets, &evaluator, 0U),
        std::invalid_argument);
    EXPECT_THROW(
        core::MultiwayRootExternalSamplingTraversal(
            coordinator, request.root(), abstraction, buckets, &evaluator, 65U),
        std::invalid_argument);

    core::MultiwayRootExternalSamplingTraversal traversal(
        coordinator, request.root(), abstraction, buckets, &evaluator, 1U);
    core::MultiwayWorkerDeltaStream stream(0U, 128U);
    EXPECT_THROW(traversal.run(-1, 1U, 7U, stream), std::invalid_argument);
    EXPECT_THROW(traversal.run(3, 1U, 7U, stream), std::invalid_argument);
}

TEST_CASE(multiway_recursive_traversal_accepts_a_non_root_opponent_traverser) {
    TraversalFixture fixture(2U);
    core::MultiwayWorkerDeltaStream stream(0U, 128U);

    EXPECT_TRUE(fixture.traversal.run(1, 1U, 7U, stream));
    EXPECT_TRUE(stream.size() > 0U);
    for (const auto& delta : stream.deltas()) {
        EXPECT_EQ(delta.infoset.seat, 1);
        EXPECT_EQ(delta.trajectory_id, 1U);
    }
}

TEST_CASE(multiway_recursive_traversal_depth_one_cuts_off_at_typed_leaves) {
    TraversalFixture fixture(1U);
    core::MultiwayWorkerDeltaStream stream(0U, 128U);

    EXPECT_TRUE(fixture.traversal.run(0, 10U, 123U, stream));
    EXPECT_EQ(fixture.probe.calls, fixture.request.root().public_state.legal_actions.size());
    EXPECT_EQ(fixture.coordinator.storage().row_count(), std::size_t{1});
    EXPECT_EQ(
        fixture.coordinator.diagnostics().public_states_admitted,
        1U + fixture.request.root().public_state.legal_actions.size());
    EXPECT_EQ(stream.size(), fixture.request.root().public_state.legal_actions.size());
}

TEST_CASE(multiway_recursive_traversal_lazily_admits_sampled_opponent_children_and_rows) {
    TraversalFixture fixture(2U);
    core::MultiwayWorkerDeltaStream stream(0U, 128U);

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
    core::MultiwayWorkerDeltaStream first_stream(0U, 128U);
    core::MultiwayWorkerDeltaStream second_stream(0U, 128U);

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
        core::MultiwayWorkerDeltaStream stream(0U, 128U);
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
    core::MultiwayWorkerDeltaStream stream(0U, 1U);
    core::MultiwayWorkerDelta sentinel;
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

    core::MultiwayRootBatchRunner runner(
        fixture.traversal, fixture.coordinator, 1U, 128U);
    const auto result = runner.run(0U, 3U, 0x5eedU);
    EXPECT_EQ(result.trajectories_attempted, 3U);
    EXPECT_EQ(result.trajectories_accepted, 3U);
    EXPECT_EQ(result.trajectories_discarded, 0U);
    EXPECT_TRUE(result.delta_entries_merged > 0U);
    EXPECT_TRUE(fixture.coordinator.storage().row_count() >= 3U);
    EXPECT_EQ(
        fixture.coordinator.diagnostics().worker_delta_entries_merged,
        result.delta_entries_merged);
}
