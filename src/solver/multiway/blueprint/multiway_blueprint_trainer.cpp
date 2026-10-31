#include "solver/multiway/blueprint/multiway_blueprint_trainer.hpp"
#include "multiway_blueprint_trainer_detail.hpp"
#include "solver/multiway/workflow/multiway_pb12_coverage.hpp"
#include "solver/multiway/engine/multiway_external_storage.hpp"
#include "solver/multiway/engine/multiway_memory.hpp"
#include "games/multiway_state.hpp"
#include "core/fingerprint.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <chrono>
#include <stdexcept>
#include <utility>

namespace texas::solver::multiway
{

	namespace
	{

		using texas::core::fingerprint::append_u64;

		template <class Range>
		void append_range(std::uint64_t& hash, const Range& values) noexcept
		{
			for (const auto value : values)
				append_u64(hash, value);
		}

		std::uint64_t hash_action_abstraction(const MultiwayActionAbstractionConfig& config) noexcept
		{
			auto hash = texas::core::fingerprint::FNV1A_OFFSET;
			append_u64(hash, config.menu_profile_version);
			append_u64(hash, config.translation_policy_version);
			append_u64(hash, config.translation_max_pseudo_harmonic_distance_basis_points);
			append_range(hash, config.first_bet_basis_points);
			append_range(hash, config.raise_basis_points);
			append_u64(hash, config.multiway_first_bet_count);
			append_u64(hash, config.three_way_first_bet_count);
			append_u64(hash, config.heads_up_first_bet_count);
			append_range(hash, config.unopened_raise_to_big_blind_basis_points);
			append_u64(hash, config.single_open_in_position_basis_points);
			append_u64(hash, config.single_open_out_of_position_basis_points);
			append_u64(hash, config.open_caller_increment_big_blind_basis_points);
			append_u64(hash, config.three_bet_or_more_basis_points);
			append_range(hash, config.contextual_multiway_first_bet_basis_points);
			append_range(hash, config.contextual_three_way_first_bet_basis_points);
			append_range(hash, config.contextual_heads_up_first_bet_basis_points);
			append_range(hash, config.contextual_raise_basis_points);
			return hash == 0U ? 1U : hash;
		}

		std::uint64_t hash_bucket_profile(
			std::uint64_t bucket_model_version,
			const MultiwayBucketBaselineProfile& profile) noexcept
		{
			auto hash = texas::core::fingerprint::FNV1A_OFFSET;
			append_u64(hash, bucket_model_version);
			append_u64(hash, profile.schema_version);
			append_u64(hash, profile.feature_version);
			append_u64(hash, profile.flop_bucket_count);
			append_u64(hash, profile.turn_bucket_count);
			append_u64(hash, profile.river_bucket_count);
			return hash == 0U ? 1U : hash;
		}

		std::uint64_t combine_identity(const MultiwayModelIdentity& identity) noexcept
		{
			auto hash = texas::core::fingerprint::FNV1A_OFFSET;
			visit_multiway_model_identity_components(identity, [&](std::uint64_t field) {
				append_u64(hash, field);
			});
			return hash == 0U ? 1U : hash;
		}

	} // namespace

	MultiwayRootSnapshot make_multiway_initial_blueprint_root(
		const MultiwayGameRules& rules,
		MultiwayPrivateConfig private_ranges,
		const MultiwayActionAbstraction& action_abstraction,
		std::uint64_t action_abstraction_version,
		std::uint64_t leaf_model_version,
		PlayerId first_player)
	{
		rules.validate();
		auto state = games::multiway::MultiwayState::initial(rules, first_player);
		auto betting = state.snapshot();
		MultiwayRootSnapshot root;
		root.public_state = MultiwayPublicBuilder::make_root(
			betting, {}, action_abstraction.make_legal_actions(betting));
		root.root_infoset = { root.public_state.id, betting.current_player };
		root.root_bucket = 0U;
		root.seat_order.resize(rules.player_count);
		for (std::uint8_t seat = 0; seat < rules.player_count; ++seat)
			root.seat_order[seat] = seat;
		root.next_street_first_seat = first_player;
		root.odd_chip_first_seat = first_player;
		root.private_ranges = std::move(private_ranges);
		root.action_abstraction_version = action_abstraction_version;
		root.leaf_model_version = leaf_model_version;
		root.validate();
		return root;
	}

