#include "solver/multiway/blueprint/multiway_blueprint_trainer.hpp"
#include "multiway_blueprint_trainer_detail.hpp"
#include "solver/multiway/blueprint/multiway_checkpoint.hpp"
#include "solver/multiway/blueprint/multiway_training_checkpoint_artifact.hpp"
#include "solver/multiway/engine/multiway_compact_storage.hpp"
#include "solver/multiway/engine/multiway_external_storage.hpp"
#include "solver/multiway/engine/multiway_memory.hpp"
#include "util/tracy.hpp"

#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string>

namespace texas::solver::multiway
{
	void MultiwayBlueprintTrainingCheckpoint::validate() const
	{
		identity.validate();
		if (training.trajectories < training.batches || coverage.admitted_rows != coordinator.storage.shapes.size() || coverage.terminal_visits != coordinator.terminal_visits || coverage.leaf_visits != coordinator.leaf_visits || coverage.missing_lookup_requests != coordinator.missing_lookup_requests || coverage.street_visits != coordinator.street_visits || coordinator.storage.regrets.size() != coordinator.storage.strategy_sums.size() || coordinator.storage.compact_regrets.size() != coordinator.storage.compact_strategy_masses.size())
		{
			throw std::invalid_argument("multiway training checkpoint metadata is invalid");
		}
		std::size_t expected_values = 0U;
		for (const auto& shape : coordinator.storage.shapes)
		{
			if (shape.infoset.seat < 0 || shape.bucket_count == 0U || shape.action_count == 0U)
			{
				throw std::invalid_argument("multiway training checkpoint row shape is invalid");
			}
			expected_values += static_cast<std::size_t>(shape.bucket_count) * shape.action_count;
		}
		if (expected_values != coordinator.storage.regrets.size())
		{
			throw std::invalid_argument("multiway training checkpoint value count is invalid");
		}
		for (const auto value : coordinator.storage.regrets)
		{
			if (!std::isfinite(value))
				throw std::invalid_argument("multiway training checkpoint regret is non-finite");
		}
		for (const auto value : coordinator.storage.strategy_sums)
		{
			if (!std::isfinite(value))
				throw std::invalid_argument("multiway training checkpoint strategy sum is non-finite");
		}
		for (const auto value : late_window_baseline)
		{
			if (!std::isfinite(value))
				throw std::invalid_argument("multiway training checkpoint baseline is non-finite");
		}
	}

	MultiwayTrainingCheckpointWriteResult MultiwayBlueprintTrainer::save_checkpoint_atomic(
		const std::filesystem::path& path) const
	{
		trainer_detail::MultiwayDurationRecorder checkpoint_duration(status_.checkpoint_wall_nanoseconds);
		const auto process_memory = working_set_monitor_.observer == nullptr
			? capture_multiway_process_memory_snapshot()
			: working_set_monitor_.observer(working_set_monitor_.observer_context);
		status_.working_set_gate = evaluate_multiway_working_set_runtime(
			process_memory, working_set_monitor_.gate);
		if (status_.working_set_gate.blocked())
			throw std::runtime_error(std::string("training checkpoint process gate rejected: ") + status_.working_set_gate.rejected_category);
		const auto system_memory = working_set_monitor_.system_memory_observer == nullptr
			? capture_multiway_system_memory_snapshot()
			: working_set_monitor_.system_memory_observer(
				  working_set_monitor_.system_memory_observer_context);
		status_.system_memory_guard = evaluate_multiway_system_memory_guard(
			system_memory, working_set_monitor_.system_memory_guard);
		if (status_.system_memory_guard.blocked())
			throw std::runtime_error(std::string("training checkpoint system memory gate rejected: ") + status_.system_memory_guard.rejected_category);
		auto lease = coordinator_->acquire_checkpoint_lease();
		const auto training = trainer_detail::make_metadata(status_, schedule_, deterministic_seed_);
		const auto coverage = coverage_manifest();
		const auto& diagnostics = lease.diagnostics();
		MultiwayTrainingCheckpointStreamSource source;
		source.identity = &identity_;
		source.training = &training;
		source.coverage = &coverage;
		source.catalog = &lease.public_catalog();
		source.storage = &lease.storage();
		source.compact_storage = lease.compact_storage();
		source.late_window_baseline = &late_window_baseline_;
		source.gate = &working_set_monitor_.export_gate;
		source.terminal_visits = diagnostics.terminal_visits;
		source.leaf_visits = diagnostics.leaf_visits;
		source.missing_lookup_requests = diagnostics.missing_lookup_requests;
		source.last_merged_stream_fingerprint = diagnostics.last_merged_stream_fingerprint;
		source.street_visits = diagnostics.street_visits;
		MultiwayCheckpointTelemetry telemetry;
		auto result = MultiwayTrainingCheckpointArtifacts::save_atomic_streamed(path, source, &telemetry);
		result.telemetry = telemetry;
		return result;
	}

