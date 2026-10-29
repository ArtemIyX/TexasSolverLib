#include "solver/multiway/blueprint/multiway_blueprint_trainer.hpp"
#include "solver/multiway/blueprint/multiway_checkpoint.hpp"
#include "solver/multiway/blueprint/multiway_training_checkpoint_artifact.hpp"
#include "solver/multiway/blueprint/multiway_blueprint_policy_provider.hpp"
#include "solver/multiway/abstraction/multiway_action_abstraction.hpp"
#include "solver/multiway/abstraction/multiway_bucket_model.hpp"
#include "solver/multiway/abstraction/multiway_public_builder.hpp"
#include "solver/multiway/engine/multiway_traversal.hpp"
#include "solver/multiway/engine/multiway_external_storage.hpp"
#include "solver/multiway/workflow/multiway_lookup_qualification.hpp"
#include "solver/multiway/workflow/multiway_pb12_coverage.hpp"
#include "test_harness.hpp"

#include <algorithm>
#include <array>
#include <stdexcept>
#include <cstddef>
#include <utility>
#include <limits>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <tuple>
#include <vector>

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

	texas::MultiwaySolverLimits export_limits(texas::MultiwaySolverLimits::StorageBackend backend,
		const std::filesystem::path& external_directory = {},
		std::uint64_t row_page_cache_bytes = 2U * 1024U * 1024U,
		const std::filesystem::path& identity_directory = {}, bool large = false)
	{
		texas::MultiwaySolverLimits limits;
		limits.worker_count = 1U;
		limits.trajectories_per_batch = 4U;
		limits.max_public_states = 128U;
		limits.max_sparse_rows = 128U;
		limits.max_sparse_values = large ? 8192U : 1024U;
		limits.max_worker_delta_entries = 2048U;
		limits.max_discovery_entries = 8192U;
		limits.max_batches = 32U;
		limits.storage_backend = backend;
		if (backend == texas::MultiwaySolverLimits::StorageBackend::ExternalCompactInt32)
		{
			limits.external_storage.directory = external_directory;
			limits.external_storage.identity_directory = identity_directory;
			limits.external_storage.public_catalog_cache_bytes = 4096U;
			limits.external_storage.row_page_cache_bytes = row_page_cache_bytes;
			limits.external_storage.maximum_wal_bytes = 1024U * 1024U;
			limits.external_storage.segment_target_bytes = 1024U * 1024U;
			limits.external_storage.compaction_fan_in = 4U;
			limits.external_storage.compaction_scratch_bytes = 1024U * 1024U;
			limits.external_storage.disk_reserve_bytes = 1024U * 1024U;
		}
		return limits;
	}

	texas::MultiwayModelIdentity export_identity(texas::MultiwaySolverLimits::StorageBackend backend,
		const std::filesystem::path& external_directory = {},
		std::uint64_t row_page_cache_bytes = 2U * 1024U * 1024U,
		const std::filesystem::path& identity_directory = {}, bool large = false)
	{
		texas::MultiwayBlueprintTrainingConfig config;
		config.limits = export_limits(backend, external_directory, row_page_cache_bytes, identity_directory, large);
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
			texas::MultiwayWorkingSetMonitor monitor = {},
			const std::filesystem::path& external_directory = {},
			std::uint64_t row_page_cache_bytes = 2U * 1024U * 1024U,
			const std::filesystem::path& identity_directory = {}, bool large = false)
			: root(export_root(abstraction)), request(root, [] { texas::MultiwayCFRConfig cfr; cfr.player_count = 3U; return cfr; }(), export_limits(backend, external_directory, row_page_cache_bytes, identity_directory, large)), coordinator(request), buckets({ texas::MultiwayBucketTable(export_identity(backend, external_directory, row_page_cache_bytes, identity_directory, large), texas::Street::River, kExportBoard, 1U, export_bucket_assignments()) }), evaluator{ export_leaf, nullptr }, traversal(coordinator, request.root(), abstraction, buckets, &evaluator, 1U), runner(traversal, coordinator, 1U, texas::MultiwayWorkerStreamCapacities{ 2048U, 2048U, 8192U, 8192U }), trainer(export_identity(backend, external_directory, row_page_cache_bytes, identity_directory, large), runner, coordinator, {}, 0x77U, 1U, 0U, {}, {}, {}, nullptr, nullptr, monitor)
		{
		}
	};

	struct ExportProcessObserverState
	{
		std::uint64_t before_bytes = 100U;
		std::uint64_t after_bytes = 150U;
		std::uint32_t calls = 0U;
	};

	struct ExportProgressState
	{
		std::uint64_t calls = 0U;
		std::uint64_t last_pages = 0U;
		std::uint64_t last_rows = 0U;
		std::uint64_t last_encoded_bytes = 0U;
		std::uint64_t last_writer_samples = 0U;
		std::uint64_t last_flushes = 0U;
		bool ordered = true;
	};

	void export_progress_observer(const texas::solver::multiway::MultiwayExternalExportProfile& profile,
		std::uint64_t, void* context) noexcept
	{
		auto& state = *static_cast<ExportProgressState*>(context);
		state.ordered = state.ordered && profile.pages == state.last_pages + 1U && profile.rows > state.last_rows && profile.encoded_bytes > state.last_encoded_bytes && profile.stream_write_bytes <= profile.encoded_bytes;
		++state.calls;
		state.last_pages = profile.pages;
		state.last_rows = profile.rows;
		state.last_encoded_bytes = profile.encoded_bytes;
		state.last_writer_samples = profile.writer_sampled_rows;
		state.last_flushes = profile.writer_flushes;
	}

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

	bool export_disk_space_observer(const void*, const std::filesystem::path&,
		std::uint64_t& available_bytes) noexcept
	{
		available_bytes = 1U;
		return true;
	}

	texas::MultiwaySystemMemorySnapshot rejected_system_memory_observer(const void*) noexcept
	{
		texas::MultiwaySystemMemorySnapshot snapshot;
		snapshot.total_physical_available = true;
		snapshot.available_physical_available = true;
		snapshot.total_physical_bytes = 64'000U;
		snapshot.available_physical_bytes = 1'499U;
		snapshot.used_physical_bytes = snapshot.total_physical_bytes - snapshot.available_physical_bytes;
		return snapshot;
	}

	std::string read_bytes(const std::filesystem::path& path)
	{
		std::ifstream input(path, std::ios::binary);
		return { std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>() };
	}

	std::vector<std::pair<std::string, std::string>> snapshot_tree_bytes(
		const std::filesystem::path& root)
	{
		std::vector<std::pair<std::string, std::string>> result;
		for (const auto& entry : std::filesystem::recursive_directory_iterator(root))
			if (entry.is_regular_file())
				result.emplace_back(entry.path().lexically_relative(root).generic_string(),
					read_bytes(entry.path()));
		std::sort(result.begin(), result.end(), [](const auto& left, const auto& right) {
			return left.first < right.first;
		});
		return result;
	}

	struct DisposableTestDirectory
	{
		std::filesystem::path path = std::filesystem::temp_directory_path()
			/ ("texas_solver_pb12_admission_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));

		~DisposableTestDirectory()
		{
			std::error_code error;
			std::filesystem::remove_all(path, error);
		}
	};

} // namespace