	void MultiwayBlueprintIterationSchedule::validate() const
	{
		if (!std::isfinite(regret_discount_factor) || regret_discount_factor <= 0.0 || regret_discount_factor > 1.0 || discount_interval_batches == 0U || pruning_interval_batches == 0U || !std::isfinite(pruning_threshold) || pruning_threshold > 0.0 || !std::isfinite(regret_floor) || regret_floor > 0.0 || !std::isfinite(pruning_exploration_probability) || pruning_exploration_probability < 0.0 || pruning_exploration_probability > 1.0 || !std::isfinite(pruning_action_probability_threshold) || pruning_action_probability_threshold < 0.0 || pruning_action_probability_threshold > 1.0)
		{
			throw std::invalid_argument("multiway blueprint schedule has invalid discount or pruning controls");
		}
	}

	double MultiwayBlueprintIterationSchedule::strategy_weight(std::uint64_t one_based_batch) const
	{
		if (one_based_batch == 0U)
		{
			throw std::invalid_argument("multiway blueprint strategy weight requires a one-based batch index");
		}
		return linear_iteration_weighting ? static_cast<double>(one_based_batch) : 1.0;
	}

	std::uint64_t MultiwayBlueprintIterationSchedule::identity() const noexcept
	{
		auto hash = texas::core::fingerprint::FNV1A_OFFSET;
		append_u64(hash, linear_iteration_weighting ? 1U : 0U);
		append_u64(hash, discount_regrets ? 1U : 0U);
		append_u64(hash, static_cast<std::uint64_t>(regret_discount_factor * 1000000000.0));
		append_u64(hash, discount_interval_batches);
		append_u64(hash, prune_negative_regrets ? 1U : 0U);
		append_u64(hash, pruning_warmup_batches);
		append_u64(hash, pruning_interval_batches);
		append_u64(hash, static_cast<std::uint64_t>(pruning_threshold * -1000000000.0));
		append_u64(hash, static_cast<std::uint64_t>(regret_floor * -1000000000.0));
		append_u64(hash, recovery_interval_batches);
		append_u64(hash, static_cast<std::uint64_t>(pruning_exploration_probability * 1000000000.0));
		append_u64(hash, static_cast<std::uint64_t>(pruning_action_probability_threshold * 1000000000.0));
		append_u64(hash, late_window_start_batch);
		return hash == 0U ? 1U : hash;
	}

	void MultiwayBlueprintTrainingConfig::validate() const
	{
		rules.validate();
		blueprint.validate();
		bucket_profile.validate();
		action_abstraction.validate();
		cfr.validate();
		limits.validate();
		schedule.validate();
		placement.validate();
		working_set_monitor.validate();
		if (deterministic_seed == 0U || cfr.player_count != rules.player_count || blueprint.player_count != rules.player_count || blueprint.initial_stack_chips != rules.initial_stack_chips || blueprint.small_blind_chips != rules.small_blind_chips || blueprint.big_blind_chips != rules.big_blind_chips || blueprint.ante_chips != rules.ante_chips || blueprint.rake_policy.identity() != rules.rake_policy.identity() || blueprint.rules_profile_version != rules.profile_version || blueprint.flop_bucket_count != bucket_profile.flop_bucket_count || blueprint.turn_bucket_count != bucket_profile.turn_bucket_count || blueprint.river_bucket_count != bucket_profile.river_bucket_count || max_decision_depth == 0U || max_decision_depth > MULTIWAY_MAX_DECISION_DEPTH || max_public_chance_depth > MULTIWAY_MAX_PUBLIC_CHANCE_DEPTH || !multiway_scheduler_mode_valid(scheduler_mode) || dynamic_chunk_size == 0U || (requested_worker_count != 0U && requested_worker_count < limits.worker_count))
		{
			throw std::invalid_argument("multiway training configuration has inconsistent artifact or traversal inputs");
		}
	}

