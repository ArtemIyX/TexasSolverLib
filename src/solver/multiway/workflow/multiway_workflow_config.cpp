#include "solver/multiway/workflow/multiway_workflow_config.hpp"
#include "core/fingerprint.hpp"

#include <algorithm>
#include <fstream>
#include <limits>
#include <set>
#include <sstream>
#include <stdexcept>

namespace texas::solver::multiway
{
	namespace
	{
		std::string trim(std::string value)
		{
			const auto first = value.find_first_not_of(" \t\r\n");
			if (first == std::string::npos)
				return {};
			const auto last = value.find_last_not_of(" \t\r\n");
			return value.substr(first, last - first + 1U);
		}
		std::uint64_t number(const std::string& value, const char* key)
		{
			if (value.empty() || value[0] == '-')
				throw std::invalid_argument(std::string("invalid value for ") + key);
			std::size_t consumed = 0U;
			std::uint64_t result = 0U;
			try
			{
				result = std::stoull(value, &consumed, 10);
			}
			catch (const std::out_of_range&)
			{
				throw std::invalid_argument(std::string("value overflows ") + key);
			}
			if (consumed != value.size())
				throw std::invalid_argument(std::string("invalid value for ") + key);
			return result;
		}
		void expect(std::uint64_t actual, std::uint64_t expected, const char* key)
		{
			if (actual != expected)
				throw std::invalid_argument(std::string("invalid value for ") + key);
		}
		constexpr std::uint8_t kRegretCapacity = 1U;
		constexpr std::uint8_t kContinuationCapacity = 2U;
		constexpr std::uint8_t kDiscoveryPublicCapacity = 4U;
		constexpr std::uint8_t kDiscoveryRowsCapacity = 8U;

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
		if ((schema_version != 1U && schema_version != 2U) || profile_id.empty() || preflop_classes != 169U ||
			(storage_backend != "CompactInt32" && storage_backend != "ExternalCompactInt32") ||
			(profile_kind != MultiwayWorkflowProfileKind::Sizing && profile_kind != MultiwayWorkflowProfileKind::Acceptance) || max_decision_depth != 64U || max_public_chance_depth != 3U || deterministic_seed != 1U || training_worker_count == 0U || training_worker_count > 16U || !multiway_scheduler_mode_valid(scheduler_mode) || dynamic_chunk_size == 0U || (schema_version == 1U && training_worker_count != 1U) || target_trajectories == 0U || (profile_kind == MultiwayWorkflowProfileKind::Acceptance && target_trajectories != 50'000'000U) || (profile_kind == MultiwayWorkflowProfileKind::Sizing && target_trajectories >= 50'000'000U))
		{
			throw std::invalid_argument("invalid F1 workflow configuration");
		}
		if (storage_backend == "ExternalCompactInt32" &&
			(storage_directory.empty() || public_catalog_cache_bytes == 0U || row_page_cache_bytes == 0U ||
				maximum_wal_bytes == 0U || segment_target_bytes == 0U || compaction_fan_in < 2U ||
				compaction_scratch_bytes == 0U || disk_reserve_bytes == 0U))
			throw std::invalid_argument("external compact storage requires a directory and positive bounded resources");
		if (storage_backend == "CompactInt32" &&
			(!storage_directory.empty() || public_catalog_cache_bytes != 0U || row_page_cache_bytes != 0U ||
				maximum_wal_bytes != 0U || segment_target_bytes != 0U || compaction_fan_in != 0U ||
				compaction_scratch_bytes != 0U || disk_reserve_bytes != 0U || !synchronous_durability))
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
		if (storage_backend == "ExternalCompactInt32" &&
			(disk_space_requirement_bytes > std::numeric_limits<std::uint64_t>::max() - disk_reserve_bytes ||
				disk_space_requirement_bytes + disk_reserve_bytes > resource_limits.max_disk_space_requirement_bytes))
			throw std::invalid_argument("external storage disk requirement plus reserve exceeds resource limit");
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
		return (explicit_stream_capacity_mask & kRegretCapacity) != 0U || max_regret_delta_entries_per_worker != 0U
			? max_regret_delta_entries_per_worker
			: worker_delta_capacity;
	}

	std::uint64_t MultiwayWorkflowConfig::resolved_continuation_delta_capacity() const noexcept
	{
		return (explicit_stream_capacity_mask & kContinuationCapacity) != 0U || max_continuation_delta_entries_per_worker != 0U
			? max_continuation_delta_entries_per_worker
			: worker_delta_capacity;
	}