	MultiwayExternalCheckpointManifest MultiwayBlueprintTrainer::save_external_checkpoint_atomic(
		const std::filesystem::path& path,
		std::uint64_t workflow_identity_hash,
		std::uint64_t storage_identity_hash) const
	{
		if (coordinator_->limits().storage_backend != MultiwaySolverLimits::StorageBackend::ExternalCompactInt32 || workflow_identity_hash == 0U || storage_identity_hash == 0U)
			throw std::invalid_argument("external checkpoint requires the external backend and verified identities");
		trainer_detail::MultiwayDurationRecorder checkpoint_duration(status_.checkpoint_wall_nanoseconds);
		const auto process_memory = working_set_monitor_.observer == nullptr
			? capture_multiway_process_memory_snapshot()
			: working_set_monitor_.observer(working_set_monitor_.observer_context);
		status_.working_set_gate = evaluate_multiway_working_set_runtime(
			process_memory, working_set_monitor_.gate);
		if (status_.working_set_gate.blocked())
			throw std::runtime_error(std::string("external checkpoint process gate rejected: ") + status_.working_set_gate.rejected_category);
		const auto system_memory = working_set_monitor_.system_memory_observer == nullptr
			? capture_multiway_system_memory_snapshot()
			: working_set_monitor_.system_memory_observer(
				  working_set_monitor_.system_memory_observer_context);
		status_.system_memory_guard = evaluate_multiway_system_memory_guard(
			system_memory, working_set_monitor_.system_memory_guard);
		if (status_.system_memory_guard.blocked())
			throw std::runtime_error(std::string("external checkpoint system memory gate rejected: ") + status_.system_memory_guard.rejected_category);
		auto lease = coordinator_->acquire_checkpoint_lease();
		if (lease.external_storage() == nullptr)
			throw std::logic_error("external checkpoint lease has no external storage");
		const auto& external = coordinator_->limits().external_storage;
		auto manifest = make_multiway_external_checkpoint_manifest(
			external.directory, identity_.combined_hash, workflow_identity_hash,
			storage_identity_hash, schedule_.identity(), status_.trajectories);
		manifest.completed_batches = status_.batches;
		manifest.pruned_negative_regrets = status_.pruned_negative_regrets;
		manifest.late_window_start_batch = status_.late_window_start_batch;
		manifest.terminal_visits = status_.terminal_visits;
		manifest.leaf_visits = status_.leaf_visits;
		manifest.missing_lookup_requests = status_.missing_lookup_requests;
		manifest.preflop_rows = status_.preflop_rows;
		manifest.flop_rows = status_.flop_rows;
		manifest.turn_rows = status_.turn_rows;
		manifest.river_rows = status_.river_rows;
		manifest.street_visits = status_.street_visits;
		manifest.linear_iteration_weighting = schedule_.linear_iteration_weighting ? 1U : 0U;
		manifest.discounting_enabled = schedule_.discount_regrets ? 1U : 0U;
		manifest.negative_regret_pruning_enabled = schedule_.prune_negative_regrets ? 1U : 0U;
		manifest.late_window_baseline = late_window_baseline_;
		save_multiway_external_checkpoint_manifest(
			path, external.directory, manifest, external.synchronous_durability, {},
			external.disk_reserve_bytes);
		return manifest;
	}