	MultiwayModelIdentity MultiwayBlueprintTrainingConfig::identity() const
	{
		validate();
		auto identity = make_multiway_model_identity(blueprint);
		identity.action_abstraction_hash = hash_action_abstraction(action_abstraction);
		identity.bucket_model_hash = hash_bucket_profile(blueprint.bucket_model_version, bucket_profile);
		auto runtime_hash = texas::core::fingerprint::FNV1A_OFFSET;
		append_u64(runtime_hash, blueprint.runtime_search_schema_version);
		append_u64(runtime_hash, max_decision_depth);
		append_u64(runtime_hash, max_public_chance_depth);
		append_u64(runtime_hash, cfr.player_count);
		append_u64(runtime_hash, cfr.deterministic_trajectory_merges ? 1U : 0U);
		append_u64(runtime_hash, limits.worker_count);
		append_u64(runtime_hash, limits.trajectories_per_batch);
		append_u64(runtime_hash, limits.max_public_states);
		append_u64(runtime_hash, limits.max_sparse_rows);
		append_u64(runtime_hash, limits.max_sparse_values);
		append_u64(runtime_hash, limits.regret_delta_capacity());
		append_u64(runtime_hash, limits.continuation_delta_capacity());
		// max_batches is a run horizon, not checkpoint/model semantics. Excluding
		// it permits a checkpoint to resume with a different remaining batch count.
		append_u64(runtime_hash, static_cast<std::uint8_t>(limits.storage_backend));
		append_u64(runtime_hash, limits.compact_page_width);
		if (limits.storage_backend == MultiwaySolverLimits::StorageBackend::ExternalCompactInt32)
		{
			const auto& identity_directory = limits.external_storage.identity_directory.empty()
				? limits.external_storage.directory
				: limits.external_storage.identity_directory;
			const auto storage_directory = identity_directory.lexically_normal().generic_u8string();
			append_u64(runtime_hash, storage_directory.size());
			for (const auto character : storage_directory)
				texas::core::fingerprint::append_u8(runtime_hash, static_cast<std::uint8_t>(character));
			append_u64(runtime_hash, limits.external_storage.public_catalog_cache_bytes);
			append_u64(runtime_hash, limits.external_storage.row_page_cache_bytes);
			append_u64(runtime_hash, limits.external_storage.maximum_wal_bytes);
			append_u64(runtime_hash, limits.external_storage.segment_target_bytes);
			append_u64(runtime_hash, limits.external_storage.compaction_fan_in);
			append_u64(runtime_hash, limits.external_storage.compaction_scratch_bytes);
			append_u64(runtime_hash, limits.external_storage.disk_reserve_bytes);
			append_u64(runtime_hash, limits.external_storage.synchronous_durability ? 1U : 0U);
		}
		append_u64(runtime_hash, limits.discovery_public_state_capacity());
		append_u64(runtime_hash, limits.discovery_row_capacity());
		append_u64(runtime_hash, immutable_batch_epochs ? 1U : 0U);
		append_u64(runtime_hash, MULTIWAY_PARTITION_VERSION);
		append_u64(runtime_hash, MULTIWAY_STATIC_BLOCK_SCHEDULER_VERSION);
		append_u64(runtime_hash, MULTIWAY_SCHEDULER_POLICY_VERSION);
		append_u64(runtime_hash, static_cast<std::uint64_t>(scheduler_mode));
		append_u64(runtime_hash, dynamic_chunk_size);
		append_u64(runtime_hash, schedule.identity());
		append_u64(runtime_hash, deterministic_seed);
		identity.runtime_search_schema_hash = runtime_hash == 0U ? 1U : runtime_hash;
		identity.combined_hash = combine_identity(identity);
		identity.validate();
		return identity;
	}

