#include "solver/multiway/blueprint/multiway_blueprint_trainer.hpp"
#include "solver/multiway/blueprint/multiway_checkpoint.hpp"
#include "solver/multiway/blueprint/multiway_training_checkpoint_artifact.hpp"
#include "solver/multiway/abstraction/multiway_action_abstraction.hpp"
#include "solver/multiway/abstraction/multiway_bucket_model.hpp"
#include "solver/multiway/abstraction/multiway_public_builder.hpp"
#include "solver/multiway/engine/multiway_traversal.hpp"
#include "test_harness.hpp"

#include <stdexcept>
#include <cstddef>
#include <utility>
#include <limits>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>

namespace
{

	constexpr std::uint8_t card(std::uint8_t rank, std::uint8_t suit)
	{
		return texas::card_to_int(rank, suit);
	}

	const std::vector<std::uint8_t> kExportBoard = {
		card(2U, 0U), card(7U, 1U), card(9U, 2U), card(4U, 3U), card(6U, 0U)
	};

	std::vector<std::uint32_t> export_bucket_assignments()
	{
		std::vector<std::uint32_t> result(texas::MULTIWAY_HOLE_COMBINATION_COUNT, 0U);
		for (std::uint8_t first = 0U; first < 52U; ++first)
			for (std::uint8_t second = static_cast<std::uint8_t>(first + 1U); second < 52U; ++second)
				for (const auto board_card : kExportBoard)
					if (first == board_card || second == board_card)
						result[texas::MultiwayBucketTable::hole_index({ first, second })] = texas::MULTIWAY_INVALID_BUCKET;
		return result;
	}

	texas::MultiwayRootSnapshot export_root(const texas::MultiwayActionAbstraction& abstraction)
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
			betting, kExportBoard, abstraction.make_legal_actions(betting));
		root.root_infoset = { root.public_state.id, 0 };
		root.root_bucket = 0U;
		root.seat_order = { 0, 1, 2 };
		root.next_street_first_seat = 0;
		root.odd_chip_first_seat = 0;
		root.private_ranges.board = kExportBoard;
		root.private_ranges.ranges.resize(3U);
		root.private_ranges.ranges[0U] = { { { card(14U, 0U), card(13U, 0U) }, 1.0 } };
		root.private_ranges.ranges[1U] = { { { card(12U, 0U), card(11U, 0U) }, 1.0 } };
		root.private_ranges.ranges[2U] = { { { card(10U, 0U), card(8U, 0U) }, 1.0 } };
		root.action_abstraction_version = 1U;
		root.leaf_model_version = 1U;
		return root;
	}

	texas::Value export_leaf(const texas::MultiwayLeafEvaluationRequest& request, const void*) noexcept
	{
		const auto traverser = static_cast<std::size_t>(request.traverser);
		return static_cast<texas::Value>(request.betting->contributions[traverser] - request.betting->current_bet);
	}

	texas::MultiwaySolverLimits export_limits(texas::MultiwaySolverLimits::StorageBackend backend)
	{
		texas::MultiwaySolverLimits limits;
		limits.worker_count = 1U;
		limits.trajectories_per_batch = 4U;
		limits.max_public_states = 128U;
		limits.max_sparse_rows = 128U;
		limits.max_sparse_values = 1024U;
		limits.max_worker_delta_entries = 2048U;
		limits.max_discovery_entries = 8192U;
		limits.max_batches = 32U;
		limits.storage_backend = backend;
		return limits;
	}

	texas::MultiwayModelIdentity export_identity(texas::MultiwaySolverLimits::StorageBackend backend)
	{
		texas::MultiwayBlueprintTrainingConfig config;
		config.limits = export_limits(backend);
		return config.identity();
	}

	struct StreamingExportFixture
	{
		texas::MultiwayActionAbstraction abstraction;
		texas::MultiwayRootSnapshot root;
		texas::MultiwaySolveRequest request;
		texas::MultiwaySolverCoordinator coordinator;
		texas::MultiwayBucketRegistry buckets;
		texas::MultiwayLeafEvaluator evaluator;
		texas::MultiwayRootExternalSamplingTraversal traversal;
		texas::MultiwayRootBatchRunner runner;
		texas::MultiwayBlueprintTrainer trainer;

		explicit StreamingExportFixture(
			texas::MultiwaySolverLimits::StorageBackend backend,
			texas::MultiwayWorkingSetMonitor monitor = {})
			: root(export_root(abstraction)),
			  request(root, [] { texas::MultiwayCFRConfig cfr; cfr.player_count = 3U; return cfr; }(), export_limits(backend)),
			  coordinator(request),
			  buckets({ texas::MultiwayBucketTable(export_identity(backend), texas::Street::River, kExportBoard, 1U, export_bucket_assignments()) }),
			  evaluator{ export_leaf, nullptr },
			  traversal(coordinator, request.root(), abstraction, buckets, &evaluator, 1U),
			  runner(traversal, coordinator, 1U, texas::MultiwayWorkerStreamCapacities{ 2048U, 2048U, 8192U, 8192U }),
			  trainer(export_identity(backend), runner, coordinator, {}, 0x77U, 1U,
				0U, {}, {}, {}, nullptr, nullptr, monitor)
		{
		}
	};

	struct ExportProcessObserverState
	{
		std::uint64_t before_bytes = 100U;
		std::uint64_t after_bytes = 150U;
		std::uint32_t calls = 0U;
	};

	texas::MultiwayProcessMemorySnapshot export_process_observer(const void* context) noexcept
	{
		auto& state = *static_cast<ExportProcessObserverState*>(const_cast<void*>(context));
		texas::MultiwayProcessMemorySnapshot snapshot;
		snapshot.current_working_set_available = true;
		snapshot.private_committed_available = true;
		snapshot.current_working_set_bytes = state.calls++ == 0U ? state.before_bytes : state.after_bytes;
		snapshot.private_committed_bytes = snapshot.current_working_set_bytes;
		return snapshot;
	}

	std::string read_bytes(const std::filesystem::path& path)
	{
		std::ifstream input(path, std::ios::binary);
		return { std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>() };
	}

}

