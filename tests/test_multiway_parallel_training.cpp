#include "solver/multiway/abstraction/multiway_action_abstraction.hpp"
#include "solver/multiway/abstraction/multiway_bucket_model.hpp"
#include "solver/multiway/abstraction/multiway_public_builder.hpp"
#include "solver/multiway/blueprint/multiway_blueprint_trainer.hpp"
#include "solver/multiway/engine/multiway_traversal.hpp"
#include "test_harness.hpp"

#include <array>
#include <chrono>
#include <cstdlib>
#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

namespace {

constexpr std::uint8_t card(std::uint8_t rank, std::uint8_t suit) {
    return texas::card_to_int(rank, suit);
}

const std::vector<std::uint8_t> kBoard = {
    card(2, 0), card(7, 1), card(9, 2), card(4, 3), card(6, 0),
};

std::vector<std::uint32_t> one_bucket_assignments() {
    std::vector<std::uint32_t> result(texas::MULTIWAY_HOLE_COMBINATION_COUNT, 0U);
    for (std::uint8_t first = 0U; first < 52U; ++first) {
        for (std::uint8_t second = static_cast<std::uint8_t>(first + 1U); second < 52U; ++second) {
            for (const auto board_card : kBoard) {
                if (first == board_card || second == board_card) {
                    result[texas::MultiwayBucketTable::hole_index({first, second})] =
                        texas::MULTIWAY_INVALID_BUCKET;
                }
            }
        }
    }
    return result;
}

texas::MultiwayRootSnapshot make_root(const texas::MultiwayActionAbstraction& abstraction) {
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

texas::Value deterministic_leaf(
    const texas::MultiwayLeafEvaluationRequest& request, const void*) noexcept {
    const auto traverser = static_cast<std::size_t>(request.traverser);
    return static_cast<texas::Value>(
        request.betting->contributions[traverser] - request.betting->current_bet);
}

texas::MultiwayModelIdentity training_identity(std::uint32_t workers) {
    texas::MultiwayBlueprintTrainingConfig config;
    config.limits.worker_count = workers;
    config.limits.trajectories_per_batch = 32U;
    config.limits.max_public_states = 256U;
    config.limits.max_sparse_rows = 128U;
    config.limits.max_sparse_values = 1024U;
    config.limits.max_worker_delta_entries = 2048U;
    config.limits.max_batches = 1024U;
    return config.identity();
}

struct ParallelTrainingFixture {
    explicit ParallelTrainingFixture(
        std::uint32_t workers,
        std::size_t max_public_states = 256U,
        std::size_t delta_capacity = 2048U)
        : root(make_root(abstraction)),
          request(root, [] {
              texas::MultiwayCFRConfig cfr;
              cfr.player_count = 3U;
              return cfr;
          }(), [workers, max_public_states, delta_capacity] {
              texas::MultiwaySolverLimits limits;
              limits.worker_count = workers;
              limits.trajectories_per_batch = 32U;
              limits.max_public_states = max_public_states;
              limits.max_sparse_rows = 128U;
              limits.max_sparse_values = 1024U;
              limits.max_worker_delta_entries = delta_capacity;
              limits.max_batches = 1024U;
              limits.storage_backend = texas::MultiwaySolverLimits::StorageBackend::CompactInt32;
              return limits;
          }()),
          coordinator(request),
          buckets({texas::MultiwayBucketTable(
              [] {
                  texas::MultiwayBlueprintConfig config;
                  config.player_count = 3U;
                  return texas::make_multiway_model_identity(config);
              }(), texas::Street::River, kBoard, 1U, one_bucket_assignments())}),
          evaluator{deterministic_leaf, nullptr},
          traversal(coordinator, request.root(), abstraction, buckets, &evaluator, 1U),
          runner(traversal, coordinator, workers, delta_capacity),
          trainer(training_identity(workers), runner, coordinator, {}, 0x77U, workers, 123456U) {}

    texas::MultiwayActionAbstraction abstraction;
    texas::MultiwayRootSnapshot root;
    texas::MultiwaySolveRequest request;
    texas::MultiwaySolverCoordinator coordinator;
    texas::MultiwayBucketRegistry buckets;
    texas::MultiwayLeafEvaluator evaluator;
    texas::MultiwayRootExternalSamplingTraversal traversal;
    texas::MultiwayRootBatchRunner runner;
    texas::MultiwayBlueprintTrainer trainer;
};

void expect_same_storage(
    const texas::MultiwayCoordinatorCheckpoint& expected,
    const texas::MultiwayCoordinatorCheckpoint& actual) {
    EXPECT_EQ(actual.public_states.size(), expected.public_states.size());
    for (std::size_t index = 0U; index < expected.public_states.size(); ++index) {
        EXPECT_EQ(actual.public_states[index].id, expected.public_states[index].id);
    }
    EXPECT_EQ(actual.storage.shapes.size(), expected.storage.shapes.size());
    for (std::size_t index = 0U; index < expected.storage.shapes.size(); ++index) {
        EXPECT_EQ(actual.storage.shapes[index].infoset, expected.storage.shapes[index].infoset);
        EXPECT_EQ(actual.storage.shapes[index].bucket_count, expected.storage.shapes[index].bucket_count);
        EXPECT_EQ(actual.storage.shapes[index].action_count, expected.storage.shapes[index].action_count);
    }
    EXPECT_EQ(actual.storage.regrets, expected.storage.regrets);
    EXPECT_EQ(actual.storage.strategy_sums, expected.storage.strategy_sums);
    EXPECT_EQ(actual.terminal_visits, expected.terminal_visits);
    EXPECT_EQ(actual.leaf_visits, expected.leaf_visits);
    EXPECT_EQ(actual.last_merged_stream_fingerprint, expected.last_merged_stream_fingerprint);
}

void expect_same_policy_rows(
    const texas::MultiwayFullBlueprintArtifact& expected,
    const texas::MultiwayFullBlueprintArtifact& actual) {
    EXPECT_EQ(actual.rows.size(), expected.rows.size());
    for (std::size_t row = 0U; row < expected.rows.size(); ++row) {
        EXPECT_EQ(actual.rows[row].infoset, expected.rows[row].infoset);
        EXPECT_EQ(actual.rows[row].bucket, expected.rows[row].bucket);
        EXPECT_EQ(actual.rows[row].action_menu_id, expected.rows[row].action_menu_id);
        EXPECT_EQ(actual.rows[row].actions.size(), expected.rows[row].actions.size());
        for (std::size_t action = 0U; action < expected.rows[row].actions.size(); ++action) {
            EXPECT_EQ(actual.rows[row].actions[action].action, expected.rows[row].actions[action].action);
            EXPECT_EQ(actual.rows[row].actions[action].probability,
                expected.rows[row].actions[action].probability);
        }
    }
}

}  // namespace

TEST_CASE(multiway_parallel_training_matches_one_worker_through_sixteen_workers) {
    ParallelTrainingFixture reference(1U);
    reference.trainer.run_batches(3U, 32U, 0x77U);
    const auto expected = reference.trainer.checkpoint().coordinator;
    const auto expected_policy = reference.trainer.export_full_policy();
    for (const std::uint32_t workers : {2U, 4U, 8U, 16U}) {
        ParallelTrainingFixture parallel(workers);
        parallel.trainer.run_batches(3U, 32U, 0x77U);
        expect_same_storage(expected, parallel.trainer.checkpoint().coordinator);
        expect_same_policy_rows(expected_policy, parallel.trainer.export_full_policy());
        const auto& status = parallel.trainer.status();
        EXPECT_EQ(status.requested_worker_count, workers);
        EXPECT_EQ(status.effective_worker_count, workers);
        EXPECT_EQ(status.trajectories_per_batch, 32U);
        EXPECT_EQ(status.memory_preflight_estimate_bytes, 123456U);
        EXPECT_TRUE(status.peak_worker_delta_entries <= status.configured_worker_delta_capacity);
        EXPECT_TRUE(status.cumulative_worker_delta_entries >= status.peak_worker_delta_entries);
        EXPECT_TRUE(status.coordinator_wait_nanoseconds > 0U);
    }
}

TEST_CASE(multiway_parallel_training_resume_is_exact_at_fixed_worker_count) {
    ParallelTrainingFixture continuous(16U);
    continuous.trainer.run_batches(2U, 32U, 0x77U);
    const auto midpoint = continuous.trainer.checkpoint();
    continuous.trainer.run_batches(2U, 32U, 0x77U);

    ParallelTrainingFixture resumed(16U);
    resumed.trainer.resume_from_checkpoint(midpoint);
    resumed.trainer.run_batches(2U, 32U, 0x77U);
    expect_same_storage(
        continuous.trainer.checkpoint().coordinator,
        resumed.trainer.checkpoint().coordinator);
}

TEST_CASE(multiway_parallel_training_rejects_checkpoint_from_different_worker_identity) {
    ParallelTrainingFixture single(1U);
    single.trainer.run_batches(1U, 32U, 0x77U);
    ParallelTrainingFixture parallel(16U);
    EXPECT_THROW(parallel.trainer.resume_from_checkpoint(single.trainer.checkpoint()),
        std::invalid_argument);
}

TEST_CASE(multiway_parallel_runner_reports_true_per_worker_delta_high_water) {
    ParallelTrainingFixture fixture(16U);
    const auto result = fixture.runner.run(0U, 4U, 0x77U);
    EXPECT_TRUE(result.clean);
    EXPECT_EQ(result.minimum_worker_trajectories, 0U);
    EXPECT_EQ(result.maximum_worker_trajectories, 1U);
    EXPECT_TRUE(result.maximum_worker_delta_entries <= result.delta_entries_merged);
    EXPECT_TRUE(result.maximum_worker_delta_entries < 2048U);
    EXPECT_TRUE(result.coordinator_wait_nanoseconds > 0U);
}

TEST_CASE(multiway_parallel_runner_propagates_worker_exceptions_and_joins) {
    ParallelTrainingFixture fixture(4U, 1U);
    EXPECT_THROW(fixture.runner.run(0U, 4U, 0x77U), std::length_error);
}

TEST_CASE(multiway_parallel_runner_reports_worker_capacity_discards) {
    ParallelTrainingFixture fixture(4U, 256U, 1U);
    const auto result = fixture.runner.run(0U, 8U, 0x77U);
    EXPECT_TRUE(result.clean);
    EXPECT_TRUE(result.trajectories_discarded > 0U);
    EXPECT_TRUE(result.maximum_worker_delta_entries <= 1U);
}

TEST_CASE(multiway_parallel_training_scaling_smoke) {
    const auto* value = std::getenv("TEXASSOLVER_PARALLEL_SMOKE_BATCHES");
    const auto batches = value == nullptr || *value == '\0' ? 20U : std::stoull(value);
    for (const std::uint32_t workers : {1U, 2U, 4U, 8U, 16U}) {
        ParallelTrainingFixture fixture(workers);
        fixture.trainer.run_batches(1U, 32U, 0x77U);
        const auto started = std::chrono::steady_clock::now();
        fixture.trainer.run_batches(batches, 32U, 0x77U);
        const auto elapsed = std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now() - started).count();
        std::cout << "workers=" << workers
                  << " trajectories=" << batches * 32U
                  << " elapsed_nanoseconds=" << elapsed
                  << " trajectories_per_second="
                  << (elapsed == 0 ? 0.0 : static_cast<double>(batches * 32U) * 1.0e9 /
                      static_cast<double>(elapsed)) << '\n';
    }
}