	MultiwayBlueprintTrainer::MultiwayBlueprintTrainer(
		MultiwayModelIdentity identity,
		MultiwayRootBatchRunner& batch_runner,
		MultiwaySolverCoordinator& coordinator,
		MultiwayBlueprintIterationSchedule schedule,
		std::uint64_t deterministic_seed,
		std::uint32_t requested_worker_count,
		std::uint64_t memory_preflight_estimate_bytes, MultiwayMemoryLedger memory_ledger,
		MultiwayProcessMemorySnapshotCollection process_memory_snapshots,
		MultiwayMemorySubsystemSnapshotCollection memory_subsystem_snapshots,
		const MultiwayBucketRegistry* buckets,
		const MultiwayFixedContinuationSelector* continuation_selector,
		MultiwayWorkingSetMonitor working_set_monitor)
		: identity_(identity), batch_runner_(&batch_runner), coordinator_(&coordinator), schedule_(schedule), deterministic_seed_(deterministic_seed), started_at_(std::chrono::steady_clock::now()), process_cpu_started_nanoseconds_(observed_multiway_training_process_cpu_nanoseconds()), buckets_(buckets), continuation_selector_(continuation_selector), working_set_monitor_(working_set_monitor)
	{
		identity_.validate();
		schedule_.validate();
		if (deterministic_seed_ == 0U)
		{
			throw std::invalid_argument("multiway blueprint trainer requires a non-zero deterministic seed");
		}
		status_.late_window_start_batch = schedule_.late_window_start_batch;
		status_.scheduler_mode = batch_runner_->scheduler_mode();
		status_.dynamic_chunk_size = batch_runner_->dynamic_chunk_size();
		status_.scheduler_policy_version = MULTIWAY_SCHEDULER_POLICY_VERSION;
		status_.static_block_scheduler_version = MULTIWAY_STATIC_BLOCK_SCHEDULER_VERSION;
		status_.partition_version = MULTIWAY_PARTITION_VERSION;
		status_.merge_order_version = MULTIWAY_MERGE_ORDER_VERSION;
		status_.admitted_rows_per_batch.reserve(256U);
		status_.configured_max_public_states = coordinator_->limits().max_public_states;
		status_.configured_max_sparse_rows = coordinator_->limits().max_sparse_rows;
		status_.configured_max_sparse_values = coordinator_->limits().max_sparse_values;
		status_.configured_max_regret_delta_entries_per_worker = coordinator_->limits().regret_delta_capacity();
		status_.configured_max_continuation_delta_entries_per_worker = coordinator_->limits().continuation_delta_capacity();
		status_.configured_max_discovery_public_states_per_worker = coordinator_->limits().discovery_public_state_capacity();
		status_.configured_max_discovery_rows_per_worker = coordinator_->limits().discovery_row_capacity();
		status_.configured_worker_delta_capacity = std::max(
			status_.configured_max_regret_delta_entries_per_worker,
			status_.configured_max_continuation_delta_entries_per_worker);
		status_.configured_discovery_capacity = std::max(
			status_.configured_max_discovery_public_states_per_worker,
			status_.configured_max_discovery_rows_per_worker);
		status_.requested_worker_count = requested_worker_count == 0U
			? coordinator_->limits().worker_count
			: requested_worker_count;
		status_.effective_worker_count = coordinator_->limits().worker_count;
		status_.trajectories_per_batch = coordinator_->limits().trajectories_per_batch;
		status_.memory_preflight_estimate_bytes = memory_preflight_estimate_bytes;
		status_.memory_ledger = memory_ledger;
		status_.working_set_gate_config = working_set_monitor_.gate;
		status_.working_set_gate.hard_limit_bytes = working_set_monitor_.gate.hard_limit_bytes;
		status_.working_set_gate.resident_allowance_bytes = working_set_monitor_.gate.resident_allowance_bytes;
		status_.working_set_gate.estimated_working_set_bytes = memory_ledger.estimated_working_set_peak_bytes;
		status_.system_memory_guard_config = working_set_monitor_.system_memory_guard;
		status_.process_memory_snapshots = process_memory_snapshots;
		status_.memory_subsystem_snapshots = memory_subsystem_snapshots;
	}

	MultiwayBlueprintCoverageManifest MultiwayBlueprintTrainer::coverage_manifest() const noexcept
	{
		return {
			status_.visited_public_descriptors,
			status_.admitted_rows,
			status_.admitted_action_cells,
			status_.terminal_visits,
			status_.leaf_visits,
			status_.missing_lookup_requests,
			status_.preflop_rows,
			status_.flop_rows,
			status_.turn_rows,
			status_.river_rows,
			status_.street_visits,
		};
	}

