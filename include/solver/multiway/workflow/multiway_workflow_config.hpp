#pragma once

#include "solver/multiway/blueprint/multiway_blueprint_config.hpp"
#include "solver/multiway/engine/multiway_memory.hpp"
#include "solver/multiway/engine/multiway_worker_topology.hpp"
#include "solver/multiway/workflow/multiway_evidence.hpp"

#include <cstdint>
#include <filesystem>
#include <string>

namespace texas::solver::multiway
{

	// Configuration ceiling; runtime memory and working-set gates remain authoritative.
	inline constexpr std::uint64_t MULTIWAY_WORKFLOW_MAX_PUBLIC_STATES = 34'000'000U;
	inline constexpr std::uint64_t MULTIWAY_WORKFLOW_MAX_SPARSE_ROWS = 8'000'000U;
	inline constexpr std::uint64_t MULTIWAY_WORKFLOW_MAX_SPARSE_VALUES = 200'000'000U;
	inline constexpr std::uint64_t MULTIWAY_WORKFLOW_MAX_STREAM_ENTRIES_PER_WORKER = 1'000'000U;
	inline constexpr std::uint64_t MULTIWAY_WORKFLOW_MAX_TRAJECTORIES_PER_BATCH = 1'000'000U;
	inline constexpr std::uint64_t MULTIWAY_WORKFLOW_MAX_CHECKPOINT_INTERVAL = 1'000'000'000U;
	inline constexpr std::uint64_t MULTIWAY_WORKFLOW_MAX_DISK_SPACE_BYTES = 1ULL * 1024ULL * 1024ULL * 1024ULL * 1024ULL;
	inline constexpr std::uint64_t MULTIWAY_WORKFLOW_MAX_PROCESS_MEMORY_BYTES =
		MULTIWAY_COMPAT_PROCESS_WORKING_SET_LIMIT_BYTES;
	inline constexpr std::uint64_t MULTIWAY_WORKFLOW_MAX_WORKING_SET_BYTES =
		MULTIWAY_COMPAT_PROCESS_WORKING_SET_LIMIT_BYTES;
	inline constexpr std::uint64_t MULTIWAY_WORKFLOW_MAX_BATCHES = 50'000'000U;

	struct MultiwayWorkflowResourceLimits
	{
		std::uint32_t max_training_workers = 16U;
		std::uint64_t max_public_states = MULTIWAY_WORKFLOW_MAX_PUBLIC_STATES;
		std::uint64_t max_sparse_rows = MULTIWAY_WORKFLOW_MAX_SPARSE_ROWS;
		std::uint64_t max_sparse_values = MULTIWAY_WORKFLOW_MAX_SPARSE_VALUES;
		std::uint64_t max_stream_entries_per_worker = MULTIWAY_WORKFLOW_MAX_STREAM_ENTRIES_PER_WORKER;
		std::uint64_t max_trajectories_per_batch = MULTIWAY_WORKFLOW_MAX_TRAJECTORIES_PER_BATCH;
		std::uint64_t max_checkpoint_interval = MULTIWAY_WORKFLOW_MAX_CHECKPOINT_INTERVAL;
		std::uint64_t max_disk_space_requirement_bytes = MULTIWAY_WORKFLOW_MAX_DISK_SPACE_BYTES;
		std::uint64_t max_process_memory_limit_bytes = MULTIWAY_WORKFLOW_MAX_PROCESS_MEMORY_BYTES;
		std::uint64_t max_process_working_set_limit_bytes = MULTIWAY_WORKFLOW_MAX_WORKING_SET_BYTES;
		std::uint64_t max_process_working_set_resident_allowance_bytes = MULTIWAY_WORKFLOW_MAX_WORKING_SET_BYTES;
		std::uint64_t max_training_batches = MULTIWAY_WORKFLOW_MAX_BATCHES;

		void validate() const;
	};

