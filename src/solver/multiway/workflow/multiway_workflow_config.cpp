#include "solver/multiway/workflow/multiway_workflow_config.hpp"
#include "multiway_workflow_config_detail.hpp"

#include <algorithm>
#include <cstddef>
#include <limits>
#include <stdexcept>
#include <string>

namespace texas::solver::multiway
{
	namespace
	{

		std::uint64_t checked_multiply(
			std::uint64_t left, std::uint64_t right, const char* message)
		{
			if (left != 0U && right > std::numeric_limits<std::uint64_t>::max() / left)
				throw std::length_error(message);
			return left * right;
		}
	} // namespace

	void MultiwayWorkflowConfig::validate() const
	{
		validate(MultiwayWorkflowResourceLimits{});
	}

	void MultiwayWorkflowResourceLimits::validate() const
	{
		if (max_training_workers == 0U || max_training_workers > 16U || max_public_states == 0U || max_sparse_rows == 0U || max_sparse_values == 0U || max_stream_entries_per_worker == 0U || max_trajectories_per_batch == 0U || max_checkpoint_interval == 0U || max_disk_space_requirement_bytes == 0U || max_process_memory_limit_bytes == 0U || max_process_working_set_limit_bytes == 0U || max_training_batches == 0U)
			throw std::invalid_argument("workflow resource limits must be positive and use at most 16 workers");
		if (max_process_working_set_resident_allowance_bytes > max_process_working_set_limit_bytes)
			throw std::invalid_argument("workflow resident allowance exceeds its working-set limit");
	}