TEST_CASE(multiway_lookup_audit_queries_policy_for_first_local_admission)
{
	StreamingExportFixture fixture(texas::MultiwaySolverLimits::StorageBackend::CompactInt32);
	texas::MultiwayBlueprintTrainingConfig config;
	config.rules.player_count = 3U;
	config.blueprint.player_count = 3U;
	config.cfr.player_count = 3U;
	config.limits = export_limits(texas::MultiwaySolverLimits::StorageBackend::CompactInt32);

	texas::MultiwayBlueprintRow root_row;
	root_row.infoset = fixture.root.root_infoset;
	root_row.bucket = fixture.root.root_bucket;
	root_row.action_menu_id = fixture.root.public_state.legal_actions.front().action_menu_id;
	const auto action_count = fixture.root.public_state.legal_actions.size();
	const auto base_probability = static_cast<std::uint32_t>(65535U / action_count);
	const auto remainder = static_cast<std::uint32_t>(65535U % action_count);
	for (std::size_t index = 0U; index < action_count; ++index)
	{
		root_row.actions.push_back({ fixture.root.public_state.legal_actions[index],
			static_cast<std::uint16_t>(base_probability + (index < remainder ? 1U : 0U)) });
	}
	const texas::MultiwayBlueprintStore store(config.identity(), { std::move(root_row) });
	const texas::MultiwayBlueprintPolicyProvider provider(store);
	texas::MultiwaySolverCoordinator coordinator(fixture.request);
	texas::MultiwayRootExternalSamplingTraversal traversal(
		coordinator, fixture.request.root(), fixture.abstraction, fixture.buckets,
		&fixture.evaluator, 1U, 0U, &provider);
	texas::MultiwayWorkerDeltaStream stream(0U, fixture.request.limits().regret_delta_capacity());
	texas::MultiwayBlueprintLookupAudit audit;
	const auto accepted = traversal.run(1, 0U, 0x1234U, stream, 1.0, nullptr, nullptr, 0U, &audit);
	EXPECT_TRUE(accepted);
	EXPECT_TRUE(audit.lookup_hits > 0U);
}

void verify_pb12_targeted_coverage_admission(
	texas::MultiwaySolverLimits::StorageBackend backend)
{
	DisposableTestDirectory disposable;
	std::filesystem::create_directories(disposable.path);
	const auto store_path = disposable.path / "training-store";
	const auto identity_path = disposable.path / "training-store-identity";
	const auto policy_path = disposable.path / "targeted-policy.bin";
	const auto limits = export_limits(backend, store_path,
		2U * 1024U * 1024U, identity_path, true);
	const auto rules = texas::MultiwayGameRules::standard_6max();
	const texas::MultiwayActionAbstraction abstraction;

	texas::MultiwayPrivateConfig private_ranges;
	private_ranges.ranges.resize(6U);
	for (std::size_t seat = 0U; seat < private_ranges.ranges.size(); ++seat)
	{
		const auto rank = static_cast<std::uint8_t>(14U - seat);
		private_ranges.ranges[seat].push_back({ { card(rank, 0U), card(rank, 1U) }, 1.0 });
	}
	auto root = texas::make_multiway_initial_blueprint_root(
		rules, private_ranges, abstraction, 1U, 1U, 0);

	texas::MultiwayBlueprintTrainingConfig training_config;
	training_config.rules = rules;
	training_config.action_abstraction = {};
	training_config.cfr.player_count = 6U;
	training_config.limits = limits;
	training_config.max_decision_depth = 1U;
	training_config.max_public_chance_depth = 0U;
	const auto identity = training_config.identity();
	texas::MultiwayBucketRegistry buckets({ texas::MultiwayBucketTable(
		identity, texas::Street::River, kExportBoard, 1U, export_bucket_assignments()) });
	texas::MultiwayBlueprintTrainingSession session(training_config, root, buckets);
	const auto admission = session.admit_pb12_targeted_coverage();
	for (std::size_t index = 0U;
		index < texas::solver::multiway::MULTIWAY_PB12_COVERAGE_KEYS.size(); ++index)
	{
		const auto& key = texas::solver::multiway::MULTIWAY_PB12_COVERAGE_KEYS[index];
		const auto& state = admission.target_states[index];
		EXPECT_EQ(state.id.value, key.public_state_id);
		EXPECT_EQ(state.betting.current_player, key.seat);
		EXPECT_TRUE(!state.legal_actions.empty());
		EXPECT_EQ(state.legal_actions.front().action_menu_id, key.action_menu_id);
		EXPECT_TRUE(key.bucket < 169U);
	}
	const auto export_result = session.save_full_policy_atomic(policy_path);
	EXPECT_TRUE(std::filesystem::exists(policy_path));
	EXPECT_TRUE(export_result.plan.row_count > 0U);
	texas::verify_multiway_pb12_policy_file(policy_path, identity);
}

TEST_CASE(multiway_pb12_captured_preflop_misses_are_admitted_exported_and_replayed)
{
	verify_pb12_targeted_coverage_admission(
		texas::MultiwaySolverLimits::StorageBackend::ExternalCompactInt32);
}