	std::uint64_t MultiwayWorkflowConfig::resolved_discovery_public_state_capacity() const noexcept
	{
		return (explicit_stream_capacity_mask & kDiscoveryPublicCapacity) != 0U || max_discovery_public_states_per_worker != 0U
			? max_discovery_public_states_per_worker
			: discovery_capacity;
	}

	std::uint64_t MultiwayWorkflowConfig::resolved_discovery_row_capacity() const noexcept
	{
		return (explicit_stream_capacity_mask & kDiscoveryRowsCapacity) != 0U || max_discovery_rows_per_worker != 0U
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
		if (config.storage_backend == "ExternalCompactInt32")
		{
			result.accepted = false;
			result.rejected_category = "storage backend";
			result.message = "ExternalCompactInt32 storage is not ready for solver execution";
		}
		return result;
	}

	std::uint64_t MultiwayWorkflowConfig::fingerprint() const noexcept
	{
		auto hash = core::fingerprint::FNV1A_OFFSET;
		const auto append = [&hash](std::uint64_t value) noexcept { core::fingerprint::append_u64(hash, value); };
		append(schema_version);
		append(static_cast<std::uint64_t>(profile_kind));
		for (const auto character : profile_id)
			core::fingerprint::append_u8(hash, static_cast<std::uint8_t>(character));
		append(model.player_count);
		append(static_cast<std::uint64_t>(model.initial_stack_chips));
		append(static_cast<std::uint64_t>(model.small_blind_chips));
		append(static_cast<std::uint64_t>(model.big_blind_chips));
		append(static_cast<std::uint64_t>(model.ante_chips));
		append(model.rake_policy.identity());
		append(preflop_classes);
		for (const auto character : storage_backend)
			core::fingerprint::append_u8(hash, static_cast<std::uint8_t>(character));
		if (storage_backend == "ExternalCompactInt32")
		{
			const auto directory = storage_directory.lexically_normal().generic_u8string();
			append(directory.size());
			for (const auto character : directory)
				core::fingerprint::append_u8(hash, static_cast<std::uint8_t>(character));
			append(public_catalog_cache_bytes);
			append(row_page_cache_bytes);
			append(maximum_wal_bytes);
			append(segment_target_bytes);
			append(compaction_fan_in);
			append(compaction_scratch_bytes);
			append(disk_reserve_bytes);
			append(synchronous_durability ? 1U : 0U);
		}
		append(model.flop_bucket_count);
		append(model.turn_bucket_count);
		append(model.river_bucket_count);
		append(max_decision_depth);
		append(max_public_chance_depth);
		append(deterministic_seed);
		append(training_worker_count);
		append(MULTIWAY_SCHEDULER_POLICY_VERSION);
		append(MULTIWAY_STATIC_BLOCK_SCHEDULER_VERSION);
		append(static_cast<std::uint64_t>(scheduler_mode));
		append(dynamic_chunk_size);
		append(target_trajectories);
		append(maximum_public_states);
		append(maximum_sparse_rows);
		append(maximum_sparse_values);
		if (explicit_stream_capacity_mask != 0U || max_regret_delta_entries_per_worker != 0U || max_continuation_delta_entries_per_worker != 0U || max_discovery_public_states_per_worker != 0U || max_discovery_rows_per_worker != 0U)
		{
			append(resolved_regret_delta_capacity());
			append(resolved_continuation_delta_capacity());
			append(resolved_discovery_public_state_capacity());
			append(resolved_discovery_row_capacity());
		}
		else
		{
			append(worker_delta_capacity);
			append(discovery_capacity);
		}
		append(trajectories_per_batch);
		append(checkpoint_interval);
		append(disk_space_requirement_bytes);
		append(process_memory_limit_bytes);
		append(process_working_set_limit_bytes);
		append(process_working_set_resident_allowance_bytes);
		return core::fingerprint::finish(hash);
	}