	void MultiwayWorkflowConfig::validate(const MultiwayWorkflowResourceLimits& resource_limits) const
	{
		resource_limits.validate();
		if ((schema_version != 1U && schema_version != 2U) || profile_id.empty() || preflop_classes != 169U || (storage_backend != "CompactInt32" && storage_backend != "ExternalCompactInt32") || (profile_kind != MultiwayWorkflowProfileKind::Sizing && profile_kind != MultiwayWorkflowProfileKind::Acceptance) || max_decision_depth != 64U || max_public_chance_depth != 3U || deterministic_seed != 1U || training_worker_count == 0U || training_worker_count > 16U || !multiway_scheduler_mode_valid(scheduler_mode) || dynamic_chunk_size == 0U || (schema_version == 1U && training_worker_count != 1U) || target_trajectories == 0U || (profile_kind == MultiwayWorkflowProfileKind::Acceptance && target_trajectories != 50'000'000U) || (profile_kind == MultiwayWorkflowProfileKind::Sizing && target_trajectories >= 50'000'000U))
		{
			throw std::invalid_argument("invalid F1 workflow configuration");
		}
		if (storage_backend == "ExternalCompactInt32" && (storage_directory.empty() || public_catalog_cache_bytes == 0U || row_page_cache_bytes == 0U || maximum_wal_bytes == 0U || segment_target_bytes == 0U || compaction_fan_in < 2U || compaction_scratch_bytes == 0U || disk_reserve_bytes == 0U))
			throw std::invalid_argument("external compact storage requires a directory and positive bounded resources");
		if (storage_backend == "CompactInt32" && (!storage_directory.empty() || public_catalog_cache_bytes != 0U || row_page_cache_bytes != 0U || maximum_wal_bytes != 0U || segment_target_bytes != 0U || compaction_fan_in != 0U || compaction_scratch_bytes != 0U || disk_reserve_bytes != 0U || !synchronous_durability))
			throw std::invalid_argument("external storage settings require ExternalCompactInt32");
		model.validate();
		MultiwayWorkerPlacementConfig{ placement_policy, 0U }.validate();
		if (model.player_count != 6U || model.initial_stack_chips != 10000 || model.small_blind_chips != 50 || model.big_blind_chips != 100 || model.ante_chips != 0)
			throw std::invalid_argument("F1 game rules profile mismatch");
		const auto any_capacity = maximum_public_states != 0U || maximum_sparse_rows != 0U || maximum_sparse_values != 0U || worker_delta_capacity != 0U || discovery_capacity != 0U || explicit_stream_capacity_mask != 0U || max_regret_delta_entries_per_worker != 0U || max_continuation_delta_entries_per_worker != 0U || max_discovery_public_states_per_worker != 0U || max_discovery_rows_per_worker != 0U || trajectories_per_batch != 0U || checkpoint_interval != 0U || disk_space_requirement_bytes != 0U || process_memory_limit_bytes != 0U;
		if (any_capacity && !capacities_resolved())
		{
			throw std::invalid_argument("F1 workflow capacities must be fully resolved together");
		}
		if (capacities_resolved() && (trajectories_per_batch > std::numeric_limits<std::uint32_t>::max() || maximum_public_states > std::numeric_limits<std::size_t>::max() || maximum_sparse_rows > std::numeric_limits<std::size_t>::max() || maximum_sparse_values > std::numeric_limits<std::size_t>::max() || resolved_regret_delta_capacity() > std::numeric_limits<std::size_t>::max() || resolved_continuation_delta_capacity() > std::numeric_limits<std::size_t>::max() || resolved_discovery_public_state_capacity() > std::numeric_limits<std::size_t>::max() || resolved_discovery_row_capacity() > std::numeric_limits<std::size_t>::max()))
		{
			throw std::invalid_argument("F1 workflow capacity exceeds the runtime type");
		}
		if (process_working_set_limit_bytes == 0U && process_working_set_resident_allowance_bytes != 0U)
			throw std::invalid_argument("working-set resident allowance requires a hard limit");
		const auto reject_above = [](std::uint64_t value, std::uint64_t maximum, const char* key) {
			if (value != 0U && value > maximum)
				throw std::invalid_argument(std::string(key) + " exceeds resource limit: requested=" + std::to_string(value) + ", maximum=" + std::to_string(maximum));
		};
		reject_above(maximum_public_states, resource_limits.max_public_states, "maximum_public_states");
		reject_above(maximum_sparse_rows, resource_limits.max_sparse_rows, "maximum_sparse_rows");
		reject_above(maximum_sparse_values, resource_limits.max_sparse_values, "maximum_sparse_values");
		reject_above(resolved_regret_delta_capacity(), resource_limits.max_stream_entries_per_worker, "regret_delta_capacity");
		reject_above(resolved_continuation_delta_capacity(), resource_limits.max_stream_entries_per_worker, "continuation_delta_capacity");
		reject_above(resolved_discovery_public_state_capacity(), resource_limits.max_stream_entries_per_worker, "discovery_public_state_capacity");
		reject_above(resolved_discovery_row_capacity(), resource_limits.max_stream_entries_per_worker, "discovery_row_capacity");
		reject_above(trajectories_per_batch, resource_limits.max_trajectories_per_batch, "trajectories_per_batch");
		reject_above(checkpoint_interval, resource_limits.max_checkpoint_interval, "checkpoint_interval");
		reject_above(disk_space_requirement_bytes, resource_limits.max_disk_space_requirement_bytes, "disk_space_requirement_bytes");
		reject_above(disk_reserve_bytes, resource_limits.max_disk_space_requirement_bytes, "disk_reserve_bytes");
		if (storage_backend == "ExternalCompactInt32" && ([&]() {
				auto required = disk_space_requirement_bytes;
				const auto add = [&required](std::uint64_t value) {
					if (value > std::numeric_limits<std::uint64_t>::max() - required)
						return false;
					required += value;
					return true;
				};
				return !add(maximum_wal_bytes) || !add(segment_target_bytes) || compaction_scratch_bytes > std::numeric_limits<std::uint64_t>::max() / 2U || !add(compaction_scratch_bytes * 2U) || !add(disk_reserve_bytes) || required > resource_limits.max_disk_space_requirement_bytes;
			})())
			throw std::invalid_argument("external storage live, WAL, compaction, and reserve bytes exceed disk resource limit");
		reject_above(process_memory_limit_bytes, resource_limits.max_process_memory_limit_bytes, "process_memory_limit_bytes");
		reject_above(process_working_set_limit_bytes, resource_limits.max_process_working_set_limit_bytes, "process_working_set_limit_bytes");
		reject_above(process_working_set_resident_allowance_bytes, resource_limits.max_process_working_set_resident_allowance_bytes, "process_working_set_resident_allowance_bytes");
		reject_above(training_batches, resource_limits.max_training_batches, "batches");
	}