TEST_CASE(multiway_pb12_captured_preflop_misses_are_admitted_to_compact_storage)
{
	verify_pb12_targeted_coverage_admission(
		texas::MultiwaySolverLimits::StorageBackend::CompactInt32);
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

TEST_CASE(multiway_external_snapshot_runtime_path_preserves_model_identity)
{
	using Backend = texas::MultiwaySolverLimits::StorageBackend;
	texas::MultiwayBlueprintTrainingConfig source;
	source.limits = export_limits(Backend::ExternalCompactInt32, "source-store");
	const auto source_identity = source.identity();

	auto isolated = source;
	isolated.limits.external_storage.identity_directory = source.limits.external_storage.directory;
	isolated.limits.external_storage.directory = "isolated-store-copy";
	EXPECT_EQ(isolated.identity(), source_identity);
}

TEST_CASE(multiway_external_training_checkpoint_resume_matches_uninterrupted_export)
{
	using namespace texas::solver::multiway;
	const auto base = std::filesystem::temp_directory_path() / ("texas_solver_external_training_resume_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
	const auto storage_directory = base / "storage";
	const auto isolated_storage_directory = base / "isolated-storage";
	const auto checkpoint_path = base / "latest.mwec";
	const auto policy_path = base / "resumed.mwfb";
	const auto reference_storage = base / "reference_storage";
	const auto reference_policy_path = base / "reference.mwfb";
	const auto backend = MultiwaySolverLimits::StorageBackend::ExternalCompactInt32;
	const auto identity = export_identity(backend, storage_directory);
	constexpr std::uint64_t workflow_hash = 0x7182U;
	constexpr std::uint64_t storage_hash = 0x8293U;
	MultiwayWorkingSetMonitor sharded_monitor;
	sharded_monitor.single_file_blueprint_limit_bytes = 1U;
	sharded_monitor.shard_max_serialized_bytes = 1024U * 1024U;
	sharded_monitor.profile_external_export = true;
	MultiwayExternalCheckpointManifest checkpoint;
	{
		StreamingExportFixture first(backend, {}, storage_directory);
		try
		{
			first.trainer.run_batches(1U, 4U, 0x77U);
			const auto external_telemetry = first.trainer.external_storage_telemetry();
			EXPECT_TRUE(external_telemetry.disk_usage.live_segment_bytes > 0U);
			EXPECT_TRUE(external_telemetry.stats.page_cache_misses > 0U);
			checkpoint = first.trainer.save_external_checkpoint_atomic(
				checkpoint_path, workflow_hash, storage_hash);
		}
		catch (const std::exception& error)
		{
			throw std::runtime_error(std::string("first external batch/checkpoint: ") + error.what());
		}
	}
	const auto source_store_before = snapshot_tree_bytes(storage_directory);
	std::filesystem::copy(storage_directory, isolated_storage_directory,
		std::filesystem::copy_options::recursive);
	MultiwayExternalCheckpointManifest restored;
	try
	{
		restored = restore_multiway_external_checkpoint(
			checkpoint_path, isolated_storage_directory, identity.combined_hash, workflow_hash,
			storage_hash, MultiwayBlueprintIterationSchedule{}.identity(), true);
	}
	catch (const std::exception& error)
	{
		throw std::runtime_error(std::string("external manifest restore: ") + error.what());
	}
	EXPECT_EQ(restored.completed_batches, 1U);
	EXPECT_EQ(restored.completed_trajectories, 4U);
	std::uint64_t resumed_policy_hash = 0U;
	try
	{
		StreamingExportFixture resumed(backend, sharded_monitor, isolated_storage_directory,
			2U * 1024U * 1024U, storage_directory);
		try
		{
			resumed.trainer.resume_from_external_checkpoint(restored, workflow_hash, storage_hash);
		}
		catch (const std::exception& error)
		{
			throw std::runtime_error(std::string("resume: ") + error.what());
		}
		EXPECT_EQ(resumed.trainer.batches(), 1U);
		try
		{
			resumed.trainer.run_batches(1U, 4U, 0x77U);
		}
		catch (const std::exception& error)
		{
			throw std::runtime_error(std::string("resumed batch: ") + error.what());
		}
		MultiwayFullBlueprintExportResult output;
		try
		{
			output = resumed.trainer.save_full_policy_atomic(policy_path);
		}
		catch (const std::exception& error)
		{
			throw std::runtime_error(std::string("resumed export: ") + error.what());
		}
		EXPECT_TRUE(output.sharded);
		const auto& profile = resumed.trainer.status().export_telemetry.external_profile;
		EXPECT_TRUE(profile.enabled);
		EXPECT_EQ(profile.rows, output.plan.row_count);
		EXPECT_EQ(profile.actions, output.plan.action_count);
		EXPECT_EQ(profile.page_decoded_count + profile.page_zero_count,
			profile.page_store.page_misses);
		resumed_policy_hash = output.policy_content_hash;
	}
	catch (const std::exception& error)
	{
		throw std::runtime_error(std::string("external resume/export: ") + error.what());
	}
	std::uint64_t uninterrupted_policy_hash = 0U;
	try
	{
		StreamingExportFixture uninterrupted(backend, sharded_monitor, reference_storage);
		uninterrupted.trainer.run_batches(2U, 4U, 0x77U);
		const auto output = uninterrupted.trainer.save_full_policy_atomic(reference_policy_path);
		EXPECT_TRUE(output.sharded);
		uninterrupted_policy_hash = output.policy_content_hash;
	}
	catch (const std::exception& error)
	{
		throw std::runtime_error(std::string("external uninterrupted/export: ") + error.what());
	}
	EXPECT_EQ(resumed_policy_hash, uninterrupted_policy_hash);
	EXPECT_EQ(snapshot_tree_bytes(storage_directory), source_store_before);
	std::filesystem::remove_all(base);
}

TEST_CASE(multiway_external_profiled_resume_matches_unprofiled_bytes)
{
	using namespace texas::solver::multiway;
	const auto base = std::filesystem::temp_directory_path() / ("texas_solver_profiled_resume_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
	const auto storage = base / "storage";
	const auto checkpoint_path = base / "latest.mwec";
	const auto baseline_path = base / "baseline.mwfb";
	const auto profiled_path = base / "profiled.mwfb";
	const auto backend = MultiwaySolverLimits::StorageBackend::ExternalCompactInt32;
	const auto identity = export_identity(backend, storage);
	constexpr std::uint64_t workflow_hash = 0x7182U;
	constexpr std::uint64_t storage_hash = 0x8293U;
	std::uint64_t baseline_hash = 0U;
	{
		StreamingExportFixture first(backend, {}, storage);
		first.trainer.run_batches(1U, 4U, 0x77U);
		(void)first.trainer.save_external_checkpoint_atomic(checkpoint_path, workflow_hash, storage_hash);
		const auto result = first.trainer.save_full_policy_atomic(baseline_path);
		EXPECT_TRUE(!result.sharded);
		baseline_hash = result.policy_content_hash;
	}
	const auto checkpoint = restore_multiway_external_checkpoint(checkpoint_path, storage,
		identity.combined_hash, workflow_hash, storage_hash,
		MultiwayBlueprintIterationSchedule{}.identity(), true);
	{
		MultiwayWorkingSetMonitor monitor;
		monitor.profile_external_export = true;
		StreamingExportFixture resumed(backend, monitor, storage);
		resumed.trainer.resume_from_external_checkpoint(checkpoint, workflow_hash, storage_hash);
		const auto result = resumed.trainer.save_full_policy_atomic(profiled_path);
		EXPECT_TRUE(!result.sharded);
		EXPECT_TRUE(resumed.trainer.status().export_telemetry.external_profile.enabled);
		EXPECT_EQ(result.policy_content_hash, baseline_hash);
		EXPECT_EQ(read_bytes(profiled_path), read_bytes(baseline_path));
	}
	std::filesystem::remove_all(base);
}

TEST_CASE(multiway_training_identity_ignores_run_horizon_for_resume)
{
	texas::MultiwayBlueprintTrainingConfig reference;
	auto resumed = reference;
	resumed.limits.max_batches = reference.limits.max_batches + 7U;
	EXPECT_EQ(reference.identity().combined_hash, resumed.identity().combined_hash);
}

TEST_CASE(multiway_training_identity_binds_scheduler_policy_and_chunk_size)
{
	texas::MultiwayBlueprintTrainingConfig reference;
	const auto fixed = reference.identity();
	reference.scheduler_mode = texas::MultiwaySchedulerMode::StaticBlock;
	EXPECT_TRUE(reference.identity() != fixed);
	reference.scheduler_mode = texas::MultiwaySchedulerMode::Dynamic;
	reference.dynamic_chunk_size = 8U;
	EXPECT_TRUE(reference.identity() != fixed);
	const auto dynamic = reference.identity();
	reference.dynamic_chunk_size = 16U;
	EXPECT_TRUE(reference.identity() != dynamic);
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

TEST_CASE(multiway_streaming_checkpoint_matches_materialized_bytes_and_loader)
{
	for (const auto backend : { texas::MultiwaySolverLimits::StorageBackend::CompactInt32,
			 texas::MultiwaySolverLimits::StorageBackend::Float64Reference })
	{
		StreamingExportFixture fixture(backend);
		fixture.trainer.run_batches(1U, 4U, 0x77U);
		const auto checkpoint = fixture.trainer.checkpoint();
		const auto base = std::filesystem::temp_directory_path() / ("texas_solver_streaming_checkpoint_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
		const auto streamed_path = base.string() + ".streamed";
		const auto materialized_path = base.string() + ".materialized";
		const auto streamed = fixture.trainer.save_checkpoint_atomic(streamed_path);
		texas::MultiwayTrainingCheckpointArtifacts::save_atomic(materialized_path, checkpoint);
		EXPECT_EQ(streamed.plan.serialized_bytes, streamed.serialized_bytes);
		EXPECT_EQ(streamed.serialized_bytes, static_cast<std::uint64_t>(read_bytes(streamed_path).size()));
		EXPECT_EQ(streamed.payload_hash, texas::MultiwayTrainingCheckpointArtifacts::payload_hash(checkpoint));
		EXPECT_EQ(read_bytes(streamed_path), read_bytes(materialized_path));
		const auto loaded = texas::MultiwayTrainingCheckpointArtifacts::load_verified(
			streamed_path, checkpoint.identity, checkpoint.training.schedule_hash, checkpoint.training.deterministic_seed);
		EXPECT_EQ(texas::MultiwayTrainingCheckpointArtifacts::payload_hash(loaded), streamed.payload_hash);
		std::filesystem::remove(streamed_path);
		std::filesystem::remove(materialized_path);
	}
}

TEST_CASE(multiway_streaming_checkpoint_gate_rejects_before_temp_creation)
{
	texas::MultiwayWorkingSetMonitor monitor;
	monitor.export_gate.process.hard_limit_bytes = 1U;
	monitor.export_gate.process.exporter_headroom_bytes = 0U;
	monitor.export_gate.process.encoder_buffer_bytes = 0U;
	StreamingExportFixture fixture(texas::MultiwaySolverLimits::StorageBackend::CompactInt32, monitor);
	fixture.trainer.run_batches(1U, 4U, 0x77U);
	const auto path = std::filesystem::temp_directory_path() / ("texas_solver_streaming_checkpoint_gate_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
	EXPECT_THROW(fixture.trainer.save_checkpoint_atomic(path), std::runtime_error);
	EXPECT_TRUE(!std::filesystem::exists(path));
	EXPECT_TRUE(!std::filesystem::exists(path.string() + ".tmp"));
}

TEST_CASE(multiway_streaming_checkpoint_failures_keep_destination_and_remove_temp)
{
	StreamingExportFixture fixture(texas::MultiwaySolverLimits::StorageBackend::CompactInt32);
	fixture.trainer.run_batches(1U, 4U, 0x77U);
	const auto checkpoint = fixture.trainer.checkpoint();
	auto lease = fixture.coordinator.acquire_checkpoint_lease();
	texas::MultiwayTrainingCheckpointStreamSource source;
	source.identity = &checkpoint.identity;
	source.training = &checkpoint.training;
	source.coverage = &checkpoint.coverage;
	source.catalog = &lease.public_catalog();
	source.storage = &lease.storage();
	source.compact_storage = lease.compact_storage();
	source.late_window_baseline = &checkpoint.late_window_baseline;
	source.terminal_visits = checkpoint.coordinator.terminal_visits;
	source.leaf_visits = checkpoint.coordinator.leaf_visits;
	source.missing_lookup_requests = checkpoint.coordinator.missing_lookup_requests;
	source.last_merged_stream_fingerprint = checkpoint.coordinator.last_merged_stream_fingerprint;
	source.street_visits = checkpoint.coordinator.street_visits;
	const auto path = std::filesystem::temp_directory_path() / ("texas_solver_streaming_checkpoint_failure_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
	const std::string original = "previous-checkpoint";
	{
		std::ofstream output(path, std::ios::binary);
		output.write(original.data(), static_cast<std::streamsize>(original.size()));
	}
	for (const auto options : { texas::MultiwayTrainingCheckpointWriteOptions{ true, false },
			 texas::MultiwayTrainingCheckpointWriteOptions{ false, true } })
	{
		EXPECT_THROW(texas::MultiwayTrainingCheckpointArtifacts::save_atomic_streamed(path, source, nullptr, options), std::runtime_error);
		EXPECT_EQ(read_bytes(path), original);
		EXPECT_TRUE(!std::filesystem::exists(path.string() + ".tmp"));
	}
	std::filesystem::remove(path);
}

TEST_CASE(multiway_streaming_export_matches_materialized_compact_bytes_and_repeats)
{
	StreamingExportFixture fixture(texas::MultiwaySolverLimits::StorageBackend::CompactInt32);
	fixture.trainer.run_batches(1U, 4U, 0x77U);
	const auto materialized = fixture.trainer.export_full_policy();
	const auto base = std::filesystem::temp_directory_path() / ("texas_solver_streaming_export_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
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

TEST_CASE(multiway_streaming_external_and_compact_policy_content_match)
{
	using Backend = texas::MultiwaySolverLimits::StorageBackend;
	const auto base = std::filesystem::temp_directory_path() / ("texas_solver_external_compact_export_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
	{
		StreamingExportFixture compact(Backend::CompactInt32);
		ExportProgressState disabled_progress;
		texas::MultiwayWorkingSetMonitor disabled_monitor;
		disabled_monitor.export_progress_observer = &export_progress_observer;
		disabled_monitor.export_progress_context = &disabled_progress;
		disabled_monitor.export_progress_page_interval = 1U;
		StreamingExportFixture external(Backend::ExternalCompactInt32, disabled_monitor, base / "storage");
		compact.trainer.run_batches(1U, 4U, 0x77U);
		external.trainer.run_batches(1U, 4U, 0x77U);
		const auto compact_result = compact.trainer.save_full_policy_atomic(base / "compact.bin");
		const auto external_result = external.trainer.save_full_policy_atomic(base / "external.bin");
		EXPECT_TRUE(!external.trainer.status().export_telemetry.external_profile.enabled);
		EXPECT_EQ(disabled_progress.calls, 0U);
		EXPECT_TRUE(compact_result.plan.row_count > 0U);
		EXPECT_EQ(compact_result.plan.row_count, external_result.plan.row_count);
		EXPECT_EQ(compact_result.plan.action_count, external_result.plan.action_count);
		EXPECT_EQ(compact_result.serialized_bytes, external_result.serialized_bytes);
		EXPECT_EQ(compact_result.policy_content_hash, external_result.policy_content_hash);
		const auto compact_artifact = texas::MultiwayFullBlueprintArtifacts::load_verified(
			base / "compact.bin", compact_result.plan.identity);
		const auto external_artifact = texas::MultiwayFullBlueprintArtifacts::load_verified(
			base / "external.bin", external_result.plan.identity);
		EXPECT_EQ(compact_artifact.rows.size(), external_artifact.rows.size());
		for (std::size_t row = 0U; row < compact_artifact.rows.size(); ++row)
		{
			const auto& expected = compact_artifact.rows[row];
			const auto& actual = external_artifact.rows[row];
			if (row != 0U)
			{
				const auto& previous = external_artifact.rows[row - 1U];
				EXPECT_TRUE(std::tie(previous.infoset.public_state.value, previous.infoset.seat,
								previous.bucket, previous.action_menu_id)
					< std::tie(actual.infoset.public_state.value,
						actual.infoset.seat, actual.bucket, actual.action_menu_id));
			}
			EXPECT_EQ(expected.infoset.public_state.value, actual.infoset.public_state.value);
			EXPECT_EQ(expected.infoset.seat, actual.infoset.seat);
			EXPECT_EQ(expected.bucket, actual.bucket);
			EXPECT_EQ(expected.action_menu_id, actual.action_menu_id);
			EXPECT_EQ(expected.actions.size(), actual.actions.size());
			for (std::size_t action = 0U; action < expected.actions.size(); ++action)
			{
				EXPECT_EQ(expected.actions[action].action.action, actual.actions[action].action.action);
				EXPECT_EQ(expected.actions[action].action.action_index, actual.actions[action].action.action_index);
				EXPECT_EQ(expected.actions[action].action.target_street_contribution,
					actual.actions[action].action.target_street_contribution);
				EXPECT_EQ(expected.actions[action].action.action_menu_id, actual.actions[action].action.action_menu_id);
				EXPECT_EQ(expected.actions[action].probability, actual.actions[action].probability);
			}
		}
		const auto repeated = external.trainer.save_full_policy_atomic(base / "repeat.bin");
		EXPECT_EQ(repeated.policy_content_hash, external_result.policy_content_hash);
		EXPECT_EQ(read_bytes(base / "repeat.bin"), read_bytes(base / "external.bin"));
		texas::MultiwayWorkingSetMonitor monitor;
		ExportProgressState progress;
		monitor.profile_external_export = true;
		monitor.export_progress_observer = &export_progress_observer;
		monitor.export_progress_context = &progress;
		monitor.export_progress_page_interval = 1U;
		StreamingExportFixture profiled(Backend::ExternalCompactInt32, monitor, base / "profiled_storage");
		profiled.trainer.run_batches(1U, 4U, 0x77U);
		const auto profiled_result = profiled.trainer.save_full_policy_atomic(base / "profiled.bin");
		const auto& profile = profiled.trainer.status().export_telemetry.external_profile;
		EXPECT_TRUE(profile.enabled);
		EXPECT_EQ(profile.shapes, profiled_result.plan.shape_count);
		EXPECT_EQ(profile.rows, profiled_result.plan.row_count);
		EXPECT_EQ(profile.actions, profiled_result.plan.action_count);
		EXPECT_TRUE(!profiled_result.sharded);
		EXPECT_EQ(profile.encoded_bytes + 8U, profiled_result.serialized_bytes);
		EXPECT_TRUE(profile.stream_write_bytes <= profile.encoded_bytes);
		EXPECT_EQ(profile.sampled_rows, (profile.rows + 255U) / 256U);
		EXPECT_EQ(profile.writer_sampled_rows, profile.sampled_rows);
		EXPECT_TRUE(profile.writer_flushes > 0U);
		EXPECT_EQ(profile.catalog.lookups, profile.shapes);
		EXPECT_EQ(profile.page_store.page_hits + profile.page_store.page_misses, profile.pages);
		EXPECT_EQ(profile.page_store.lookups, profile.page_store.page_misses);
		EXPECT_EQ(profile.page_decoded_count + profile.page_zero_count,
			profile.page_store.page_misses);
		EXPECT_TRUE(progress.ordered);
		EXPECT_EQ(progress.calls, profile.pages);
		EXPECT_EQ(progress.last_rows, profile.rows);
		EXPECT_EQ(progress.last_encoded_bytes, profile.encoded_bytes);
		EXPECT_EQ(progress.last_writer_samples, profile.writer_sampled_rows);
		EXPECT_TRUE(progress.last_flushes <= profile.writer_flushes);
		EXPECT_EQ(profiled_result.policy_content_hash, external_result.policy_content_hash);
		EXPECT_EQ(profiled_result.serialized_bytes, external_result.serialized_bytes);
		const auto* fixture_profile = std::getenv("TEXASSOLVER_PB12_FIXTURE_PROFILE");
		if (fixture_profile != nullptr && std::string(fixture_profile) == "1")
		{
			std::cout << "PB12_FIXTURE_PROFILE"
					  << " rows=" << profile.rows << " actions=" << profile.actions << " pages=" << profile.pages
					  << " export_total_ns=" << profiled.trainer.status().export_telemetry.duration_nanoseconds
					  << " encoded_bytes=" << profile.encoded_bytes
					  << " stream_write_bytes=" << profile.stream_write_bytes
					  << " ordered_visit_ns=" << profile.ordered_visit_nanoseconds
					  << " visitor_ns=" << profile.visitor_nanoseconds
					  << " menu_ns=" << profile.menu_nanoseconds << " pin_ns=" << profile.pin_nanoseconds
					  << " locate_ns=" << profile.page_locate_nanoseconds
					  << " cache_find_ns=" << profile.page_cache_find_nanoseconds
					  << " victim_ns=" << profile.page_victim_nanoseconds
					  << " store_lookup_ns=" << profile.page_store_lookup_nanoseconds
					  << " decode_ns=" << profile.page_decode_nanoseconds
					  << " zero_ns=" << profile.page_zero_nanoseconds
					  << " decoded_pages=" << profile.page_decoded_count << " zero_pages=" << profile.page_zero_count
					  << " index_opens=" << profile.page_index_opens << " index_reads=" << profile.page_index_reads
					  << " data_opens=" << profile.page_data_opens << " data_reads=" << profile.page_data_reads
					  << " requested_read_bytes=" << profile.page_requested_read_bytes
					  << " index_open_ns=" << profile.page_index_open_nanoseconds
					  << " index_search_ns=" << profile.page_index_search_nanoseconds
					  << " data_open_ns=" << profile.page_data_open_nanoseconds
					  << " data_read_ns=" << profile.page_data_read_nanoseconds
					  << " sampled_rows=" << profile.sampled_rows
					  << " average_sample_ns=" << profile.sampled_average_nanoseconds
					  << " quantize_sample_ns=" << profile.sampled_quantize_nanoseconds
					  << " writer_sample_ns=" << profile.sampled_writer_nanoseconds
					  << " writer_samples=" << profile.writer_sampled_rows
					  << " validation_sample_ns=" << profile.writer_sampled_validation_nanoseconds
					  << " payload_hash_sample_ns=" << profile.writer_sampled_payload_hash_nanoseconds
					  << " policy_hash_sample_ns=" << profile.writer_sampled_policy_hash_nanoseconds
					  << " encoding_sample_ns=" << profile.writer_sampled_encoding_nanoseconds
					  << " flushes=" << profile.writer_flushes
					  << " catalog_run_probes=" << profile.catalog.run_probes
					  << " page_run_probes=" << profile.page_store.run_probes
					  << " policy_hash=" << profiled_result.policy_content_hash << '\n';
		}
	}
	std::filesystem::remove_all(base);
}

TEST_CASE(multiway_synthetic_external_export_pages_and_runs_without_training)
{
	using Backend = texas::MultiwaySolverLimits::StorageBackend;
	const auto base = std::filesystem::temp_directory_path() / ("texas_solver_synthetic_external_export_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
	texas::MultiwayWorkingSetMonitor monitor;
	monitor.profile_external_export = true;
	const auto* measurement_mode = std::getenv("TEXASSOLVER_PB12_EXPORT_MODE");
	if (measurement_mode != nullptr)
		monitor.external_export_membership_filter = std::string(measurement_mode) == "filter";
	// Hold a small number of dirty pages while constructing overlapping runs.
	constexpr std::uint64_t kSmallRowPageCacheBytes = 32'768U;
	const std::filesystem::path identity_directory = "pb12-synthetic-identity";
	StreamingExportFixture fixture(Backend::ExternalCompactInt32, monitor, base / "storage",
		kSmallRowPageCacheBytes, identity_directory, true);

	const auto& root = fixture.request.root().public_state;
	std::size_t check_index = root.legal_actions.size();
	for (std::size_t index = 0U; index < root.legal_actions.size(); ++index)
	{
		if (root.legal_actions[index].action == texas::MultiwayAction::Check)
		{
			check_index = index;
			break;
		}
	}
	EXPECT_TRUE(check_index < root.legal_actions.size());
	if (check_index >= root.legal_actions.size())
		return;

	const auto& check = root.legal_actions[check_index];
	const auto child_betting = texas::MultiwayState::from_snapshot(root.betting)
								   .apply(check.action, check.target_street_contribution)
								   .snapshot();
	const auto child_actions = fixture.abstraction.make_legal_actions(child_betting);
	const auto child = texas::MultiwayPublicBuilder::make_action_child(
		root, check_index, child_betting, child_actions);
	fixture.coordinator.admit_public_state(child);

	std::vector<texas::MultiwaySparseRowShape> ordered_shapes = {
		{ fixture.request.root().root_infoset, 1025U,
			static_cast<std::uint8_t>(root.legal_actions.size()) },
		{ { child.id, child.betting.current_player }, 513U,
			static_cast<std::uint8_t>(child.legal_actions.size()) }
	};
	std::sort(ordered_shapes.begin(), ordered_shapes.end(), [](const auto& left, const auto& right) {
		return std::tie(left.infoset.public_state.value, left.infoset.seat) < std::tie(right.infoset.public_state.value, right.infoset.seat);
	});
	EXPECT_TRUE(ordered_shapes[0].infoset.public_state.value < ordered_shapes[1].infoset.public_state.value);
	fixture.coordinator.admit_infoset_row(ordered_shapes[1]);
	fixture.coordinator.admit_infoset_row(ordered_shapes[0]);

	std::array<std::uint64_t, 2U> first_pages{};
	{
		auto lease = fixture.coordinator.acquire_export_lease();
		const auto* external = lease.external_storage();
		EXPECT_TRUE(external != nullptr);
		if (external == nullptr)
			return;
		for (std::size_t index = 0U; index < ordered_shapes.size(); ++index)
		{
			texas::MultiwayExternalRowBinding binding;
			EXPECT_TRUE(external->compact_storage().lookup_row(ordered_shapes[index].infoset, binding));
			first_pages[index] = binding.first_page_id();
		}
	}
	EXPECT_TRUE(first_pages[0] > first_pages[1]);

	const auto merge_two_deltas = [&](const texas::MultiwaySparseRowShape& shape,
									  std::uint32_t bucket, std::uint64_t trajectory_id) {
		std::vector<texas::MultiwayWorkerDeltaStream> streams;
		streams.emplace_back(0U, 2U);
		EXPECT_TRUE(streams[0].try_append({ shape.infoset, bucket, 0U,
			static_cast<double>(trajectory_id) / 10.0, 1.0, trajectory_id }));
		EXPECT_TRUE(streams[0].try_append({ shape.infoset, 1024U, 0U,
			static_cast<double>(trajectory_id) / 10.0, 1.0, trajectory_id }));
		streams[0].sort_fixed_order();
		fixture.coordinator.merge_worker_streams(streams);
	};
	for (std::uint32_t page = 0U; page < 12U; ++page)
		merge_two_deltas(ordered_shapes[0].bucket_count == 1025U ? ordered_shapes[0] : ordered_shapes[1],
			page * 16U, static_cast<std::uint64_t>(page) + 1U);
	const auto checkpoint = fixture.trainer.save_external_checkpoint_atomic(base / "fixture.mwec", 1U, 2U);
	StreamingExportFixture reopened(Backend::ExternalCompactInt32, monitor, base / "storage",
		kSmallRowPageCacheBytes, identity_directory, true);
	reopened.trainer.resume_from_external_checkpoint(checkpoint, 1U, 2U);

	const auto output_path = base / "synthetic_external.bin";
	const auto result = reopened.trainer.save_full_policy_atomic(output_path);
	const auto& profile = reopened.trainer.status().export_telemetry.external_profile;
	EXPECT_TRUE(profile.enabled);
	EXPECT_EQ(profile.shapes, std::uint64_t{ 2U });
	EXPECT_EQ(profile.rows, std::uint64_t{ 1538U });
	EXPECT_EQ(profile.pages, std::uint64_t{ 98U });
	EXPECT_EQ(profile.rows, result.plan.row_count);
	EXPECT_EQ(profile.shapes, result.plan.shape_count);
	EXPECT_TRUE(profile.page_store.page_misses > 0U);
	EXPECT_EQ(profile.page_store.page_hits + profile.page_store.page_misses, profile.pages);
	EXPECT_TRUE(profile.page_decoded_count > 0U);
	EXPECT_TRUE(profile.page_zero_count > 0U);
	if (monitor.external_export_membership_filter)
		EXPECT_TRUE(profile.page_membership_misses > 0U);
	EXPECT_EQ(profile.page_decoded_count + profile.page_zero_count, profile.page_store.page_misses);
	EXPECT_TRUE(profile.page_store.run_probes > 0U);

	if (const auto* fixture_profile = std::getenv("TEXASSOLVER_PB12_FIXTURE_PROFILE");
		fixture_profile != nullptr && std::string(fixture_profile) == "1")
	{
		std::cout << "PB12_FIXTURE_PROFILE"
				  << " rows=" << profile.rows << " actions=" << profile.actions << " pages=" << profile.pages
				  << " encoded_bytes=" << profile.encoded_bytes
				  << " ordered_visit_ns=" << profile.ordered_visit_nanoseconds
				  << " export_total_ns=" << reopened.trainer.status().export_telemetry.duration_nanoseconds
				  << " decoded_pages=" << profile.page_decoded_count << " zero_pages=" << profile.page_zero_count
				  << " page_hits=" << profile.page_store.page_hits
				  << " page_misses=" << profile.page_store.page_misses
				  << " page_lookups=" << profile.page_store.lookups
				  << " page_run_probes=" << profile.page_store.run_probes
				  << " index_opens=" << profile.page_index_opens
				  << " index_reads=" << profile.page_index_reads
				  << " range_skips=" << profile.page_run_range_skips
				  << " membership_misses=" << profile.page_membership_misses
				  << " data_reads=" << profile.page_data_reads
				  << " row_index_run_probes=" << profile.row_index.run_probes
				  << " catalog_run_probes=" << profile.catalog.run_probes
				  << " policy_hash=" << result.policy_content_hash
				  << " serialized_bytes=" << result.serialized_bytes << '\n';
	}
	if (measurement_mode != nullptr)
	{
		const auto* saved_output = std::getenv("TEXASSOLVER_PB12_EXPORT_OUTPUT");
		if (saved_output != nullptr)
			std::filesystem::copy_file(output_path, saved_output);
		std::filesystem::remove_all(base);
		return;
	}

	monitor.external_export_membership_filter = false;
	texas::MultiwayBlueprintTrainer repeated(export_identity(Backend::ExternalCompactInt32,
												 base / "storage", kSmallRowPageCacheBytes, identity_directory, true),
		reopened.runner, reopened.coordinator, {}, 0x77U, 1U, 0U, {}, {}, {}, nullptr, nullptr, monitor);
	const auto repeated_result = repeated.save_full_policy_atomic(base / "repeated.bin");
	EXPECT_TRUE(repeated.status().export_telemetry.external_profile.enabled);
	EXPECT_EQ(repeated.status().export_telemetry.external_profile.page_membership_misses, 0U);
	EXPECT_EQ(repeated_result.plan.row_count, result.plan.row_count);
	EXPECT_EQ(repeated_result.plan.action_count, result.plan.action_count);
	EXPECT_EQ(repeated_result.policy_content_hash, result.policy_content_hash);
	EXPECT_EQ(repeated_result.serialized_bytes, result.serialized_bytes);
	EXPECT_EQ(read_bytes(base / "repeated.bin"), read_bytes(output_path));
	std::filesystem::path data_path;
	std::uint64_t latest_generation = 0U;
	for (const auto& entry : std::filesystem::directory_iterator(base / "storage" / "pages"))
	{
		const auto name = entry.path().stem().string();
		if (entry.path().extension() != ".dat" || name.rfind("run_", 0U) != 0U)
			continue;
		const auto generation = std::stoull(name.substr(4U));
		if (generation > latest_generation)
		{
			latest_generation = generation;
			data_path = entry.path();
		}
	}
	EXPECT_TRUE(!data_path.empty());
	auto saved_data = read_bytes(data_path);
	EXPECT_TRUE(!saved_data.empty());
	{
		std::fstream data(data_path, std::ios::binary | std::ios::in | std::ios::out);
		data.put(static_cast<char>(saved_data[0] ^ 1U));
		EXPECT_TRUE(static_cast<bool>(data));
	}
	monitor.external_export_membership_filter = true;
	texas::MultiwayBlueprintTrainer corrupt(export_identity(Backend::ExternalCompactInt32,
												base / "storage", kSmallRowPageCacheBytes, identity_directory, true),
		reopened.runner, reopened.coordinator, {}, 0x77U, 1U, 0U, {}, {}, {}, nullptr, nullptr, monitor);
	const auto failed_path = base / "failed.bin";
	EXPECT_THROW(corrupt.save_full_policy_atomic(failed_path), std::invalid_argument);
	EXPECT_TRUE(!std::filesystem::exists(failed_path));
	EXPECT_TRUE(!std::filesystem::exists(failed_path.string() + ".tmp"));
	{
		std::ofstream data(data_path, std::ios::binary | std::ios::trunc);
		data.write(reinterpret_cast<const char*>(saved_data.data()), static_cast<std::streamsize>(saved_data.size()));
		EXPECT_TRUE(static_cast<bool>(data));
	}
	const auto recovered = corrupt.save_full_policy_atomic(base / "recovered.bin");
	EXPECT_EQ(recovered.policy_content_hash, result.policy_content_hash);
	EXPECT_EQ(read_bytes(base / "recovered.bin"), read_bytes(output_path));
	std::filesystem::remove_all(base);
}

TEST_CASE(multiway_streaming_export_float64_uses_bounded_order_indexes_and_matches_bytes)
{
	StreamingExportFixture fixture(texas::MultiwaySolverLimits::StorageBackend::Float64Reference);
	fixture.trainer.run_batches(1U, 4U, 0x77U);
	const auto materialized = fixture.trainer.export_full_policy();
	const auto base = std::filesystem::temp_directory_path() / ("texas_solver_streaming_float64_export_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
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

TEST_CASE(multiway_streaming_export_selects_bounded_shards_and_preserves_policy_hash)
{
	texas::MultiwayWorkingSetMonitor monitor;
	monitor.single_file_blueprint_limit_bytes = 1U;
	monitor.shard_max_serialized_bytes = 176U + 8U + 28U + 64U * 19U;
	StreamingExportFixture fixture(texas::MultiwaySolverLimits::StorageBackend::CompactInt32, monitor);
	fixture.trainer.run_batches(1U, 4U, 0x77U);
	const auto materialized = fixture.trainer.export_full_policy();
	const auto path = std::filesystem::temp_directory_path() / ("texas_solver_streaming_sharded_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + ".mwfb");
	const auto result = fixture.trainer.save_full_policy_atomic(path);
	const auto& telemetry = fixture.trainer.status().export_telemetry;
	EXPECT_TRUE(result.sharded);
	EXPECT_EQ(result.shard_count, std::uint64_t{ 1U });
	EXPECT_EQ(result.policy_content_hash,
		texas::MultiwayFullBlueprintArtifacts::policy_content_hash(materialized));
	EXPECT_EQ(result.serialized_bytes, telemetry.disk_gate.serialized_output_bytes);
	EXPECT_EQ(result.manifest_bytes, std::filesystem::file_size(path));
	EXPECT_EQ(telemetry.shard_count, result.shard_count);
	EXPECT_TRUE(telemetry.disk_gate.temporary_overlap_bytes <= monitor.shard_max_serialized_bytes);
	texas::MultiwayFullBlueprintArtifacts::verify_sharded_artifact(path, result.plan.identity);
	const auto manifest = texas::MultiwayFullBlueprintArtifacts::load_sharded_manifest(path,
		result.plan.identity);
	const auto generation_directory = path.parent_path() / std::filesystem::u8path(manifest.shards.front().relative_path).parent_path();
	std::filesystem::remove_all(generation_directory);
	std::filesystem::remove(path);
}

TEST_CASE(multiway_sharded_workflow_lookup_matches_materialized_qualification)
{
	StreamingExportFixture fixture(texas::MultiwaySolverLimits::StorageBackend::CompactInt32);
	fixture.trainer.run_batches(1U, 4U, 0x77U);
	auto artifact = fixture.trainer.export_full_policy();
	texas::MultiwayBlueprintTrainingConfig config;
	config.rules.player_count = 3U;
	config.blueprint.player_count = 3U;
	config.cfr.player_count = 3U;
	config.limits = export_limits(texas::MultiwaySolverLimits::StorageBackend::CompactInt32);
	artifact.identity = config.identity();
	artifact.payload_hash = texas::MultiwayFullBlueprintArtifacts::payload_hash(artifact);
	texas::MultiwayBucketRegistry qualification_buckets({ texas::MultiwayBucketTable(
		artifact.identity, texas::Street::River, kExportBoard, 1U, export_bucket_assignments()) });
	std::uint64_t action_count = 0U;
	for (const auto& row : artifact.rows)
		action_count += row.actions.size();
	const auto plan = texas::make_multiway_full_blueprint_stream_plan(
		artifact.identity, artifact.training, artifact.rows.size(), action_count);
	const auto shard_bound = 176U + 8U + 28U + 64U * 19U;
	const auto layout = texas::make_multiway_full_blueprint_shard_layout(plan, shard_bound);
	std::vector<std::uint64_t> shard_actions(layout.ranges.size(), 0U);
	for (std::size_t row_index = 0U; row_index < artifact.rows.size(); ++row_index)
		shard_actions[row_index / layout.maximum_rows_per_shard] += artifact.rows[row_index].actions.size();
	const auto path = std::filesystem::temp_directory_path() / ("texas_solver_sharded_qualification_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + ".mwfb");
	{
		texas::MultiwayFullBlueprintShardedStreamWriter writer(path, plan, layout, shard_actions);
		for (const auto& row : artifact.rows)
		{
			texas::MultiwayFullBlueprintRowView view;
			view.infoset = row.infoset;
			view.bucket = row.bucket;
			view.action_menu_id = row.action_menu_id;
			view.actions = row.actions.data();
			view.action_count = row.actions.size();
			writer.write_row(view);
		}
		(void)writer.finish();
	}
	const auto mapped = texas::MultiwayMappedShardedBlueprintProvider::load_verified(path, artifact.identity);
	texas::MultiwayEvidenceHeader evidence;
	evidence.schema_version = texas::MULTIWAY_LOOKUP_QUALIFICATION_SCHEMA_VERSION;
	evidence.profile_kind = texas::MultiwayWorkflowProfileKind::Acceptance;
	evidence.producer.model_identity = artifact.identity;
	const auto in_memory = texas::qualify_multiway_required_lookups(
		config, fixture.root, qualification_buckets, artifact, 8U, evidence);
	const auto sharded = texas::qualify_multiway_required_lookups(
		config, fixture.root, qualification_buckets, *mapped, 8U, evidence);
	EXPECT_EQ(sharded.lookup_hits, in_memory.lookup_hits);
	EXPECT_EQ(sharded.missing_infosets, in_memory.missing_infosets);
	EXPECT_EQ(sharded.missing_buckets, in_memory.missing_buckets);
	EXPECT_EQ(sharded.action_menu_mismatches, in_memory.action_menu_mismatches);
	EXPECT_EQ(sharded.policy_content_hash, in_memory.policy_content_hash);
	EXPECT_EQ(sharded.replay_fingerprint, in_memory.replay_fingerprint);
	const auto manifest = texas::MultiwayFullBlueprintArtifacts::load_sharded_manifest(path, artifact.identity);
	std::filesystem::remove_all(path.parent_path() / std::filesystem::u8path(manifest.shards.front().relative_path).parent_path());
	std::filesystem::remove(path);
}

TEST_CASE(multiway_streaming_export_process_gate_rejects_before_temp_creation)
{
	texas::MultiwayWorkingSetMonitor monitor;
	monitor.export_gate.process.hard_limit_bytes = 1U;
	monitor.export_gate.process.exporter_headroom_bytes = 0U;
	monitor.export_gate.process.encoder_buffer_bytes = 0U;
	StreamingExportFixture fixture(texas::MultiwaySolverLimits::StorageBackend::CompactInt32, monitor);
	fixture.trainer.run_batches(1U, 4U, 0x77U);
	const auto path = std::filesystem::temp_directory_path() / ("texas_solver_streaming_gate_reject_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
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

TEST_CASE(multiway_streaming_export_disk_gate_rejects_before_temp_creation)
{
	texas::MultiwayWorkingSetMonitor monitor;
	monitor.export_disk_space_observer = export_disk_space_observer;
	StreamingExportFixture fixture(texas::MultiwaySolverLimits::StorageBackend::CompactInt32, monitor);
	fixture.trainer.run_batches(1U, 4U, 0x77U);
	const auto path = std::filesystem::temp_directory_path() / ("texas_solver_streaming_disk_gate_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
	{
		std::ofstream previous(path, std::ios::binary);
		previous << "previous-blueprint";
	}
	std::filesystem::remove(path.string() + ".tmp");
	EXPECT_THROW(fixture.trainer.save_full_policy_atomic(path), std::runtime_error);
	EXPECT_EQ(read_bytes(path), std::string("previous-blueprint"));
	EXPECT_TRUE(!std::filesystem::exists(path.string() + ".tmp"));
	EXPECT_EQ(fixture.trainer.status().export_telemetry.disk_gate.status,
		texas::MultiwayExportDiskGateStatus::Rejected);
	EXPECT_EQ(fixture.trainer.status().export_telemetry.disk_gate.rejection_reason,
		texas::MultiwayExportDiskRejectionReason::InsufficientFreeSpace);
	std::filesystem::remove(path);
}

TEST_CASE(multiway_training_system_memory_guard_rejects_before_batch)
{
	using namespace texas::solver::multiway;
	MultiwayWorkingSetMonitor monitor;
	monitor.system_memory_guard.enabled = true;
	monitor.system_memory_guard.minimum_available_physical_bytes = 1'500U;
	monitor.system_memory_observer = rejected_system_memory_observer;
	StreamingExportFixture fixture(MultiwaySolverLimits::StorageBackend::CompactInt32, monitor);
	EXPECT_THROW(fixture.trainer.run_batches(1U, 4U, 0x77U), std::runtime_error);
	EXPECT_EQ(fixture.trainer.status().batches, 0U);
	EXPECT_EQ(fixture.trainer.status().system_memory_guard.status,
		MultiwaySystemMemoryGuardStatus::Rejected);
	EXPECT_EQ(std::string(fixture.trainer.status().system_memory_guard.rejected_category),
		"system physical memory ceiling");
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
	const auto path = std::filesystem::temp_directory_path() / ("texas_solver_streaming_gate_telemetry_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
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