	MultiwayBlueprintTrainingCheckpoint MultiwayBlueprintTrainer::checkpoint() const
	{
		MultiwayBlueprintTrainingCheckpoint result;
		result.identity = identity_;
		result.training = trainer_detail::make_metadata(status_, schedule_, deterministic_seed_);
		result.coverage = coverage_manifest();
		result.coordinator = coordinator_->checkpoint();
		result.telemetry = result.coordinator.telemetry;
		result.late_window_baseline = late_window_baseline_;
		{
			TEXASSOLVER_TRACY_ZONE_SCOPED("mw.checkpoint.training_validation");
			const auto started = std::chrono::steady_clock::now();
			result.validate();
			result.telemetry.validation_nanoseconds += static_cast<std::uint64_t>(
				std::chrono::duration_cast<std::chrono::nanoseconds>(
					std::chrono::steady_clock::now() - started)
					.count());
		}
		return result;
	}

	void MultiwayBlueprintTrainer::resume_from_root_policy(const MultiwayBlueprintSnapshot& snapshot)
	{
		MultiwayRootPolicyArtifact::validate_resume_identity(snapshot, identity_);
		if (snapshot.training.schedule_hash != schedule_.identity() || snapshot.training.deterministic_seed != deterministic_seed_)
		{
			throw std::invalid_argument("multiway checkpoint schedule or seed does not match the live trainer");
		}
		if (snapshot.training.trajectories != snapshot.trajectories || snapshot.training.batches != status_.batches || snapshot.trajectories != status_.trajectories)
		{
			throw std::invalid_argument("multiway compact checkpoint requires matching live training state");
		}
	}

	void MultiwayBlueprintTrainer::resume_from_checkpoint(
		const MultiwayBlueprintTrainingCheckpoint& checkpoint)
	{
		checkpoint.validate();
		checkpoint.identity.validate();
		if (checkpoint.identity != identity_ || checkpoint.training.schedule_hash != schedule_.identity() || checkpoint.training.deterministic_seed != deterministic_seed_ || checkpoint.training.trajectories < checkpoint.training.batches || checkpoint.training.linear_iteration_weighting != (schedule_.linear_iteration_weighting ? 1U : 0U) || checkpoint.training.discounting_enabled != (schedule_.discount_regrets ? 1U : 0U) || checkpoint.training.negative_regret_pruning_enabled != (schedule_.prune_negative_regrets ? 1U : 0U))
		{
			throw std::invalid_argument("multiway full checkpoint identity or schedule does not match the trainer");
		}
		coordinator_->restore_checkpoint(checkpoint.coordinator);
		status_.batches = checkpoint.training.batches;
		status_.trajectories = checkpoint.training.trajectories;
		status_.pruned_negative_regrets = checkpoint.training.pruned_negative_regrets;
		status_.late_window_start_batch = checkpoint.training.late_window_start_batch;
		status_.late_window_active = !checkpoint.late_window_baseline.empty();
		status_.visited_public_descriptors = checkpoint.coverage.visited_public_descriptors;
		status_.admitted_rows = checkpoint.coverage.admitted_rows;
		status_.admitted_action_cells = checkpoint.coverage.admitted_action_cells;
		status_.terminal_visits = checkpoint.coverage.terminal_visits;
		status_.leaf_visits = checkpoint.coverage.leaf_visits;
		status_.missing_lookup_requests = checkpoint.coverage.missing_lookup_requests;
		status_.preflop_rows = checkpoint.coverage.preflop_rows;
		status_.flop_rows = checkpoint.coverage.flop_rows;
		status_.turn_rows = checkpoint.coverage.turn_rows;
		status_.river_rows = checkpoint.coverage.river_rows;
		status_.street_visits = checkpoint.coverage.street_visits;
		late_window_baseline_ = checkpoint.late_window_baseline;
		const auto& diagnostics = coordinator_->diagnostics();
		status_.visited_public_descriptors = diagnostics.public_states_admitted;
		status_.admitted_rows = diagnostics.sparse_rows_admitted;
		status_.preflop_rows = diagnostics.preflop_rows_admitted;
		status_.flop_rows = diagnostics.flop_rows_admitted;
		status_.turn_rows = diagnostics.turn_rows_admitted;
		status_.river_rows = diagnostics.river_rows_admitted;
		status_.discarded_trajectories = diagnostics.trajectories_discarded;
		status_.merged_stream_fingerprint = diagnostics.last_merged_stream_fingerprint;
		const auto* compact_storage = coordinator_->compact_storage();
		status_.admitted_action_cells = compact_storage != nullptr
			? compact_storage->value_count()
			: coordinator_->storage().value_count();
		status_.peak_public_states = diagnostics.public_states_admitted;
		status_.peak_sparse_rows = diagnostics.sparse_rows_admitted;
		status_.peak_sparse_values = status_.admitted_action_cells;
		status_.street_visits = diagnostics.street_visits;
	}