	MultiwayWorkflowConfig parse_multiway_workflow_config(const std::string& text)
	{
		MultiwayWorkflowConfig result;
		std::set<std::string> seen;
		std::istringstream input(text);
		std::string line;
		while (std::getline(input, line))
		{
			line = trim(line);
			if (line.empty() || line[0] == '#')
				continue;
			const auto separator = line.find('=');
			if (separator == std::string::npos)
				throw std::invalid_argument("configuration line requires '='");
			const auto key = trim(line.substr(0, separator));
			const auto value = trim(line.substr(separator + 1U));
			if (!seen.insert(key).second)
				throw std::invalid_argument("duplicate configuration key: " + key);
			const auto n = [&] { return number(value, key.c_str()); };
			if (key == "schema_version")
				result.schema_version = static_cast<std::uint32_t>(n());
			else if (key == "profile_kind")
			{
				if (value == "sizing")
					result.profile_kind = MultiwayWorkflowProfileKind::Sizing;
				else if (value == "acceptance")
					result.profile_kind = MultiwayWorkflowProfileKind::Acceptance;
				else
					throw std::invalid_argument("invalid profile_kind");
			}
			else if (key == "profile_id")
				result.profile_id = value;
			else if (key == "players")
				result.model.player_count = static_cast<std::uint8_t>(n());
			else if (key == "initial_stack_chips")
				result.model.initial_stack_chips = static_cast<int>(n());
			else if (key == "small_blind_chips")
				result.model.small_blind_chips = static_cast<int>(n());
			else if (key == "big_blind_chips")
				result.model.big_blind_chips = static_cast<int>(n());
			else if (key == "ante_chips")
				result.model.ante_chips = static_cast<int>(n());
			else if (key == "rake")
				expect(n(), 0U, "rake");
			else if (key == "preflop_classes")
				result.preflop_classes = static_cast<std::uint32_t>(n());
			else if (key == "flop_buckets")
				result.model.flop_bucket_count = static_cast<std::uint32_t>(n());
			else if (key == "turn_buckets")
				result.model.turn_bucket_count = static_cast<std::uint32_t>(n());
			else if (key == "river_buckets")
				result.model.river_bucket_count = static_cast<std::uint32_t>(n());
			else if (key == "storage_backend")
				result.storage_backend = value;
			else if (key == "storage_directory")
				result.storage_directory = value;
			else if (key == "public_catalog_cache_bytes")
				result.public_catalog_cache_bytes = n();
			else if (key == "row_page_cache_bytes")
				result.row_page_cache_bytes = n();
			else if (key == "maximum_wal_bytes")
				result.maximum_wal_bytes = n();
			else if (key == "segment_target_bytes")
				result.segment_target_bytes = n();
			else if (key == "compaction_fan_in")
			{
				const auto fan_in = n();
				if (fan_in > std::numeric_limits<std::uint32_t>::max())
					throw std::invalid_argument("compaction_fan_in exceeds uint32 range");
				result.compaction_fan_in = static_cast<std::uint32_t>(fan_in);
			}
			else if (key == "compaction_scratch_bytes")
				result.compaction_scratch_bytes = n();
			else if (key == "disk_reserve_bytes")
				result.disk_reserve_bytes = n();
			else if (key == "synchronous_durability")
			{
				if (value == "true") result.synchronous_durability = true;
				else if (value == "false") result.synchronous_durability = false;
				else throw std::invalid_argument("invalid synchronous_durability");
			}
			else if (key == "max_decision_depth")
				result.max_decision_depth = static_cast<std::uint32_t>(n());
			else if (key == "max_public_chance_depth")
				result.max_public_chance_depth = static_cast<std::uint32_t>(n());
			else if (key == "deterministic_seed")
				result.deterministic_seed = n();
			else if (key == "reference_worker_count" || key == "training_worker_count" || key == "threads")
			{
				const auto workers = n();
				if (workers > 16U)
					throw std::invalid_argument("training worker count exceeds 16");
				result.training_worker_count = static_cast<std::uint32_t>(workers);
			}
			else if (key == "scheduler_mode")
			{
				if (value == "fixed")
					result.scheduler_mode = MultiwaySchedulerMode::Fixed;
				else if (value == "static-block")
					result.scheduler_mode = MultiwaySchedulerMode::StaticBlock;
				else if (value == "dynamic")
					result.scheduler_mode = MultiwaySchedulerMode::Dynamic;
				else
					throw std::invalid_argument("invalid scheduler_mode");
			}
			else if (key == "dynamic_chunk_size")
			{
				const auto chunk_size = n();
				if (chunk_size > std::numeric_limits<std::uint32_t>::max())
					throw std::invalid_argument("dynamic_chunk_size exceeds uint32 range");
				result.dynamic_chunk_size = static_cast<std::uint32_t>(chunk_size);
			}
			else if (key == "placement_policy")
				result.placement_policy = parse_multiway_worker_placement_policy(value.c_str());
			else if (key == "target_trajectories")
				result.target_trajectories = n();
			else if (key == "maximum_public_states" || key == "maximum_sparse_rows" || key == "maximum_sparse_values" || key == "worker_delta_capacity" || key == "discovery_capacity" || key == "max_regret_delta_entries_per_worker" || key == "max_continuation_delta_entries_per_worker" || key == "max_discovery_public_states_per_worker" || key == "max_discovery_rows_per_worker" || key == "trajectories_per_batch" || key == "checkpoint_interval" || key == "disk_space_requirement_bytes" || key == "process_memory_limit_bytes" || key == "process_working_set_limit_bytes" || key == "process_working_set_resident_allowance_bytes")
			{
				const auto capacity = value == "UNRESOLVED" ? 0U : number(value, key.c_str());
				if (key == "maximum_public_states")
					result.maximum_public_states = capacity;
				else if (key == "maximum_sparse_rows")
					result.maximum_sparse_rows = capacity;
				else if (key == "maximum_sparse_values")
					result.maximum_sparse_values = capacity;
				else if (key == "worker_delta_capacity")
					result.worker_delta_capacity = capacity;
				else if (key == "discovery_capacity")
					result.discovery_capacity = capacity;
				else if (key == "max_regret_delta_entries_per_worker")
				{
					result.max_regret_delta_entries_per_worker = capacity;
					result.explicit_stream_capacity_mask |= kRegretCapacity;
				}
				else if (key == "max_continuation_delta_entries_per_worker")
				{
					result.max_continuation_delta_entries_per_worker = capacity;
					result.explicit_stream_capacity_mask |= kContinuationCapacity;
				}
				else if (key == "max_discovery_public_states_per_worker")
				{
					result.max_discovery_public_states_per_worker = capacity;
					result.explicit_stream_capacity_mask |= kDiscoveryPublicCapacity;
				}
				else if (key == "max_discovery_rows_per_worker")
				{
					result.max_discovery_rows_per_worker = capacity;
					result.explicit_stream_capacity_mask |= kDiscoveryRowsCapacity;
				}
				else if (key == "trajectories_per_batch")
					result.trajectories_per_batch = capacity;
				else if (key == "checkpoint_interval")
					result.checkpoint_interval = capacity;
				else if (key == "disk_space_requirement_bytes")
					result.disk_space_requirement_bytes = capacity;
				else if (key == "process_working_set_limit_bytes")
					result.process_working_set_limit_bytes = capacity;
				else if (key == "process_working_set_resident_allowance_bytes")
					result.process_working_set_resident_allowance_bytes = capacity;
				else
					result.process_memory_limit_bytes = capacity;
			}
			else if (key == "input_path")
				result.training_input_path = value;
			else if (key == "output_path")
				result.training_output_path = value;
			else if (key == "report_path")
				result.training_report_path = value;
			else if (key == "checkpoint_dir")
				result.training_checkpoint_dir = value;
			else if (key == "batches")
				result.training_batches = n();
			else if (key == "log_path")
				result.training_log_path = value;
			else
				throw std::invalid_argument("unknown configuration key: " + key);
		}
		if (result.schema_version == 1U)
		{
			if (seen.count("reference_worker_count") == 0U || seen.count("training_worker_count") != 0U)
			{
				throw std::invalid_argument("schema 1 requires reference_worker_count");
			}
		}
		else if (result.schema_version == 2U)
		{
			if (seen.count("training_worker_count") == 0U || seen.count("reference_worker_count") != 0U)
			{
				throw std::invalid_argument("schema 2 requires training_worker_count");
			}
		}
		result.validate();
		return result;
	}

