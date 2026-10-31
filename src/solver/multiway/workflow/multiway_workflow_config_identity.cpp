#include "solver/multiway/workflow/multiway_workflow_config.hpp"
#include "core/fingerprint.hpp"

#include <cstdint>

namespace texas::solver::multiway
{
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
			const auto& identity_directory = identity_storage_directory.empty()
				? storage_directory
				: identity_storage_directory;
			const auto directory = identity_directory.lexically_normal().generic_u8string();
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

	std::uint64_t MultiwayWorkflowConfig::external_storage_fingerprint() const
	{
		if (storage_backend != "ExternalCompactInt32")
			throw std::invalid_argument("external storage identity requires ExternalCompactInt32");
		auto hash = core::fingerprint::FNV1A_OFFSET;
		const auto append = [&hash](std::uint64_t value) noexcept { core::fingerprint::append_u64(hash, value); };
		const auto& identity_directory = identity_storage_directory.empty()
			? storage_directory
			: identity_storage_directory;
		const auto directory = identity_directory.lexically_normal().generic_u8string();
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
		return core::fingerprint::finish(hash);
	}
} // namespace texas::solver::multiway