	bool MultiwayWorkflowConfig::capacities_resolved() const noexcept
	{
		return maximum_public_states != 0U && maximum_sparse_rows != 0U && maximum_sparse_values != 0U && resolved_regret_delta_capacity() != 0U && resolved_continuation_delta_capacity() != 0U && resolved_discovery_public_state_capacity() != 0U && resolved_discovery_row_capacity() != 0U && trajectories_per_batch != 0U && checkpoint_interval != 0U && disk_space_requirement_bytes != 0U && process_memory_limit_bytes != 0U;
	}

	std::uint64_t MultiwayWorkflowConfig::resolved_regret_delta_capacity() const noexcept
	{
		return (explicit_stream_capacity_mask & workflow_config_detail::kRegretCapacity) != 0U || max_regret_delta_entries_per_worker != 0U
			? max_regret_delta_entries_per_worker
			: worker_delta_capacity;
	}

	std::uint64_t MultiwayWorkflowConfig::resolved_continuation_delta_capacity() const noexcept
	{
		return (explicit_stream_capacity_mask & workflow_config_detail::kContinuationCapacity) != 0U || max_continuation_delta_entries_per_worker != 0U
			? max_continuation_delta_entries_per_worker
			: worker_delta_capacity;
	}

	std::uint64_t MultiwayWorkflowConfig::resolved_discovery_public_state_capacity() const noexcept
	{
		return (explicit_stream_capacity_mask & workflow_config_detail::kDiscoveryPublicCapacity) != 0U || max_discovery_public_states_per_worker != 0U
			? max_discovery_public_states_per_worker
			: discovery_capacity;
	}

	std::uint64_t MultiwayWorkflowConfig::resolved_discovery_row_capacity() const noexcept
	{
		return (explicit_stream_capacity_mask & workflow_config_detail::kDiscoveryRowsCapacity) != 0U || max_discovery_rows_per_worker != 0U
			? max_discovery_rows_per_worker
			: discovery_capacity;
	}

	MultiwayTrainingWorkerResolution resolve_multiway_training_workers(
		std::uint32_t configured_workers,
		std::uint32_t override_workers,
		std::uint64_t trajectories_per_batch)
	{
		const auto requested = override_workers == 0U ? configured_workers : override_workers;
		if (requested == 0U || requested > 16U || trajectories_per_batch == 0U)
		{
			throw std::invalid_argument("training worker count must be in [1, 16] with a positive batch size");
		}
		return { requested, static_cast<std::uint32_t>(std::min<std::uint64_t>(requested, trajectories_per_batch)) };
	}