	void MultiwayBlueprintTrainer::resume_from_external_checkpoint(
		const MultiwayExternalCheckpointManifest& checkpoint,
		std::uint64_t workflow_identity_hash,
		std::uint64_t storage_identity_hash)
	{
		if (coordinator_->limits().storage_backend != MultiwaySolverLimits::StorageBackend::ExternalCompactInt32 || checkpoint.model_identity_hash != identity_.combined_hash || checkpoint.workflow_identity_hash != workflow_identity_hash || checkpoint.storage_identity_hash != storage_identity_hash || checkpoint.schedule_hash != schedule_.identity() || checkpoint.completed_trajectories < checkpoint.completed_batches || checkpoint.linear_iteration_weighting != (schedule_.linear_iteration_weighting ? 1U : 0U) || checkpoint.discounting_enabled != (schedule_.discount_regrets ? 1U : 0U) || checkpoint.negative_regret_pruning_enabled != (schedule_.prune_negative_regrets ? 1U : 0U))
			throw std::invalid_argument("external checkpoint identity or schedule does not match the trainer");
		status_.batches = checkpoint.completed_batches;
		status_.trajectories = checkpoint.completed_trajectories;
		status_.pruned_negative_regrets = checkpoint.pruned_negative_regrets;
		status_.late_window_start_batch = checkpoint.late_window_start_batch;
		status_.late_window_active = !checkpoint.late_window_baseline.empty();
		status_.terminal_visits = checkpoint.terminal_visits;
		status_.leaf_visits = checkpoint.leaf_visits;
		status_.missing_lookup_requests = checkpoint.missing_lookup_requests;
		status_.preflop_rows = checkpoint.preflop_rows;
		status_.flop_rows = checkpoint.flop_rows;
		status_.turn_rows = checkpoint.turn_rows;
		status_.river_rows = checkpoint.river_rows;
		status_.street_visits = checkpoint.street_visits;
		status_.visited_public_descriptors = checkpoint.generation.public_state_count;
		status_.admitted_rows = checkpoint.generation.row_count;
		status_.admitted_action_cells = checkpoint.generation.logical_value_count;
		status_.merged_stream_fingerprint = checkpoint.generation.merge_fingerprint;
		late_window_baseline_ = checkpoint.late_window_baseline;
	}

	void MultiwayBlueprintTrainingSession::resume_from_root_policy(const MultiwayBlueprintSnapshot& snapshot)
	{
		trainer_->resume_from_root_policy(snapshot);
	}

	void MultiwayBlueprintTrainingSession::resume_from_checkpoint(
		const MultiwayBlueprintTrainingCheckpoint& checkpoint)
	{
		trainer_->resume_from_checkpoint(checkpoint);
	}

	void MultiwayBlueprintTrainingSession::resume_from_external_checkpoint(
		const MultiwayExternalCheckpointManifest& checkpoint,
		std::uint64_t workflow_identity_hash,
		std::uint64_t storage_identity_hash)
	{
		trainer_->resume_from_external_checkpoint(checkpoint, workflow_identity_hash,
			storage_identity_hash);
	}

	MultiwayTrainingCheckpointWriteResult MultiwayBlueprintTrainingSession::save_checkpoint_atomic(
		const std::filesystem::path& path) const
	{
		return trainer_->save_checkpoint_atomic(path);
	}

	MultiwayExternalCheckpointManifest MultiwayBlueprintTrainingSession::save_external_checkpoint_atomic(
		const std::filesystem::path& path,
		std::uint64_t workflow_identity_hash,
		std::uint64_t storage_identity_hash) const
	{
		return trainer_->save_external_checkpoint_atomic(path, workflow_identity_hash,
			storage_identity_hash);
	}

	MultiwayBlueprintTrainingCheckpoint MultiwayBlueprintTrainingSession::checkpoint() const
	{
		auto result = trainer_->checkpoint();
		checkpoint_buffers_ = result.coordinator.memory_snapshot();
		return result;
	}
}
