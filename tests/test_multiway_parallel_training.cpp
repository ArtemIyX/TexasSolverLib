#include "solver/multiway/abstraction/multiway_action_abstraction.hpp"
#include "solver/multiway/abstraction/multiway_bucket_model.hpp"
#include "solver/multiway/abstraction/multiway_public_builder.hpp"
#include "solver/multiway/blueprint/multiway_blueprint_trainer.hpp"
#include "solver/multiway/engine/multiway_traversal.hpp"
#include "solver/multiway/engine/multiway_memory.hpp"
#include "solver/multiway/engine/multiway_worker_topology.hpp"
#include "test_harness.hpp"

#include <array>
#include <chrono>
#include <cstdlib>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace
{

	constexpr std::uint8_t card(std::uint8_t rank, std::uint8_t suit)
	{
		return texas::card_to_int(rank, suit);
	}

	const std::vector<std::uint8_t> kBoard = {
		card(2, 0),
		card(7, 1),
		card(9, 2),
		card(4, 3),
		card(6, 0),
	};

	std::vector<std::uint32_t> one_bucket_assignments()
	{
		std::vector<std::uint32_t> result(texas::MULTIWAY_HOLE_COMBINATION_COUNT, 0U);
		for (std::uint8_t first = 0U; first < 52U; ++first)
		{
			for (std::uint8_t second = static_cast<std::uint8_t>(first + 1U); second < 52U; ++second)
			{
				for (const auto board_card : kBoard)
				{
					if (first == board_card || second == board_card)
					{
						result[texas::MultiwayBucketTable::hole_index({ first, second })] =
							texas::MULTIWAY_INVALID_BUCKET;
					}
				}
			}
		}
		return result;
	}

	texas::MultiwayRootSnapshot make_root(const texas::MultiwayActionAbstraction& abstraction)
	{
		texas::MultiwayGameConfig game;
		game.starting_stacks = { 1000, 1000, 1000 };
		game.initial_contributions = { 0, 0, 0 };
		game.initial_street_contributions = { 0, 0, 0 };
		game.first_player = 0;
		game.big_blind = 100;
		game.street = texas::Street::River;
		const auto betting = texas::MultiwayState::initial(game).snapshot();
		texas::MultiwayRootSnapshot root;
		root.public_state = texas::MultiwayPublicBuilder::make_root(
			betting, kBoard, abstraction.make_legal_actions(betting));
		root.root_infoset = { root.public_state.id, 0 };
		root.root_bucket = 0U;
		root.seat_order = { 0, 1, 2 };
		root.next_street_first_seat = 0;
		root.odd_chip_first_seat = 0;
		root.private_ranges.board = kBoard;
		root.private_ranges.ranges = {
			{ { { card(14, 0), card(13, 0) }, 1.0 } },
			{ { { card(12, 0), card(11, 0) }, 1.0 } },
			{ { { card(10, 0), card(8, 0) }, 1.0 } },
		};
		root.action_abstraction_version = 1U;
		root.leaf_model_version = 1U;
		return root;
	}

	texas::Value deterministic_leaf(
		const texas::MultiwayLeafEvaluationRequest& request, const void*) noexcept
	{
		const auto traverser = static_cast<std::size_t>(request.traverser);
		return static_cast<texas::Value>(
			request.betting->contributions[traverser] - request.betting->current_bet);
	}

	texas::MultiwayModelIdentity training_identity(std::uint32_t workers)
	{
		texas::MultiwayBlueprintTrainingConfig config;
		config.limits.worker_count = workers;
		config.limits.trajectories_per_batch = 32U;
		config.limits.max_public_states = 256U;
		config.limits.max_sparse_rows = 128U;
		config.limits.max_sparse_values = 1024U;
		config.limits.max_worker_delta_entries = 2048U;
		config.limits.max_discovery_entries = 8192U;
		config.limits.max_batches = 1024U;
		return config.identity();
	}

	struct ParallelTrainingFixture
	{
		explicit ParallelTrainingFixture(
			std::uint32_t workers,
			std::size_t max_public_states = 256U,
			std::size_t delta_capacity = 2048U,
			bool immutable_epochs = false,
			texas::MultiwaySchedulerMode scheduler_mode = texas::MultiwaySchedulerMode::Fixed,
			std::uint32_t dynamic_chunk_size = texas::MULTIWAY_DYNAMIC_SCHEDULER_CHUNK_SIZE,
			bool pin_workers = false,
			texas::MultiwaySearchProfileMode profile_mode = texas::MultiwaySearchProfileMode::Disabled,
			std::size_t explicit_regret_capacity = 0U,
			std::size_t explicit_continuation_capacity = 0U,
			std::size_t explicit_discovery_public_state_capacity = 0U,
			std::size_t explicit_discovery_row_capacity = 0U)
			: root(make_root(abstraction)), request(root, [] {
              texas::MultiwayCFRConfig cfr;
              cfr.player_count = 3U;
			return cfr; }(), [workers, max_public_states, delta_capacity, explicit_regret_capacity, explicit_continuation_capacity, explicit_discovery_public_state_capacity, explicit_discovery_row_capacity] {
              texas::MultiwaySolverLimits limits;
              limits.worker_count = workers;
              limits.trajectories_per_batch = 32U;
              limits.max_public_states = max_public_states;
              limits.max_sparse_rows = 128U;
              limits.max_sparse_values = 1024U;
              limits.max_worker_delta_entries = delta_capacity;
              limits.max_discovery_entries = delta_capacity * 4U;
              if (explicit_regret_capacity != 0U)
                  limits.max_regret_delta_entries_per_worker = explicit_regret_capacity;
              if (explicit_continuation_capacity != 0U)
                  limits.max_continuation_delta_entries_per_worker = explicit_continuation_capacity;
              if (explicit_discovery_public_state_capacity != 0U)
                  limits.max_discovery_public_states_per_worker = explicit_discovery_public_state_capacity;
              if (explicit_discovery_row_capacity != 0U)
                  limits.max_discovery_rows_per_worker = explicit_discovery_row_capacity;
              limits.max_batches = 1024U;
              limits.storage_backend = texas::MultiwaySolverLimits::StorageBackend::CompactInt32;
              return limits; }()), coordinator(request), buckets({ texas::MultiwayBucketTable([] {
                  texas::MultiwayBlueprintConfig config;
              config.player_count = 3U;
				   return texas::make_multiway_model_identity(config); }(), texas::Street::River, kBoard, 1U, one_bucket_assignments()) }), evaluator{ deterministic_leaf, nullptr }, traversal(coordinator, request.root(), abstraction, buckets, &evaluator, 1U), runner(traversal, coordinator, workers, texas::MultiwayWorkerStreamCapacities{ explicit_regret_capacity == 0U ? delta_capacity : explicit_regret_capacity, explicit_continuation_capacity == 0U ? delta_capacity : explicit_continuation_capacity, explicit_discovery_public_state_capacity, explicit_discovery_row_capacity }, profile_mode, pin_workers, immutable_epochs, scheduler_mode, dynamic_chunk_size), trainer(training_identity(workers), runner, coordinator, {}, 0x77U, workers, 123456U) {}

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
		const texas::MultiwayCoordinatorCheckpoint& actual)
	{
		EXPECT_EQ(actual.public_states.size(), expected.public_states.size());
		for (std::size_t index = 0U; index < expected.public_states.size(); ++index)
		{
			EXPECT_EQ(actual.public_states[index].id, expected.public_states[index].id);
		}
		EXPECT_EQ(actual.storage.shapes.size(), expected.storage.shapes.size());
		for (std::size_t index = 0U; index < expected.storage.shapes.size(); ++index)
		{
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
		const texas::MultiwayFullBlueprintArtifact& actual)
	{
		EXPECT_EQ(actual.rows.size(), expected.rows.size());
		for (std::size_t row = 0U; row < expected.rows.size(); ++row)
		{
			EXPECT_EQ(actual.rows[row].infoset, expected.rows[row].infoset);
			EXPECT_EQ(actual.rows[row].bucket, expected.rows[row].bucket);
			EXPECT_EQ(actual.rows[row].action_menu_id, expected.rows[row].action_menu_id);
			EXPECT_EQ(actual.rows[row].actions.size(), expected.rows[row].actions.size());
			for (std::size_t action = 0U; action < expected.rows[row].actions.size(); ++action)
			{
				EXPECT_EQ(actual.rows[row].actions[action].action, expected.rows[row].actions[action].action);
				EXPECT_EQ(actual.rows[row].actions[action].probability,
					expected.rows[row].actions[action].probability);
			}
		}
	}

	struct BatchProgressCapture
	{
		std::vector<std::uint64_t> batches;
		std::vector<std::uint64_t> trajectories;
	};

	void capture_batch_progress(
		const texas::MultiwayBlueprintTrainingStatus& status,
		void* context)
	{
		auto& capture = *static_cast<BatchProgressCapture*>(context);
		capture.batches.push_back(status.batches);
		capture.trajectories.push_back(status.trajectories);
	}

} // namespace

TEST_CASE(multiway_training_batch_progress_reports_each_completed_batch)
{
	ParallelTrainingFixture first(4U);
	ParallelTrainingFixture second(4U);
	BatchProgressCapture first_progress;
	BatchProgressCapture second_progress;
	first.trainer.run_batches(3U, 32U, 0x77U, capture_batch_progress, &first_progress);
	second.trainer.run_batches(3U, 32U, 0x77U, capture_batch_progress, &second_progress);

	EXPECT_EQ(first_progress.batches.size(), 3U);
	EXPECT_EQ(first_progress.batches[0U], 1U);
	EXPECT_EQ(first_progress.batches[1U], 2U);
	EXPECT_EQ(first_progress.batches[2U], 3U);
	EXPECT_EQ(first_progress.trajectories[0U], 32U);
	EXPECT_EQ(first_progress.trajectories[1U], 64U);
	EXPECT_EQ(first_progress.trajectories[2U], 96U);
	EXPECT_EQ(first_progress.batches, second_progress.batches);
	EXPECT_EQ(first_progress.trajectories, second_progress.trajectories);
	EXPECT_EQ(first.trainer.status().merged_stream_fingerprint,
		second.trainer.status().merged_stream_fingerprint);
}

TEST_CASE(multiway_memory_worker_attribution_scales_from_one_to_four_workers)
{
	ParallelTrainingFixture one(1U, 64U, 32U);
	ParallelTrainingFixture four(4U, 64U, 32U);
	const auto one_snapshot = texas::solver::multiway::capture_multiway_memory_subsystems(nullptr, nullptr, &one.runner);
	const auto four_snapshot = texas::solver::multiway::capture_multiway_memory_subsystems(nullptr, nullptr, &four.runner);
	EXPECT_EQ(one_snapshot.worker_count, 1U);
	EXPECT_EQ(four_snapshot.worker_count, 4U);
	EXPECT_EQ(four_snapshot.worker_regret_delta_stream.retained_capacity_bytes,
		4U * one_snapshot.worker_regret_delta_stream.retained_capacity_bytes);
	EXPECT_EQ(four_snapshot.worker_continuation_delta_stream.retained_capacity_bytes,
		4U * one_snapshot.worker_continuation_delta_stream.retained_capacity_bytes);
	EXPECT_EQ(four_snapshot.discovery_public_state_stream.retained_capacity_bytes,
		4U * one_snapshot.discovery_public_state_stream.retained_capacity_bytes);
	EXPECT_EQ(four_snapshot.discovery_row_stream.retained_capacity_bytes,
		4U * one_snapshot.discovery_row_stream.retained_capacity_bytes);
	EXPECT_EQ(four_snapshot.worker_action_menu_scratch.retained_capacity_bytes,
		4U * one_snapshot.worker_action_menu_scratch.retained_capacity_bytes);
	std::uint64_t per_worker_total = 0U;
	for (std::size_t worker = 0U; worker < four_snapshot.worker_count; ++worker)
		per_worker_total += four_snapshot.workers[worker].total.retained_capacity_bytes;
	EXPECT_EQ(per_worker_total,
		four_snapshot.worker_regret_delta_stream.retained_capacity_bytes + four_snapshot.worker_continuation_delta_stream.retained_capacity_bytes + four_snapshot.discovery_public_state_stream.retained_capacity_bytes + four_snapshot.discovery_row_stream.retained_capacity_bytes + four_snapshot.worker_action_menu_scratch.retained_capacity_bytes);
}

TEST_CASE(multiway_memory_worker_streams_use_distinct_explicit_capacities)
{
	ParallelTrainingFixture fixture(
		2U, 256U, 2048U, true, texas::MultiwaySchedulerMode::Fixed,
		texas::MULTIWAY_DYNAMIC_SCHEDULER_CHUNK_SIZE, false,
		texas::MultiwaySearchProfileMode::Disabled, 7U, 3U, 11U, 5U);
	const auto snapshot = fixture.runner.memory_snapshot();
	EXPECT_EQ(snapshot.worker_count, 2U);
	EXPECT_EQ(snapshot.workers[0U].regret_delta_stream.retained_capacity_bytes,
		7U * sizeof(texas::MultiwayWorkerDelta));
	EXPECT_EQ(snapshot.workers[0U].continuation_delta_stream.retained_capacity_bytes,
		3U * sizeof(texas::MultiwayContinuationDelta));
	EXPECT_EQ(snapshot.workers[0U].discovery_public_states.retained_capacity_bytes,
		11U * sizeof(texas::MultiwayPublicStateDescriptor));
	EXPECT_EQ(snapshot.workers[0U].discovery_rows.retained_capacity_bytes,
		5U * sizeof(texas::MultiwaySparseRowShape));

	const auto result = fixture.runner.run(0U, 1U, 0x77U);
	EXPECT_TRUE(result.clean);
	EXPECT_TRUE(!result.affinity_requested);
	EXPECT_TRUE(!result.capacity_exhausted);
}

TEST_CASE(multiway_parallel_training_fixed_and_dynamic_modes_match_at_focused_worker_counts)
{
	for (const std::uint32_t workers : { 1U, 4U, 8U })
	{
		ParallelTrainingFixture fixed(workers, 256U, 2048U, false, texas::MultiwaySchedulerMode::Fixed);
		ParallelTrainingFixture dynamic(workers, 256U, 2048U, false, texas::MultiwaySchedulerMode::Dynamic);
		ParallelTrainingFixture repeated_dynamic(workers, 256U, 2048U, false, texas::MultiwaySchedulerMode::Dynamic);
		fixed.trainer.run_batches(3U, 32U, 0x77U);
		dynamic.trainer.run_batches(3U, 32U, 0x77U);
		repeated_dynamic.trainer.run_batches(3U, 32U, 0x77U);

		const auto& fixed_status = fixed.trainer.status();
		const auto& dynamic_status = dynamic.trainer.status();
		const auto& repeated_status = repeated_dynamic.trainer.status();
		EXPECT_EQ(fixed_status.trajectories, dynamic_status.trajectories);
		EXPECT_EQ(fixed_status.trajectories, 96U);
		EXPECT_EQ(fixed_status.discarded_trajectories, 0U);
		EXPECT_EQ(fixed_status.discarded_trajectories, dynamic_status.discarded_trajectories);
		EXPECT_EQ(fixed_status.cumulative_worker_delta_entries, dynamic_status.cumulative_worker_delta_entries);
		EXPECT_EQ(fixed_status.merged_stream_fingerprint, dynamic_status.merged_stream_fingerprint);
		EXPECT_EQ(dynamic_status.merged_stream_fingerprint, repeated_status.merged_stream_fingerprint);
		expect_same_storage(fixed.trainer.checkpoint().coordinator, dynamic.trainer.checkpoint().coordinator);
		expect_same_storage(dynamic.trainer.checkpoint().coordinator, repeated_dynamic.trainer.checkpoint().coordinator);
		expect_same_policy_rows(fixed.trainer.export_full_policy(), dynamic.trainer.export_full_policy());
		expect_same_policy_rows(dynamic.trainer.export_full_policy(), repeated_dynamic.trainer.export_full_policy());
	}
}

TEST_CASE(multiway_parallel_training_dynamic_chunk_sizes_match_fixed_merge)
{
	for (const std::uint32_t chunk_size : { 1U, 2U, 4U, 8U, 16U })
	{
		ParallelTrainingFixture fixed(4U, 256U, 2048U, false, texas::MultiwaySchedulerMode::Fixed);
		ParallelTrainingFixture dynamic(4U, 256U, 2048U, false, texas::MultiwaySchedulerMode::Dynamic, chunk_size);
		EXPECT_EQ(dynamic.runner.dynamic_chunk_size(), chunk_size);

		fixed.trainer.run_batches(3U, 32U, 0x77U);
		dynamic.trainer.run_batches(3U, 32U, 0x77U);

		const auto& fixed_status = fixed.trainer.status();
		const auto& dynamic_status = dynamic.trainer.status();
		EXPECT_EQ(dynamic_status.trajectories, fixed_status.trajectories);
		EXPECT_EQ(dynamic_status.discarded_trajectories, 0U);
		EXPECT_EQ(dynamic_status.merged_stream_fingerprint, fixed_status.merged_stream_fingerprint);
		expect_same_storage(fixed.trainer.checkpoint().coordinator, dynamic.trainer.checkpoint().coordinator);
		expect_same_policy_rows(fixed.trainer.export_full_policy(), dynamic.trainer.export_full_policy());
	}
}

TEST_CASE(multiway_parallel_training_all_schedulers_match_across_supported_workers)
{
	ParallelTrainingFixture reference(1U, 256U, 2048U, false, texas::MultiwaySchedulerMode::Fixed, 4U);
	reference.trainer.run_batches(2U, 32U, 0x77U);
	const auto expected = reference.trainer.checkpoint().coordinator;
	const auto expected_policy = reference.trainer.export_full_policy();
	for (const auto mode : { texas::MultiwaySchedulerMode::Fixed, texas::MultiwaySchedulerMode::StaticBlock, texas::MultiwaySchedulerMode::Dynamic })
	{
		for (const auto workers : { 1U, 2U, 4U, 8U, 16U })
		{
			ParallelTrainingFixture fixture(workers, 256U, 2048U, false, mode, 4U);
			fixture.trainer.run_batches(2U, 32U, 0x77U);
			expect_same_storage(expected, fixture.trainer.checkpoint().coordinator);
			expect_same_policy_rows(expected_policy, fixture.trainer.export_full_policy());
			const auto& status = fixture.trainer.status();
			EXPECT_EQ(status.scheduler_mode, mode);
			EXPECT_EQ(status.dynamic_chunk_size, 4U);
			EXPECT_TRUE(status.schedule_fingerprint != 0U);
			EXPECT_EQ(status.worker_trajectory_counts.size(), workers);
		}
	}
}

TEST_CASE(multiway_parallel_training_matches_one_worker_through_sixteen_workers)
{
	ParallelTrainingFixture reference(1U);
	reference.trainer.run_batches(3U, 32U, 0x77U);
	const auto expected = reference.trainer.checkpoint().coordinator;
	const auto expected_policy = reference.trainer.export_full_policy();
	for (const std::uint32_t workers : { 2U, 4U, 8U, 16U })
	{
		ParallelTrainingFixture parallel(workers);
		parallel.trainer.run_batches(3U, 32U, 0x77U);
		expect_same_storage(expected, parallel.trainer.checkpoint().coordinator);
		expect_same_policy_rows(expected_policy, parallel.trainer.export_full_policy());
		const auto& status = parallel.trainer.status();
		EXPECT_EQ(status.trajectories, 96U);
		EXPECT_EQ(status.discarded_trajectories, 0U);
		EXPECT_EQ(status.requested_worker_count, workers);
		EXPECT_EQ(status.effective_worker_count, workers);
		EXPECT_EQ(status.trajectories_per_batch, 32U);
		EXPECT_EQ(status.memory_preflight_estimate_bytes, 123456U);
		EXPECT_TRUE(status.peak_worker_delta_entries <= status.configured_worker_delta_capacity);
		EXPECT_TRUE(status.cumulative_worker_delta_entries >= status.peak_worker_delta_entries);
		EXPECT_TRUE(status.coordinator_wait_nanoseconds > 0U);
		EXPECT_EQ(status.worker_trajectory_counts.size(), workers);
		EXPECT_EQ(status.worker_active_nanoseconds_by_worker.size(), workers);
		std::uint64_t completed_trajectories = 0U;
		for (const auto count : status.worker_trajectory_counts)
			completed_trajectories += count;
		EXPECT_EQ(completed_trajectories, status.trajectories);
		std::uint64_t allocation_count = 0U;
		std::uint64_t allocation_bytes = 0U;
		for (std::size_t worker = 0U; worker < status.worker_allocation_counts.size(); ++worker)
		{
			allocation_count += status.worker_allocation_counts[worker];
			allocation_bytes += status.worker_allocated_bytes[worker];
		}
		EXPECT_EQ(status.memory_ledger.worker_allocation_count, allocation_count);
		EXPECT_EQ(status.memory_ledger.worker_allocation_bytes, allocation_bytes);
		EXPECT_TRUE(status.minimum_worker_active_nanoseconds <= status.maximum_worker_active_nanoseconds);
		EXPECT_TRUE(status.maximum_worker_active_nanoseconds > 0U);
		EXPECT_TRUE(status.barrier_wait_nanoseconds <= status.coordinator_wait_nanoseconds);
	}
}

TEST_CASE(multiway_immutable_compact_training_matches_worker_partitions)
{
	ParallelTrainingFixture reference(1U, 256U, 2048U, true);
	reference.trainer.run_batches(2U, 32U, 0x77U);
	const auto expected = reference.trainer.checkpoint().coordinator;
	const auto expected_policy = reference.trainer.export_full_policy();

	ParallelTrainingFixture parallel(2U, 256U, 2048U, true);
	parallel.trainer.run_batches(2U, 32U, 0x77U);
	expect_same_storage(expected, parallel.trainer.checkpoint().coordinator);
	expect_same_policy_rows(expected_policy, parallel.trainer.export_full_policy());
}

TEST_CASE(multiway_parallel_training_resume_is_exact_at_fixed_worker_count)
{
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

TEST_CASE(multiway_parallel_training_rejects_checkpoint_from_different_worker_identity)
{
	ParallelTrainingFixture single(1U);
	single.trainer.run_batches(1U, 32U, 0x77U);
	ParallelTrainingFixture parallel(16U);
	EXPECT_THROW(parallel.trainer.resume_from_checkpoint(single.trainer.checkpoint()),
		std::invalid_argument);
}

TEST_CASE(multiway_parallel_runner_reports_true_per_worker_delta_high_water)
{
	ParallelTrainingFixture fixture(16U);
	const auto result = fixture.runner.run(0U, 4U, 0x77U);
	EXPECT_TRUE(result.clean);
	EXPECT_EQ(result.minimum_worker_trajectories, 0U);
	EXPECT_EQ(result.maximum_worker_trajectories, 1U);
	EXPECT_TRUE(result.maximum_worker_delta_entries <= result.delta_entries_merged);
	EXPECT_TRUE(result.maximum_worker_delta_entries < 2048U);
	EXPECT_TRUE(result.coordinator_wait_nanoseconds > 0U);
	EXPECT_EQ(result.worker_trajectory_counts.size(), 16U);
	EXPECT_EQ(result.worker_active_nanoseconds_by_worker.size(), 16U);
	EXPECT_TRUE(result.minimum_worker_active_nanoseconds <= result.maximum_worker_active_nanoseconds);
}

TEST_CASE(multiway_parallel_runner_small_batch_waits_only_for_active_workers)
{
	ParallelTrainingFixture fixture(16U);
	const auto result = fixture.runner.run(0U, 1U, 0x77U);
	EXPECT_TRUE(result.clean);
	EXPECT_EQ(result.minimum_worker_trajectories, 0U);
	EXPECT_EQ(result.maximum_worker_trajectories, 1U);
	EXPECT_EQ(result.trajectories_attempted, 1U);
	EXPECT_EQ(result.worker_trajectory_counts.size(), 16U);
	EXPECT_EQ(result.worker_trajectory_counts[0U], 1U);
}

TEST_CASE(multiway_dynamic_scheduler_exercises_skewed_chunk_assignment)
{
	ParallelTrainingFixture fixture(4U, 256U, 2048U, false, texas::MultiwaySchedulerMode::Dynamic);
	const auto result = fixture.runner.run(0U, 9U, 0x77U);
	EXPECT_TRUE(result.clean);
	EXPECT_EQ(result.trajectories_attempted, 9U);
	EXPECT_EQ(result.trajectories_discarded, 0U);
	EXPECT_TRUE(result.maximum_worker_trajectories > result.minimum_worker_trajectories);
	EXPECT_EQ(result.worker_trajectory_counts.size(), 4U);
	EXPECT_EQ(result.worker_scheduler_claim_nanoseconds_by_worker.size(), 4U);
	std::uint64_t scheduler_claim_total = 0U;
	for (const auto value : result.worker_scheduler_claim_nanoseconds_by_worker)
		scheduler_claim_total += value;
	EXPECT_EQ(scheduler_claim_total, result.worker_scheduler_claim_nanoseconds);
}

TEST_CASE(multiway_parallel_runner_reports_affinity_fallback_and_resolved_processors)
{
	ParallelTrainingFixture fixture(
		2U, 256U, 2048U, false, texas::MultiwaySchedulerMode::Fixed,
		texas::MULTIWAY_DYNAMIC_SCHEDULER_CHUNK_SIZE, true);
	const auto result = fixture.runner.run(0U, 8U, 0x77U);
	const auto repeated = fixture.runner.run(8U, 8U, 0x77U);
	EXPECT_TRUE(result.clean);
	EXPECT_TRUE(repeated.clean);
	EXPECT_TRUE(result.affinity_requested);
	EXPECT_TRUE(repeated.affinity_requested);
	EXPECT_EQ(repeated.worker_requested_processor.size(), 2U);
	EXPECT_EQ(repeated.worker_resolved_processor.size(), 2U);
	EXPECT_EQ(repeated.worker_affinity_masks.size(), 2U);
	EXPECT_EQ(repeated.worker_affinity_succeeded.size(), 2U);
	for (std::size_t worker = 0U; worker < repeated.worker_affinity_succeeded.size(); ++worker)
	{
		if (repeated.worker_affinity_succeeded[worker] != 0U)
		{
			EXPECT_TRUE(repeated.worker_requested_processor[worker] != 0xffffffffU);
			EXPECT_TRUE(repeated.worker_resolved_processor[worker] != 0xffffffffU);
			EXPECT_EQ(repeated.worker_requested_processor[worker], repeated.worker_resolved_processor[worker]);
		}
	}
}

TEST_CASE(multiway_parallel_runner_profiles_traversal_hot_stages)
{
	ParallelTrainingFixture fixture(
		1U, 256U, 2048U, false, texas::MultiwaySchedulerMode::Fixed,
		texas::MULTIWAY_DYNAMIC_SCHEDULER_CHUNK_SIZE, false,
		texas::MultiwaySearchProfileMode::Checkpoints);
	const auto result = fixture.runner.run(0U, 8U, 0x77U);
	EXPECT_TRUE(result.clean);
	EXPECT_EQ(result.worker_profiles.size(), 1U);
	const auto& profile = result.worker_profiles[0U];
	EXPECT_TRUE(profile.profiled());
	EXPECT_TRUE(profile.checkpoint(texas::MultiwaySearchProfileStage::ActionMenuGeneration).calls > 0U);
	EXPECT_TRUE(profile.checkpoint(texas::MultiwaySearchProfileStage::RowLookup).calls > 0U);
	EXPECT_TRUE(profile.checkpoint(texas::MultiwaySearchProfileStage::ContinuationLeaf).calls > 0U);
}

TEST_CASE(multiway_blueprint_training_telemetry_retention_is_bounded)
{
	ParallelTrainingFixture fixture(1U);
	fixture.trainer.run_batches(260U, 1U, 0x77U);
	EXPECT_EQ(fixture.trainer.status().admitted_rows_per_batch.size(), 256U);
	EXPECT_EQ(fixture.trainer.status().admitted_rows_per_batch.back(),
		fixture.trainer.status().admitted_rows);
}

TEST_CASE(multiway_parallel_runner_propagates_worker_exceptions_and_joins)
{
	ParallelTrainingFixture fixture(4U, 1U);
	EXPECT_THROW(fixture.runner.run(0U, 4U, 0x77U), std::length_error);
}

TEST_CASE(multiway_parallel_runner_reports_worker_capacity_discards)
{
	ParallelTrainingFixture fixture(4U, 256U, 1U);
	const auto result = fixture.runner.run(0U, 8U, 0x77U);
	EXPECT_TRUE(!result.clean);
	EXPECT_TRUE(result.trajectories_discarded > 0U);
	EXPECT_TRUE(result.maximum_worker_delta_entries <= 1U);
}

TEST_CASE(multiway_topology_synthetic_records_have_unique_logical_identity)
{
	texas::solver::multiway::MultiwayWorkerTopologyRecord second;
	second.processor_group = 1U;
	second.logical_processor_index = 3U;
	texas::solver::multiway::MultiwayWorkerTopologyRecord first;
	first.processor_group = 0U;
	first.logical_processor_index = 7U;

	const auto inventory = texas::solver::multiway::make_multiway_worker_topology(
		{ second, first });
	EXPECT_EQ(inventory.processors.size(), 2U);
	EXPECT_TRUE(inventory.processors[0U].logical_processor_identity() != inventory.processors[1U].logical_processor_identity());
	EXPECT_TRUE(inventory.find(0U, 7U) != nullptr);
	EXPECT_TRUE(inventory.find(1U, 3U) != nullptr);
	EXPECT_TRUE(inventory.find(0U, 3U) == nullptr);

	second.logical_processor_index = 7U;
	second.processor_group = 0U;
	EXPECT_THROW(
		texas::solver::multiway::make_multiway_worker_topology({ first, second }),
		std::invalid_argument);
}

TEST_CASE(multiway_topology_synthetic_records_distinguish_physical_core_and_smt_sibling)
{
	texas::solver::multiway::MultiwayWorkerTopologyRecord sibling_zero;
	sibling_zero.logical_processor_index = 4U;
	sibling_zero.physical_core_id = 12U;
	sibling_zero.smt_sibling_index = 0U;
	texas::solver::multiway::MultiwayWorkerTopologyRecord sibling_one = sibling_zero;
	sibling_one.logical_processor_index = 5U;
	sibling_one.smt_sibling_index = 1U;
	texas::solver::multiway::MultiwayWorkerTopologyRecord other_core = sibling_zero;
	other_core.logical_processor_index = 6U;
	other_core.physical_core_id = 13U;
	other_core.smt_sibling_index = 0U;

	const auto inventory = texas::solver::multiway::make_multiway_worker_topology(
		{ sibling_one, other_core, sibling_zero });
	EXPECT_EQ(inventory.processors[0U].physical_core_id, 12U);
	EXPECT_EQ(inventory.processors[1U].physical_core_id, 12U);
	EXPECT_EQ(inventory.processors[0U].smt_sibling_index, 0U);
	EXPECT_EQ(inventory.processors[1U].smt_sibling_index, 1U);
	EXPECT_TRUE(inventory.processors[0U].physical_core_id != inventory.processors[2U].physical_core_id);
}

TEST_CASE(multiway_topology_synthetic_records_preserve_numa_and_cache_domain_facts)
{
	texas::solver::multiway::MultiwayWorkerTopologyRecord record;
	record.processor_group = 2U;
	record.logical_processor_index = 9U;
	record.numa_node_id = 4U;
	record.cache_domain_id = 17U;

	const auto inventory = texas::solver::multiway::make_multiway_worker_topology({ record });
	const auto* found = inventory.find(2U, 9U);
	EXPECT_TRUE(found != nullptr);
	EXPECT_EQ(found->numa_node_id, 4U);
	EXPECT_EQ(found->cache_domain_id, 17U);
	EXPECT_TRUE(!inventory.has_complete_windows_topology());
}

TEST_CASE(multiway_topology_fallback_keeps_only_unique_logical_identities)
{
	const auto inventory = texas::solver::multiway::make_multiway_worker_topology_fallback(3U);
	EXPECT_TRUE(inventory.is_fallback());
	EXPECT_EQ(inventory.processors.size(), 3U);
	for (std::uint16_t logical = 0U; logical < 3U; ++logical)
	{
		const auto* record = inventory.find(0U, logical);
		EXPECT_TRUE(record != nullptr);
		EXPECT_EQ(record->cpu_set_id, texas::solver::multiway::kMultiwayUnknownTopologyId);
		EXPECT_EQ(record->physical_core_id, texas::solver::multiway::kMultiwayUnknownTopologyId);
		EXPECT_EQ(record->smt_sibling_index,
			texas::solver::multiway::kMultiwayUnknownTopologyIndex);
		EXPECT_EQ(record->numa_node_id,
			texas::solver::multiway::kMultiwayUnknownTopologyIndex);
		EXPECT_EQ(record->cache_domain_id,
			texas::solver::multiway::kMultiwayUnknownTopologyIndex);
	}
}

TEST_CASE(multiway_placement_os_default_does_not_request_affinity)
{
	using namespace texas::solver::multiway;
	const auto inventory = make_multiway_worker_topology({ MultiwayWorkerTopologyRecord{ 1U, 0U, 0U, 10U, 0U },
		MultiwayWorkerTopologyRecord{ 2U, 0U, 1U, 10U, 1U } });
	const auto plan = resolve_multiway_worker_placement(
		inventory, 2U, MultiwayWorkerPlacementPolicy::OsDefault, 0U);
	EXPECT_EQ(plan.requested.size(), 0U);
	EXPECT_EQ(plan.resolved_worker_count, 0U);
}

TEST_CASE(multiway_placement_physical_policies_use_unique_cores_before_siblings)
{
	using namespace texas::solver::multiway;
	std::vector<MultiwayWorkerTopologyRecord> records;
	for (std::uint16_t core = 10U; core < 14U; ++core)
	{
		for (std::uint16_t sibling = 0U; sibling < 2U; ++sibling)
		{
			records.push_back(MultiwayWorkerTopologyRecord{
				static_cast<std::uint32_t>(core * 2U + sibling), 0U,
				static_cast<std::uint16_t>((core - 10U) * 2U + sibling), core, sibling, 0U, core / 2U });
		}
	}
	const auto inventory = make_multiway_worker_topology(std::move(records));
	for (const auto policy : { MultiwayWorkerPlacementPolicy::PhysicalCompact,
			 MultiwayWorkerPlacementPolicy::PhysicalSpread, MultiwayWorkerPlacementPolicy::CpuSetHint,
			 MultiwayWorkerPlacementPolicy::DiagnosticHardAffinity })
	{
		const auto plan = resolve_multiway_worker_placement(inventory, 4U, policy, kMultiwayUnknownTopologyId);
		EXPECT_EQ(plan.requested.size(), 4U);
		for (std::size_t first = 0U; first < plan.requested.size(); ++first)
			for (std::size_t second = first + 1U; second < plan.requested.size(); ++second)
				EXPECT_TRUE(plan.requested[first].physical_core_id != plan.requested[second].physical_core_id);

		const auto with_siblings = resolve_multiway_worker_placement(inventory, 6U, policy, kMultiwayUnknownTopologyId);
		EXPECT_EQ(with_siblings.requested.size(), 6U);
		EXPECT_EQ(with_siblings.requested[4U].physical_core_id, with_siblings.requested[0U].physical_core_id);
	}
}

TEST_CASE(multiway_placement_coordinator_reserves_entire_physical_core)
{
	using namespace texas::solver::multiway;
	const auto inventory = make_multiway_worker_topology({ MultiwayWorkerTopologyRecord{ 100U, 0U, 0U, 10U, 0U },
		MultiwayWorkerTopologyRecord{ 101U, 0U, 1U, 10U, 1U },
		MultiwayWorkerTopologyRecord{ 102U, 0U, 2U, 11U, 0U },
		MultiwayWorkerTopologyRecord{ 103U, 0U, 3U, 11U, 1U },
		MultiwayWorkerTopologyRecord{ 104U, 0U, 4U, 12U, 0U },
		MultiwayWorkerTopologyRecord{ 105U, 0U, 5U, 12U, 1U } });
	const auto plan = resolve_multiway_worker_placement(
		inventory, 2U, MultiwayWorkerPlacementPolicy::PhysicalCompact,
		inventory.processors[0U].logical_processor_identity());
	EXPECT_TRUE(plan.coordinator_reserved);
	EXPECT_EQ(plan.coordinator_physical_core_id, 10U);
	EXPECT_EQ(plan.requested.size(), 2U);
	for (const auto& record : plan.requested)
		EXPECT_TRUE(record.physical_core_id != 10U);
}

TEST_CASE(multiway_placement_fallback_is_deterministic_without_physical_identity)
{
	using namespace texas::solver::multiway;
	const auto inventory = make_multiway_worker_topology_fallback(4U);
	const auto plan = resolve_multiway_worker_placement(
		inventory, 3U, MultiwayWorkerPlacementPolicy::PhysicalCompact, 0U);
	EXPECT_TRUE(!plan.topology_available);
	EXPECT_EQ(plan.requested.size(), 3U);
	EXPECT_EQ(plan.requested[0U].logical_processor_index, 1U);
	EXPECT_EQ(plan.requested[1U].logical_processor_index, 2U);
	EXPECT_EQ(plan.requested[2U].logical_processor_index, 3U);
	for (const auto& record : plan.requested)
		EXPECT_EQ(record.physical_core_id, kMultiwayUnknownTopologyId);
}

TEST_CASE(multiway_placement_reports_cpu_set_and_hard_affinity_requests)
{
	using namespace texas::solver::multiway;
	const auto inventory = make_multiway_worker_topology({ MultiwayWorkerTopologyRecord{ 50U, 0U, 0U, 10U, 0U },
		MultiwayWorkerTopologyRecord{ 51U, 0U, 1U, 11U, 0U },
		MultiwayWorkerTopologyRecord{ 52U, 0U, 2U, 12U, 0U } });
	const auto cpu_sets = resolve_multiway_worker_placement(
		inventory, 2U, MultiwayWorkerPlacementPolicy::CpuSetHint, kMultiwayUnknownTopologyId);
	const auto hard_affinity = resolve_multiway_worker_placement(
		inventory, 2U, MultiwayWorkerPlacementPolicy::DiagnosticHardAffinity, kMultiwayUnknownTopologyId);
	EXPECT_EQ(cpu_sets.requested[0U].cpu_set_id, 50U);
	EXPECT_EQ(hard_affinity.requested[0U].physical_core_id, 10U);
	EXPECT_TRUE(std::string(multiway_worker_placement_policy_name(cpu_sets.policy)) == "cpu-set-hint");
	EXPECT_TRUE(std::string(multiway_worker_placement_policy_name(hard_affinity.policy)) == "diagnostic-hard-affinity");
}

TEST_CASE(multiway_blueprint_training_retains_failed_batch_telemetry_without_committing_it)
{
	ParallelTrainingFixture fixture(4U, 256U, 1U, true);
	const auto before = fixture.coordinator.checkpoint();
	EXPECT_THROW(fixture.trainer.run_batches(1U, 8U, 0x77U), std::runtime_error);
	const auto& status = fixture.trainer.status();
	EXPECT_EQ(status.batches, 0U);
	EXPECT_EQ(status.trajectories, 0U);
	EXPECT_TRUE(status.failed_batch_available);
	EXPECT_EQ(status.failed_batch.trajectories_attempted, 8U);
	EXPECT_TRUE(status.failed_batch.trajectories_discarded > 0U);
	EXPECT_TRUE(status.failed_batch.capacity_exhausted);
	EXPECT_TRUE(status.failed_batch.regret_delta_capacity_exhausted);
	EXPECT_TRUE(status.failed_batch.worker_trajectory_counts.size() == 4U);
	const auto after = fixture.coordinator.checkpoint();
	EXPECT_EQ(after.storage.shapes.size(), before.storage.shapes.size());
	for (std::size_t index = 0U; index < before.storage.shapes.size(); ++index)
	{
		EXPECT_EQ(after.storage.shapes[index].infoset, before.storage.shapes[index].infoset);
		EXPECT_EQ(after.storage.shapes[index].bucket_count, before.storage.shapes[index].bucket_count);
		EXPECT_EQ(after.storage.shapes[index].action_count, before.storage.shapes[index].action_count);
	}
	EXPECT_EQ(after.storage.regrets, before.storage.regrets);
	EXPECT_EQ(after.storage.strategy_sums, before.storage.strategy_sums);
	EXPECT_EQ(after.storage.compact_regrets, before.storage.compact_regrets);
	EXPECT_EQ(after.storage.compact_strategy_masses, before.storage.compact_strategy_masses);
	EXPECT_EQ(after.terminal_visits, before.terminal_visits);
	EXPECT_EQ(after.leaf_visits, before.leaf_visits);
}

TEST_CASE(multiway_parallel_training_scaling_smoke)
{
	const auto* value = std::getenv("TEXASSOLVER_PARALLEL_SMOKE_BATCHES");
	const auto batches = value == nullptr || *value == '\0' ? 20U : std::stoull(value);
	for (const std::uint32_t workers : { 1U, 2U, 4U, 8U, 16U })
	{
		ParallelTrainingFixture fixture(workers);
		fixture.trainer.run_batches(1U, 32U, 0x77U);
		const auto started = std::chrono::steady_clock::now();
		fixture.trainer.run_batches(batches, 32U, 0x77U);
		const auto elapsed = std::chrono::duration_cast<std::chrono::nanoseconds>(
			std::chrono::steady_clock::now() - started)
								 .count();
		std::cout << "workers=" << workers
				  << " trajectories=" << batches * 32U
				  << " elapsed_nanoseconds=" << elapsed
				  << " trajectories_per_second="
				  << (elapsed == 0 ? 0.0 : static_cast<double>(batches * 32U) * 1.0e9 / static_cast<double>(elapsed)) << '\n';
	}
}