	MultiwayWorkflowResourcePreflight preflight_multiway_workflow(
		const MultiwayWorkflowConfig& config,
		std::uint32_t worker_override,
		std::uint64_t batches,
		const MultiwayWorkflowResourceLimits& resource_limits)
	{
		config.validate(resource_limits);
		if (!config.capacities_resolved())
			throw std::invalid_argument("workflow resource preflight requires resolved capacities");
		if (batches == 0U)
			throw std::invalid_argument("workflow resource preflight requires positive batches");
		if (batches > resource_limits.max_training_batches)
			throw std::invalid_argument("batches exceeds resource limit: requested=" + std::to_string(batches) + ", maximum=" + std::to_string(resource_limits.max_training_batches));

		MultiwayWorkflowResourcePreflight result;
		result.workers = resolve_multiway_training_workers(
			config.training_worker_count, worker_override, config.trajectories_per_batch);
		if (result.workers.requested > resource_limits.max_training_workers)
			throw std::invalid_argument("training workers exceed resource limit: requested=" + std::to_string(result.workers.requested) + ", maximum=" + std::to_string(resource_limits.max_training_workers));
		result.requested_training_trajectories = checked_multiply(
			batches, config.trajectories_per_batch, "workflow trajectory count overflows uint64");
		if (result.requested_training_trajectories > config.target_trajectories)
		{
			result.rejected_category = "workload";
			result.message = "requested training workload exceeds target trajectories";
			return result;
		}
		if (config.process_memory_limit_bytes < 3U)
			throw std::invalid_argument("process_memory_limit_bytes must be at least 3 bytes");
		const auto operating_bytes = config.process_memory_limit_bytes - 1U;
		const auto warning_bytes = std::max<std::uint64_t>(1U, operating_bytes - operating_bytes / 5U);
		MultiwaySolverLimits solver_limits;
		solver_limits.worker_count = result.workers.effective;
		solver_limits.trajectories_per_batch = static_cast<std::uint32_t>(config.trajectories_per_batch);
		solver_limits.max_public_states = static_cast<std::size_t>(config.maximum_public_states);
		solver_limits.max_sparse_rows = static_cast<std::size_t>(config.maximum_sparse_rows);
		solver_limits.max_sparse_values = static_cast<std::size_t>(config.maximum_sparse_values);
		solver_limits.max_regret_delta_entries_per_worker = static_cast<std::size_t>(config.resolved_regret_delta_capacity());
		solver_limits.max_continuation_delta_entries_per_worker = static_cast<std::size_t>(config.resolved_continuation_delta_capacity());
		solver_limits.max_discovery_public_states_per_worker = static_cast<std::size_t>(config.resolved_discovery_public_state_capacity());
		solver_limits.max_discovery_rows_per_worker = static_cast<std::size_t>(config.resolved_discovery_row_capacity());
		solver_limits.storage_backend = config.storage_backend == "ExternalCompactInt32"
			? MultiwaySolverLimits::StorageBackend::ExternalCompactInt32
			: MultiwaySolverLimits::StorageBackend::CompactInt32;
		if (solver_limits.storage_backend == MultiwaySolverLimits::StorageBackend::ExternalCompactInt32)
		{
			solver_limits.external_storage.directory = config.storage_directory;
			solver_limits.external_storage.public_catalog_cache_bytes = config.public_catalog_cache_bytes;
			solver_limits.external_storage.row_page_cache_bytes = config.row_page_cache_bytes;
			solver_limits.external_storage.maximum_wal_bytes = config.maximum_wal_bytes;
			solver_limits.external_storage.segment_target_bytes = config.segment_target_bytes;
			solver_limits.external_storage.compaction_fan_in = config.compaction_fan_in;
			solver_limits.external_storage.compaction_scratch_bytes = config.compaction_scratch_bytes;
			solver_limits.external_storage.disk_reserve_bytes = config.disk_reserve_bytes;
			solver_limits.external_storage.synchronous_durability = config.synchronous_durability;
		}
		MultiwayMemoryInputs memory_inputs;
		memory_inputs.disk_output_bytes = config.disk_space_requirement_bytes;
		if (solver_limits.storage_backend == MultiwaySolverLimits::StorageBackend::ExternalCompactInt32)
		{
			if (config.public_catalog_cache_bytes > std::numeric_limits<std::uint64_t>::max() - config.row_page_cache_bytes)
				throw std::length_error("external cache byte count overflows");
			memory_inputs.external_storage_cache_bytes = config.public_catalog_cache_bytes + config.row_page_cache_bytes;
			if (config.compaction_scratch_bytes > std::numeric_limits<std::uint64_t>::max() - config.maximum_wal_bytes)
				throw std::length_error("external storage scratch byte count overflows");
			memory_inputs.external_storage_scratch_bytes = config.compaction_scratch_bytes + config.maximum_wal_bytes;
			memory_inputs.external_storage_disk_reserve_bytes = config.disk_reserve_bytes;
		}
		result.memory = preflight_multiway_memory(
			solver_limits, { warning_bytes, config.process_memory_limit_bytes, operating_bytes }, memory_inputs);
		result.accepted = result.memory.status != MultiwayMemoryStatus::Rejected;
		result.rejected_category = result.accepted ? "none" : result.memory.rejected_category;
		result.message = result.accepted ? "workflow resource preflight admitted" : result.memory.message;
		return result;
	}

} // namespace texas::solver::multiway