	MultiwayExternalStorageTelemetry MultiwayBlueprintTrainer::external_storage_telemetry(bool include_disk_usage) const
	{
		if (coordinator_->limits().storage_backend != MultiwaySolverLimits::StorageBackend::ExternalCompactInt32)
			throw std::logic_error("external storage telemetry requires ExternalCompactInt32");
		const auto lease = coordinator_->acquire_export_lease();
		const auto* external = lease.external_storage();
		if (external == nullptr)
			throw std::logic_error("external storage telemetry has no external storage");
		return external->telemetry(include_disk_usage);
	}

	void MultiwayBlueprintTrainer::set_external_publication_observer(
		MultiwayExternalPublicationObserver observer, void* context)
	{
		coordinator_->set_external_publication_observer(observer, context);
	}

	MultiwayBlueprintSnapshot MultiwayBlueprintTrainer::publish(MultiwayBlueprintPolicyKind policy_kind) const
	{
		auto metadata = trainer_detail::make_metadata(status_, schedule_, deterministic_seed_);
		if (policy_kind == MultiwayBlueprintPolicyKind::LateWindowAverage)
		{
			if (late_window_baseline_.empty())
			{
				throw std::logic_error("multiway late-window policy was requested before its window started");
			}
			const auto policy = coordinator_->export_root_policy_since(late_window_baseline_);
			MultiwayBlueprintSnapshot snapshot;
			snapshot.identity = identity_;
			snapshot.public_state = policy.public_state;
			snapshot.infoset = policy.infoset;
			snapshot.bucket = policy.bucket;
			snapshot.trajectories = status_.trajectories;
			snapshot.policy_kind = policy_kind;
			snapshot.training = metadata;
			std::uint32_t assigned = 0;
			snapshot.actions.reserve(policy.actions.size());
			for (std::size_t index = 0; index < policy.actions.size(); ++index)
			{
				const auto quantized = index + 1U == policy.actions.size()
					? static_cast<std::uint16_t>(std::numeric_limits<std::uint16_t>::max() - assigned)
					: static_cast<std::uint16_t>(std::floor(policy.actions[index].probability * std::numeric_limits<std::uint16_t>::max()));
				assigned += quantized;
				snapshot.actions.push_back({ policy.actions[index].action, quantized });
			}
			snapshot.validate();
			return snapshot;
		}
		return export_multiway_root_snapshot(identity_, *coordinator_, status_.trajectories, policy_kind, metadata);
	}