	std::string serialize_multiway_workflow_config(const MultiwayWorkflowConfig& config)
	{
		config.validate();
		std::ostringstream output;
		const auto line = [&output](const char* key, const auto& value) { output << key << '=' << value << '\n'; };
		line("schema_version", config.schema_version);
		line("profile_kind", config.profile_kind == MultiwayWorkflowProfileKind::Sizing ? "sizing" : "acceptance");
		line("profile_id", config.profile_id);
		line("players", static_cast<unsigned>(config.model.player_count));
		line("initial_stack_chips", config.model.initial_stack_chips);
		line("small_blind_chips", config.model.small_blind_chips);
		line("big_blind_chips", config.model.big_blind_chips);
		line("ante_chips", config.model.ante_chips);
		line("rake", 0U);
		line("preflop_classes", config.preflop_classes);
		line("flop_buckets", config.model.flop_bucket_count);
		line("turn_buckets", config.model.turn_bucket_count);
		line("river_buckets", config.model.river_bucket_count);
		line("storage_backend", config.storage_backend);
		if (config.storage_backend == "ExternalCompactInt32")
		{
			line("storage_directory", config.storage_directory.generic_string());
			line("public_catalog_cache_bytes", config.public_catalog_cache_bytes);
			line("row_page_cache_bytes", config.row_page_cache_bytes);
			line("maximum_wal_bytes", config.maximum_wal_bytes);
			line("segment_target_bytes", config.segment_target_bytes);
			line("compaction_fan_in", config.compaction_fan_in);
			line("compaction_scratch_bytes", config.compaction_scratch_bytes);
			line("disk_reserve_bytes", config.disk_reserve_bytes);
			line("synchronous_durability", config.synchronous_durability ? "true" : "false");
		}
		line("max_decision_depth", config.max_decision_depth);
		line("max_public_chance_depth", config.max_public_chance_depth);
		line("deterministic_seed", config.deterministic_seed);
		if (config.schema_version == 1U)
			line("reference_worker_count", config.training_worker_count);
		else
			line("training_worker_count", config.training_worker_count);
		line("scheduler_mode", multiway_scheduler_mode_name(config.scheduler_mode));
		line("dynamic_chunk_size", config.dynamic_chunk_size);
		line("placement_policy", multiway_worker_placement_policy_name(config.placement_policy));
		line("target_trajectories", config.target_trajectories);
		const auto unresolved = [](std::uint64_t value) { return value == 0U ? std::string("UNRESOLVED") : std::to_string(value); };
		line("maximum_public_states", unresolved(config.maximum_public_states));
		line("maximum_sparse_rows", unresolved(config.maximum_sparse_rows));
		line("maximum_sparse_values", unresolved(config.maximum_sparse_values));
		const bool explicit_capacities = config.explicit_stream_capacity_mask != 0U || config.max_regret_delta_entries_per_worker != 0U || config.max_continuation_delta_entries_per_worker != 0U || config.max_discovery_public_states_per_worker != 0U || config.max_discovery_rows_per_worker != 0U;
		if (explicit_capacities)
		{
			line("max_regret_delta_entries_per_worker", config.resolved_regret_delta_capacity());
			line("max_continuation_delta_entries_per_worker", config.resolved_continuation_delta_capacity());
			line("max_discovery_public_states_per_worker", config.resolved_discovery_public_state_capacity());
			line("max_discovery_rows_per_worker", config.resolved_discovery_row_capacity());
		}
		else
		{
			line("worker_delta_capacity", unresolved(config.worker_delta_capacity));
			line("discovery_capacity", unresolved(config.discovery_capacity));
		}
		line("trajectories_per_batch", unresolved(config.trajectories_per_batch));
		line("checkpoint_interval", unresolved(config.checkpoint_interval));
		line("disk_space_requirement_bytes", unresolved(config.disk_space_requirement_bytes));
		line("process_memory_limit_bytes", unresolved(config.process_memory_limit_bytes));
		line("process_working_set_limit_bytes", config.process_working_set_limit_bytes);
		line("process_working_set_resident_allowance_bytes", config.process_working_set_resident_allowance_bytes);
		if (!config.training_input_path.empty())
			line("input_path", config.training_input_path.string());
		if (!config.training_output_path.empty())
			line("output_path", config.training_output_path.string());
		if (!config.training_report_path.empty())
			line("report_path", config.training_report_path.string());
		if (!config.training_checkpoint_dir.empty())
			line("checkpoint_dir", config.training_checkpoint_dir.string());
		if (config.training_batches != 0U)
			line("batches", config.training_batches);
		if (!config.training_log_path.empty())
			line("log_path", config.training_log_path.string());
		return output.str();
	}

	MultiwayWorkflowConfig load_multiway_workflow_config(const std::filesystem::path& path)
	{
		std::ifstream input(path);
		if (!input)
			throw std::runtime_error("cannot open workflow configuration");
		return parse_multiway_workflow_config({ std::istreambuf_iterator<char>(input), {} });
	}
} // namespace texas::solver::multiway