TEST_CASE(multiway_training_config_binds_default_rules_and_artifacts)
{
	texas::MultiwayBlueprintTrainingConfig config;
	config.validate();
	EXPECT_EQ(config.max_decision_depth, texas::MULTIWAY_MAX_DECISION_DEPTH);
	EXPECT_EQ(config.max_public_chance_depth, texas::MULTIWAY_MAX_PUBLIC_CHANCE_DEPTH);
	EXPECT_TRUE(!config.pin_workers);
	const auto identity = config.identity();
	EXPECT_TRUE(identity.combined_hash != 0U);
	config.pin_workers = true;
	EXPECT_EQ(config.identity().combined_hash, identity.combined_hash);

	config.action_abstraction.first_bet_basis_points[0] = 3400U;
	EXPECT_TRUE(config.identity() != identity);

	config.blueprint.turn_bucket_count = 127U;
	EXPECT_THROW(config.validate(), std::invalid_argument);
}

TEST_CASE(multiway_training_identity_binds_storage_backend)
{
	texas::MultiwayBlueprintTrainingConfig reference;
	auto compact = reference;
	compact.limits.storage_backend = texas::MultiwaySolverLimits::StorageBackend::CompactInt32;
	EXPECT_TRUE(reference.identity() != compact.identity());
}

TEST_CASE(multiway_training_identity_ignores_run_horizon_for_resume)
{
	texas::MultiwayBlueprintTrainingConfig reference;
	auto resumed = reference;
	resumed.limits.max_batches = reference.limits.max_batches + 7U;
	EXPECT_EQ(reference.identity().combined_hash, resumed.identity().combined_hash);
}

TEST_CASE(multiway_initial_blueprint_root_is_six_player_preflop_state)
{
	texas::MultiwayPrivateConfig ranges;
	ranges.ranges.resize(6U);
	for (std::size_t seat = 0; seat < 6U; ++seat)
	{
		ranges.ranges[seat] = { { { texas::card_to_int(static_cast<std::uint8_t>(14U - seat), 0),
									  texas::card_to_int(static_cast<std::uint8_t>(13U - seat), 1) },
			1.0 } };
	}
	texas::MultiwayActionAbstraction abstraction;
	const auto root = texas::make_multiway_initial_blueprint_root(
		texas::MultiwayGameRules::standard_6max(), std::move(ranges), abstraction, 1U, 1U);
	EXPECT_EQ(root.public_state.betting.street, texas::Street::Preflop);
	EXPECT_EQ(root.seat_order.size(), std::size_t{ 6 });
	EXPECT_EQ(root.root_infoset.seat, root.public_state.betting.current_player);
	EXPECT_TRUE(!root.public_state.legal_actions.empty());
}