	MultiwayBlueprintTrainingSession::MultiwayBlueprintTrainingSession(
		MultiwayBlueprintTrainingConfig config,
		MultiwayRootSnapshot root,
		const MultiwayBucketRegistry& buckets,
		const MultiwayLeafEvaluator* leaf_evaluator)
		: config_(std::move(config)), root_(std::move(root)), buckets_(&buckets)
	{
		config_.validate();
		root_.validate();
		const auto identity = config_.identity();
		// Bucket artifacts carry their producer model identity. Runtime training
		// identity adds schedule/worker fields and is intentionally separate.
		if (root_.seat_order.size() != config_.rules.player_count || root_.rake_policy.identity() != config_.rules.rake_policy.identity() || root_.action_abstraction_version != config_.blueprint.action_abstraction_version || root_.leaf_model_version != config_.blueprint.terminal_model_version || (leaf_evaluator != nullptr && !leaf_evaluator->valid()))
		{
			throw std::invalid_argument("multiway training session has incompatible root, bucket, or leaf inputs");
		}
		if (leaf_evaluator != nullptr)
			leaf_evaluator_ = *leaf_evaluator;
		MultiwaySolveRequest request(root_, config_.cfr, config_.limits);
		coordinator_ = std::make_unique<MultiwaySolverCoordinator>(request);
		if (!config_.process_memory_snapshots.record_current(MultiwayMemoryPhase::AfterCoordinatorConstruction))
			throw std::logic_error("multiway training memory snapshot phase order is invalid");
		if (config_.memory_subsystem_snapshots.enabled)
		{
			if (!config_.memory_subsystem_snapshots.record(
					MultiwayMemoryPhase::AfterCoordinatorConstruction,
					capture_multiway_memory_subsystems(buckets_, coordinator_.get(), nullptr)))
				throw std::logic_error("multiway subsystem memory snapshot phase order is invalid");
		}
		action_abstraction_ = std::make_unique<MultiwayActionAbstraction>(config_.action_abstraction);
		continuation_selector_ = std::make_unique<MultiwayFixedContinuationSelector>(
			config_.blueprint.continuation_policy);
		MultiwayRootExternalSamplingTraversal traversal(
			*coordinator_, root_, *action_abstraction_, *buckets_, leaf_evaluator == nullptr ? nullptr : &leaf_evaluator_,
			config_.max_decision_depth, config_.max_public_chance_depth, nullptr, continuation_selector_.get(), nullptr,
			{ config_.schedule.prune_negative_regrets, config_.schedule.pruning_warmup_batches,
				config_.schedule.recovery_interval_batches, config_.schedule.pruning_exploration_probability,
				config_.schedule.pruning_action_probability_threshold,
				config_.schedule.pruning_threshold });
		batch_runner_ = std::make_unique<MultiwayRootBatchRunner>(
			std::move(traversal), *coordinator_, config_.limits.worker_count,
			MultiwayWorkerStreamCapacities{
				config_.limits.regret_delta_capacity(),
				config_.limits.continuation_delta_capacity(),
				config_.limits.discovery_public_state_capacity(),
				config_.limits.discovery_row_capacity() },
			MultiwaySearchProfileMode::Disabled,
			config_.pin_workers
				? MultiwayWorkerPlacementConfig{ MultiwayWorkerPlacementPolicy::DiagnosticHardAffinity, 0U }
				: config_.placement,
			config_.immutable_batch_epochs,
			config_.scheduler_mode,
			config_.dynamic_chunk_size);
		if (!config_.process_memory_snapshots.record_current(MultiwayMemoryPhase::AfterWorkerPoolConstruction))
			throw std::logic_error("multiway training memory snapshot phase order is invalid");
		if (config_.memory_subsystem_snapshots.enabled)
		{
			if (!config_.memory_subsystem_snapshots.record(
					MultiwayMemoryPhase::AfterWorkerPoolConstruction,
					capture_multiway_memory_subsystems(buckets_, coordinator_.get(), batch_runner_.get(), continuation_selector_.get())))
				throw std::logic_error("multiway subsystem memory snapshot phase order is invalid");
		}
		trainer_ = std::make_unique<MultiwayBlueprintTrainer>(
			identity, *batch_runner_, *coordinator_, config_.schedule, config_.deterministic_seed,
			config_.requested_worker_count, config_.memory_preflight_estimate_bytes, config_.memory_ledger,
			config_.process_memory_snapshots, config_.memory_subsystem_snapshots,
			buckets_, continuation_selector_.get(), config_.working_set_monitor);
	}

	MultiwayPB12CoverageAdmission MultiwayBlueprintTrainingSession::admit_pb12_targeted_coverage()
	{
		return admit_multiway_pb12_coverage(root_, *action_abstraction_, *coordinator_);
	}

	const MultiwayBlueprintTrainingStatus& MultiwayBlueprintTrainingSession::status() const noexcept
	{
		return trainer_->status();
	}

	MultiwayBlueprintCoverageManifest MultiwayBlueprintTrainingSession::coverage_manifest() const noexcept
	{
		return trainer_->coverage_manifest();
	}

	MultiwayExternalStorageTelemetry MultiwayBlueprintTrainingSession::external_storage_telemetry(bool include_disk_usage) const
	{
		return trainer_->external_storage_telemetry(include_disk_usage);
	}

	void MultiwayBlueprintTrainingSession::set_external_publication_observer(
		MultiwayExternalPublicationObserver observer, void* context)
	{
		trainer_->set_external_publication_observer(observer, context);
	}

	void MultiwayBlueprintTrainingSession::record_process_memory_snapshot(MultiwayMemoryPhase phase)
	{
		trainer_->record_process_memory_snapshot(
			phase,
			phase == MultiwayMemoryPhase::AfterCheckpoint ? checkpoint_buffers_ : MultiwayMemoryComponent{},
			phase == MultiwayMemoryPhase::AfterExport ? export_buffers_ : MultiwayMemoryComponent{});
		if (phase == MultiwayMemoryPhase::AfterCheckpoint)
			checkpoint_buffers_ = {};
		if (phase == MultiwayMemoryPhase::AfterExport)
			export_buffers_ = {};
	}

} // namespace texas::solver::multiway