	struct MultiwayWorkflowConfig
	{
		std::uint32_t schema_version = 0U;
		MultiwayWorkflowProfileKind profile_kind = MultiwayWorkflowProfileKind::Acceptance;
		std::string profile_id;
		MultiwayBlueprintConfig model;
		std::uint32_t preflop_classes = 0U;
		std::string storage_backend;
		std::filesystem::path storage_directory;
		std::uint64_t public_catalog_cache_bytes = 0U;
		std::uint64_t row_page_cache_bytes = 0U;
		std::uint64_t maximum_wal_bytes = 0U;
		std::uint64_t segment_target_bytes = 0U;
		std::uint32_t compaction_fan_in = 0U;
		std::uint64_t compaction_scratch_bytes = 0U;
		std::uint64_t disk_reserve_bytes = 0U;
		bool synchronous_durability = true;
		std::uint32_t max_decision_depth = 0U;
		std::uint32_t max_public_chance_depth = 0U;
		std::uint64_t deterministic_seed = 0U;
		std::uint32_t training_worker_count = 0U;
		MultiwaySchedulerMode scheduler_mode = MultiwaySchedulerMode::Fixed;
		std::uint32_t dynamic_chunk_size = MULTIWAY_DYNAMIC_SCHEDULER_CHUNK_SIZE;
		MultiwayWorkerPlacementPolicy placement_policy = MultiwayWorkerPlacementPolicy::OsDefault;
		std::uint64_t target_trajectories = 0U;
		std::uint64_t maximum_public_states = 0U;
		std::uint64_t maximum_sparse_rows = 0U;
		std::uint64_t maximum_sparse_values = 0U;
		std::uint64_t worker_delta_capacity = 0U;
		std::uint64_t discovery_capacity = 0U;
		std::uint64_t max_regret_delta_entries_per_worker = 0U;
		std::uint64_t max_continuation_delta_entries_per_worker = 0U;
		std::uint64_t max_discovery_public_states_per_worker = 0U;
		std::uint64_t max_discovery_rows_per_worker = 0U;
		std::uint64_t trajectories_per_batch = 0U;
		std::uint64_t checkpoint_interval = 0U;
		std::uint64_t disk_space_requirement_bytes = 0U;
		std::uint64_t process_memory_limit_bytes = 0U;
		// The legacy process memory limit remains a configurable 64 GiB-style
		// accounting limit. This separate gate defaults to a conservative 48 GiB.
		std::uint64_t process_working_set_limit_bytes = MULTIWAY_DEFAULT_PROCESS_WORKING_SET_LIMIT_BYTES;
		std::uint64_t process_working_set_resident_allowance_bytes = 0U;
		std::filesystem::path training_input_path;
		std::filesystem::path training_output_path;
		std::filesystem::path training_report_path;
		std::filesystem::path training_checkpoint_dir;
		std::uint64_t training_batches = 0U;
		std::filesystem::path training_log_path;

		void validate() const;
		void validate(const MultiwayWorkflowResourceLimits& resource_limits) const;
		[[nodiscard]] bool capacities_resolved() const noexcept;
		[[nodiscard]] std::uint64_t fingerprint() const noexcept;
		[[nodiscard]] std::uint64_t external_storage_fingerprint() const;
		[[nodiscard]] std::uint64_t resolved_regret_delta_capacity() const noexcept;
		[[nodiscard]] std::uint64_t resolved_continuation_delta_capacity() const noexcept;
		[[nodiscard]] std::uint64_t resolved_discovery_public_state_capacity() const noexcept;
		[[nodiscard]] std::uint64_t resolved_discovery_row_capacity() const noexcept;
		// Parser-only provenance. Zero means the legacy aggregate keys were used.
		std::uint8_t explicit_stream_capacity_mask = 0U;
	};

	struct MultiwayTrainingWorkerResolution
	{
		std::uint32_t requested = 0U;
		std::uint32_t effective = 0U;
	};

	struct MultiwayWorkflowResourcePreflight
	{
		MultiwayTrainingWorkerResolution workers{};
		std::uint64_t requested_training_trajectories = 0U;
		MultiwayMemoryPreflight memory{};
		bool accepted = false;
		const char* rejected_category = "none";
		const char* message = "not run";
	};

	[[nodiscard]] MultiwayTrainingWorkerResolution resolve_multiway_training_workers(
		std::uint32_t configured_workers,
		std::uint32_t override_workers,
		std::uint64_t trajectories_per_batch);

	[[nodiscard]] MultiwayWorkflowResourcePreflight preflight_multiway_workflow(
		const MultiwayWorkflowConfig& config,
		std::uint32_t worker_override,
		std::uint64_t batches,
		const MultiwayWorkflowResourceLimits& resource_limits = {});

	[[nodiscard]] MultiwayWorkflowConfig parse_multiway_workflow_config(const std::string& text);
	[[nodiscard]] std::string serialize_multiway_workflow_config(const MultiwayWorkflowConfig& config);
	[[nodiscard]] MultiwayWorkflowConfig load_multiway_workflow_config(const std::filesystem::path& path);

} // namespace texas::solver::multiway