TEST_CASE(multiway_training_schedule_controls_linear_weighting_and_pruning)
{
	texas::MultiwayBlueprintIterationSchedule schedule;
	EXPECT_EQ(schedule.strategy_weight(1U), 1.0);
	EXPECT_EQ(schedule.strategy_weight(3U), 3.0);

	schedule.linear_iteration_weighting = false;
	EXPECT_EQ(schedule.strategy_weight(3U), 1.0);
	schedule.prune_negative_regrets = true;
	schedule.pruning_warmup_batches = 4U;
	schedule.validate();
	EXPECT_TRUE(schedule.identity() != 0U);
}

TEST_CASE(multiway_training_schedule_rejects_invalid_pruning_controls)
{
	texas::MultiwayBlueprintIterationSchedule schedule;
	schedule.pruning_threshold = 0.1;
	EXPECT_THROW(schedule.validate(), std::invalid_argument);
	schedule.pruning_threshold = 0.0;
	schedule.pruning_exploration_probability = 1.1;
	EXPECT_THROW(schedule.validate(), std::invalid_argument);
	schedule.pruning_exploration_probability = 0.05;
	schedule.pruning_action_probability_threshold = 0.25;
	schedule.validate();
}

TEST_CASE(multiway_traversal_pruning_policy_covers_warmup_recovery_and_exemptions)
{
	texas::MultiwayTraversalPruningConfig policy;
	policy.enabled = true;
	policy.warmup_batches = 4U;
	policy.recovery_interval_batches = 20U;
	EXPECT_TRUE(!policy.should_explore_action(4U, true, false, false));
	EXPECT_TRUE(policy.should_explore_action(5U, true, false, false));
	EXPECT_TRUE(!policy.should_explore_action(5U, false, false, false));
	EXPECT_TRUE(policy.recovery_batch(20U));
	EXPECT_TRUE(policy.should_explore_action(20U, false, false, false));
	EXPECT_TRUE(!policy.should_explore_action(21U, true, true, false));
	EXPECT_TRUE(!policy.should_explore_action(21U, true, false, true));
}

TEST_CASE(multiway_checkpoint_resume_identity_rejects_different_artifacts)
{
	texas::MultiwayBlueprintTrainingConfig config;
	const auto expected = config.identity();
	auto snapshot = texas::MultiwayBlueprintSnapshot{};
	snapshot.identity = expected;
	snapshot.public_state = { 7U };
	snapshot.infoset = { { 7U }, 0 };
	snapshot.bucket = 0U;
	snapshot.trajectories = 3U;
	snapshot.training.trajectories = 3U;
	snapshot.actions = { { { texas::MultiwayAction::Check, 0U, 0, 11U }, 65535U } };

	texas::MultiwayRootPolicyArtifact::validate_resume_identity(snapshot, expected);
	++config.blueprint.bucket_model_version;
	EXPECT_THROW(
		texas::MultiwayRootPolicyArtifact::validate_resume_identity(snapshot, config.identity()),
		std::invalid_argument);
}

TEST_CASE(multiway_full_checkpoint_rejects_mismatched_sparse_value_shapes)
{
	texas::MultiwayBlueprintTrainingCheckpoint checkpoint;
	texas::MultiwayBlueprintConfig config;
	checkpoint.identity = texas::make_multiway_model_identity(config);
	checkpoint.coordinator.storage.shapes.push_back({ { { 1U }, 0 }, 2U, 2U });
	checkpoint.coordinator.storage.regrets.resize(3U);
	checkpoint.coordinator.storage.strategy_sums.resize(3U);
	EXPECT_THROW(checkpoint.validate(), std::invalid_argument);
}

TEST_CASE(multiway_full_checkpoint_rejects_non_finite_accumulators)
{
	texas::MultiwayBlueprintTrainingCheckpoint checkpoint;
	texas::MultiwayBlueprintConfig config;
	checkpoint.identity = texas::make_multiway_model_identity(config);
	checkpoint.coordinator.storage.shapes.push_back({ { { 1U }, 0 }, 1U, 1U });
	checkpoint.coordinator.storage.regrets.push_back(std::numeric_limits<double>::quiet_NaN());
	checkpoint.coordinator.storage.strategy_sums.push_back(0.0);
	EXPECT_THROW(checkpoint.validate(), std::invalid_argument);
}

TEST_CASE(multiway_training_metadata_persists_policy_variant_contract)
{
	texas::MultiwayBlueprintTrainingMetadata metadata;
	metadata.trajectories = 11U;
	metadata.batches = 2U;
	metadata.deterministic_seed = 9U;
	metadata.schedule_hash = 1U;

	texas::MultiwayBlueprintTrainingConfig config;
	texas::MultiwayBlueprintSnapshot snapshot;
	snapshot.identity = config.identity();
	snapshot.public_state = { 3U };
	snapshot.infoset = { { 3U }, 0 };
	snapshot.trajectories = metadata.trajectories;
	snapshot.policy_kind = texas::MultiwayBlueprintPolicyKind::LateWindowAverage;
	snapshot.training = metadata;
	snapshot.actions = { { { texas::MultiwayAction::Check, 0U, 0, 5U }, 65535U } };
	snapshot.validate();
	EXPECT_EQ(snapshot.policy_kind, texas::MultiwayBlueprintPolicyKind::LateWindowAverage);
}

TEST_CASE(multiway_full_training_checkpoint_round_trips_and_rejects_tampering)
{
	texas::MultiwayBlueprintTrainingConfig config;
	texas::MultiwayBlueprintTrainingCheckpoint checkpoint;
	checkpoint.identity = config.identity();
	checkpoint.training.batches = 2U;
	checkpoint.training.trajectories = 4U;
	checkpoint.training.deterministic_seed = config.deterministic_seed;
	checkpoint.training.schedule_hash = config.schedule.identity();
	checkpoint.coverage.street_visits = { 1U, 2U, 3U, 4U };
	checkpoint.coordinator.street_visits = checkpoint.coverage.street_visits;
	checkpoint.telemetry.payload_hash_nanoseconds = 999U;
	checkpoint.telemetry.public_state_bytes = 888U;
	checkpoint.validate();
	const auto path = std::filesystem::temp_directory_path() / ("texas_solver_training_checkpoint_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + ".bin");
	texas::MultiwayCheckpointTelemetry telemetry;
	texas::MultiwayTrainingCheckpointArtifacts::save_atomic(path, checkpoint, &telemetry);
	EXPECT_TRUE(telemetry.public_state_bytes != 0U);
	EXPECT_TRUE(telemetry.row_shape_bytes != 0U);
	EXPECT_TRUE(telemetry.regret_bytes != 0U);
	EXPECT_TRUE(telemetry.strategy_sum_bytes != 0U);
	EXPECT_TRUE(telemetry.late_window_baseline_bytes != 0U);
	const auto loaded = texas::MultiwayTrainingCheckpointArtifacts::load_verified(
		path, checkpoint.identity, checkpoint.training.schedule_hash, checkpoint.training.deterministic_seed);
	EXPECT_EQ(loaded.training.trajectories, checkpoint.training.trajectories);
	EXPECT_EQ(loaded.coverage.street_visits[2U], 3U);
	EXPECT_EQ(loaded.telemetry.payload_hash_nanoseconds, 0U);
	EXPECT_EQ(loaded.telemetry.public_state_bytes, 0U);
	EXPECT_EQ(texas::MultiwayTrainingCheckpointArtifacts::payload_hash(loaded),
		texas::MultiwayTrainingCheckpointArtifacts::payload_hash(checkpoint));
	{
		std::ofstream output(path, std::ios::binary | std::ios::app);
		output.put('\0');
	}
	EXPECT_THROW(texas::MultiwayTrainingCheckpointArtifacts::load_verified(
					 path, checkpoint.identity, checkpoint.training.schedule_hash, checkpoint.training.deterministic_seed),
		std::runtime_error);
	std::filesystem::remove(path);
}

TEST_CASE(multiway_streaming_export_matches_materialized_compact_bytes_and_repeats)
{
	StreamingExportFixture fixture(texas::MultiwaySolverLimits::StorageBackend::CompactInt32);
	fixture.trainer.run_batches(1U, 4U, 0x77U);
	const auto materialized = fixture.trainer.export_full_policy();
	const auto base = std::filesystem::temp_directory_path() /
		("texas_solver_streaming_export_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
	const auto streamed_path = base.string() + ".streamed";
	const auto materialized_path = base.string() + ".materialized";
	const auto first = fixture.trainer.save_full_policy_atomic(streamed_path);
	texas::MultiwayFullBlueprintArtifacts::save_atomic(materialized_path, materialized);
	EXPECT_TRUE(first.plan.compact_direct_order);
	EXPECT_EQ(first.plan.order_index_bytes, 0U);
	EXPECT_EQ(first.plan.row_count, static_cast<std::uint64_t>(materialized.rows.size()));
	EXPECT_EQ(first.payload_hash, materialized.payload_hash);
	EXPECT_EQ(read_bytes(streamed_path), read_bytes(materialized_path));
	const auto first_bytes = read_bytes(streamed_path);
	const auto second = fixture.trainer.save_full_policy_atomic(streamed_path);
	EXPECT_EQ(second.payload_hash, first.payload_hash);
	EXPECT_EQ(read_bytes(streamed_path), first_bytes);
	std::filesystem::remove(streamed_path);
	std::filesystem::remove(materialized_path);
}

TEST_CASE(multiway_streaming_export_float64_uses_bounded_order_indexes_and_matches_bytes)
{
	StreamingExportFixture fixture(texas::MultiwaySolverLimits::StorageBackend::Float64Reference);
	fixture.trainer.run_batches(1U, 4U, 0x77U);
	const auto materialized = fixture.trainer.export_full_policy();
	const auto base = std::filesystem::temp_directory_path() /
		("texas_solver_streaming_float64_export_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
	const auto streamed_path = base.string() + ".streamed";
	const auto materialized_path = base.string() + ".materialized";
	const auto streamed = fixture.trainer.save_full_policy_atomic(streamed_path);
	texas::MultiwayFullBlueprintArtifacts::save_atomic(materialized_path, materialized);
	EXPECT_TRUE(!streamed.plan.compact_direct_order);
	EXPECT_EQ(streamed.plan.order_index_bytes, streamed.plan.shape_count * sizeof(std::uint32_t));
	EXPECT_EQ(streamed.payload_hash, materialized.payload_hash);
	EXPECT_EQ(read_bytes(streamed_path), read_bytes(materialized_path));
	std::filesystem::remove(streamed_path);
	std::filesystem::remove(materialized_path);
}

TEST_CASE(multiway_streaming_export_process_gate_rejects_before_temp_creation)
{
	texas::MultiwayWorkingSetMonitor monitor;
	monitor.export_gate.process.hard_limit_bytes = 1U;
	monitor.export_gate.process.exporter_headroom_bytes = 0U;
	monitor.export_gate.process.encoder_buffer_bytes = 0U;
	StreamingExportFixture fixture(texas::MultiwaySolverLimits::StorageBackend::CompactInt32, monitor);
	fixture.trainer.run_batches(1U, 4U, 0x77U);
	const auto path = std::filesystem::temp_directory_path() /
		("texas_solver_streaming_gate_reject_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
	{
		std::ofstream previous(path, std::ios::binary);
		previous << "previous-blueprint";
	}
	std::filesystem::remove(path.string() + ".tmp");
	EXPECT_THROW(fixture.trainer.save_full_policy_atomic(path), std::runtime_error);
	EXPECT_EQ(read_bytes(path), std::string("previous-blueprint"));
	EXPECT_TRUE(!std::filesystem::exists(path.string() + ".tmp"));
	EXPECT_EQ(fixture.trainer.status().export_telemetry.process_gate.rejection_reason,
		texas::MultiwayExportProcessRejectionReason::ProcessLimitExceeded);
	std::filesystem::remove(path);
}

TEST_CASE(multiway_streaming_export_records_fresh_memory_telemetry)
{
	ExportProcessObserverState observer_state;
	texas::MultiwayWorkingSetMonitor monitor;
	monitor.export_gate.process.hard_limit_bytes = 10'000'000U;
	monitor.export_gate.process.exporter_headroom_bytes = 0U;
	monitor.export_gate.process.encoder_buffer_bytes = 0U;
	monitor.export_process_observer = export_process_observer;
	monitor.export_observer_context = &observer_state;
	StreamingExportFixture fixture(texas::MultiwaySolverLimits::StorageBackend::CompactInt32, monitor);
	fixture.trainer.run_batches(1U, 4U, 0x77U);
	const auto path = std::filesystem::temp_directory_path() /
		("texas_solver_streaming_gate_telemetry_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
	const auto result = fixture.trainer.save_full_policy_atomic(path);
	const auto& telemetry = fixture.trainer.status().export_telemetry;
	EXPECT_EQ(observer_state.calls, 2U);
	EXPECT_TRUE(telemetry.attempted);
	EXPECT_TRUE(telemetry.succeeded);
	EXPECT_EQ(telemetry.result_bytes, telemetry.plan.serialized_bytes);
	EXPECT_EQ(telemetry.payload_hash, result.payload_hash);
	EXPECT_EQ(telemetry.observed_delta.working_set_delta_bytes, 50U);
	EXPECT_EQ(telemetry.process_gate.status, texas::MultiwayExportGateStatus::Admitted);
	std::filesystem::remove(path);
}
