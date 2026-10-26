#pragma once

#include "solver/multiway/blueprint/multiway_blueprint_trainer.hpp"
#include "solver/multiway/workflow/multiway_evidence.hpp"

#include <filesystem>
#include <array>
#include <string>

namespace texas::solver::multiway
{

	struct MultiwayTrainingReport
	{
		MultiwayEvidenceHeader evidence{
			MULTIWAY_TRAINING_REPORT_SCHEMA_VERSION,
			MultiwayWorkflowProfileKind::Acceptance,
			{}
		};
		std::string instrumentation_kind = "none";
		std::string tracy_version;
		std::string trace_capture_id;
		bool allocation_probe_valid = true;
		bool tracy_memory_events_enabled = false;
		std::uint64_t elapsed_wall_nanoseconds = 0U;
		std::uint64_t training_wall_nanoseconds = 0U;
		std::uint64_t checkpoint_wall_nanoseconds = 0U;
		std::uint64_t export_wall_nanoseconds = 0U;
		std::uint64_t total_wrapper_wall_nanoseconds = 0U;
		bool total_wrapper_wall_available = false;
		std::uint64_t current_process_rss_bytes = 0U;
		// Legacy field. With phase snapshots, this is the authoritative process
		// working-set high-water mark, not a sampled current-working-set peak.
		std::uint64_t peak_process_rss_bytes = 0U;
		std::uint64_t sampled_process_working_set_peak_bytes = 0U;
		std::uint64_t current_working_set_bytes = 0U;
		std::uint64_t peak_process_working_set_bytes = 0U;
		std::uint64_t private_committed_bytes = 0U;
		std::uint64_t peak_private_committed_bytes = 0U;
		std::uint64_t minor_page_faults = 0U;
		std::uint64_t major_page_faults = 0U;
		std::uint64_t total_page_faults = 0U;
		bool current_working_set_available = false;
		bool peak_process_working_set_available = false;
		bool private_committed_available = false;
		bool peak_private_committed_available = false;
		bool minor_page_faults_available = false;
		bool major_page_faults_available = false;
		bool total_page_faults_available = false;
		std::uint64_t process_memory_limit_bytes = 0U;
		MultiwayWorkingSetGateConfig working_set_gate_config{};
		MultiwayWorkingSetGateResult working_set_gate{};
		MultiwaySystemMemoryGuardConfig system_memory_guard_config{};
		MultiwaySystemMemoryGuardResult system_memory_guard{};
		bool process_rss_available = false;
		bool started_at_preflop = false;
		std::uint64_t peak_public_states = 0U;
		std::uint64_t peak_sparse_rows = 0U;
		std::uint64_t peak_sparse_values = 0U;
		std::uint64_t peak_worker_delta_entries = 0U;
		std::uint64_t total_peak_regret_delta_entries = 0U;
		std::uint64_t total_peak_continuation_delta_entries = 0U;
		std::uint64_t total_peak_delta_entries = 0U;
		std::uint64_t generated_update_count = 0U;
		std::uint64_t generated_discovery_public_state_count = 0U;
		std::uint64_t generated_discovery_row_count = 0U;
		std::uint64_t coordinator_admission_nanoseconds = 0U;
		MultiwayAdmissionDiagnostics admission_diagnostics{};
		std::uint64_t trainer_only_nanoseconds = 0U;
		std::uint64_t process_cpu_nanoseconds = 0U;
		std::uint64_t peak_working_set_bytes = 0U;
		std::uint64_t peak_compact_storage_bytes = 0U;
		std::uint64_t configured_max_public_states = 0U;
		std::uint64_t configured_max_sparse_rows = 0U;
		std::uint64_t configured_max_sparse_values = 0U;
		std::uint64_t configured_worker_delta_capacity = 0U;
		std::uint64_t configured_discovery_capacity = 0U;
		std::uint64_t configured_max_regret_delta_entries_per_worker = 0U;
		std::uint64_t configured_max_continuation_delta_entries_per_worker = 0U;
		std::uint64_t configured_max_discovery_public_states_per_worker = 0U;
		std::uint64_t configured_max_discovery_rows_per_worker = 0U;
		std::uint64_t cumulative_worker_delta_entries = 0U;
		std::uint64_t worker_active_nanoseconds = 0U;
		std::uint64_t worker_wake_latency_nanoseconds = 0U;
		std::uint64_t worker_cv_wait_nanoseconds = 0U;
		std::uint64_t worker_scheduler_claim_nanoseconds = 0U;
		std::uint64_t worker_completion_nanoseconds = 0U;
		std::uint64_t worker_tail_idle_nanoseconds = 0U;
		std::uint64_t minimum_worker_active_nanoseconds = 0U;
		std::uint64_t maximum_worker_active_nanoseconds = 0U;
		std::uint64_t coordinator_wait_nanoseconds = 0U;
		std::uint64_t barrier_wait_nanoseconds = 0U;
		std::uint64_t delta_sort_nanoseconds = 0U;
		std::uint64_t merge_nanoseconds = 0U;
		std::uint64_t minimum_worker_trajectories = 0U;
		std::uint64_t maximum_worker_trajectories = 0U;
		std::uint32_t requested_worker_count = 0U;
		std::uint32_t effective_worker_count = 0U;
		std::uint64_t trajectories_per_batch = 0U;
		std::vector<std::uint64_t> worker_trajectory_counts;
		std::vector<std::uint64_t> worker_active_nanoseconds_by_worker;
		std::vector<std::uint64_t> worker_sort_nanoseconds_by_worker;
		std::vector<std::uint64_t> worker_wake_latency_nanoseconds_by_worker;
		std::vector<std::uint64_t> worker_cv_wait_nanoseconds_by_worker;
		std::vector<std::uint64_t> worker_scheduler_claim_nanoseconds_by_worker;
		std::vector<std::uint64_t> worker_completion_nanoseconds_by_worker;
		std::vector<std::uint64_t> worker_tail_idle_nanoseconds_by_worker;
		std::vector<std::uint64_t> worker_regret_delta_entries;
		std::vector<std::uint64_t> worker_continuation_delta_entries;
		std::vector<std::uint64_t> worker_discovery_public_state_counts;
		std::vector<std::uint64_t> worker_discovery_row_counts;
		std::vector<std::uint64_t> worker_allocation_counts;
		std::vector<std::uint64_t> worker_allocated_bytes;
		std::vector<std::uint32_t> worker_requested_processor;
		std::vector<std::uint32_t> worker_resolved_processor;
		std::vector<std::uint32_t> worker_requested_cpu_set;
		std::vector<std::uint32_t> worker_resolved_cpu_set;
		std::vector<std::uint32_t> worker_requested_physical_core;
		std::vector<std::uint32_t> worker_resolved_physical_core;
		std::vector<std::uint32_t> worker_requested_smt_sibling;
		std::vector<std::uint32_t> worker_resolved_smt_sibling;
		std::vector<std::uint32_t> worker_requested_cache_domain;
		std::vector<std::uint32_t> worker_resolved_cache_domain;
		std::vector<std::uint8_t> worker_migration_sampled;
		std::vector<std::uint8_t> worker_migration_detected;
		std::vector<std::uint64_t> worker_affinity_masks;
		std::vector<std::uint8_t> worker_affinity_succeeded;
		MultiwayWorkerPlacementPolicy placement_policy = MultiwayWorkerPlacementPolicy::OsDefault;
		bool affinity_requested = false;
		std::vector<MultiwaySearchProfileSnapshot> worker_profiles;
		std::uint64_t memory_preflight_estimate_bytes = 0U;
		MultiwayMemoryLedger memory_ledger{};
		MultiwayProcessMemorySnapshotCollection process_memory_snapshots{};
		MultiwayMemorySubsystemSnapshotCollection memory_subsystems{};
		MultiwayExportTelemetry export_telemetry{};
		bool unattributed_private_bytes_available = false;
		std::uint64_t unattributed_private_bytes = 0U;
		std::uint64_t checkpoint_bytes = 0U;
		std::string storage_backend = "CompactInt32";
		std::uint32_t checkpoint_schema_version = 5U;
		std::uint64_t external_checkpoint_generation = 0U;
		std::uint64_t external_checkpoint_segment_count = 0U;
		std::uint64_t external_storage_identity_hash = 0U;
		std::uint64_t external_storage_disk_preflight_bytes = 0U;
		std::uint64_t external_storage_cache_bytes = 0U;
		std::uint64_t external_storage_wal_limit_bytes = 0U;
		std::uint64_t external_storage_compaction_scratch_bytes = 0U;
		std::uint64_t external_storage_disk_reserve_bytes = 0U;
		std::uint64_t checkpoint_planned_bytes = 0U;
		std::uint64_t checkpoint_scratch_bytes = 0U;
		std::uint64_t checkpoint_payload_hash = 0U;
		core::AtomicPublishMode checkpoint_publication_mode = core::AtomicPublishMode::Unknown;
		MultiwayExportGateStatus checkpoint_process_gate_status = MultiwayExportGateStatus::Disabled;
		MultiwayExportGateStatus checkpoint_system_gate_status = MultiwayExportGateStatus::Disabled;
		std::uint64_t checkpoint_write_nanoseconds = 0U;
		MultiwayCheckpointTelemetry checkpoint_telemetry{};
		std::uint64_t blueprint_bytes = 0U;
		std::uint64_t blueprint_export_nanoseconds = 0U;
		MultiwayTrainingCapacityStage capacity_exhaustion_stage =
			MultiwayTrainingCapacityStage::None;
		bool failed_batch_available = false;
		MultiwayRootBatchResult failed_batch{};
		std::vector<std::uint64_t> admitted_rows_per_batch;
		[[nodiscard]] double trajectories_per_second() const noexcept;
		std::uint64_t batches = 0U;
		std::uint64_t trajectories = 0U;
		std::uint64_t accepted_trajectories = 0U;
		MultiwaySchedulerMode scheduler_mode = MultiwaySchedulerMode::Fixed;
		std::uint32_t dynamic_chunk_size = MULTIWAY_DYNAMIC_SCHEDULER_CHUNK_SIZE;
		std::uint32_t scheduler_policy_version = MULTIWAY_SCHEDULER_POLICY_VERSION;
		std::uint32_t static_block_scheduler_version = MULTIWAY_STATIC_BLOCK_SCHEDULER_VERSION;
		std::uint64_t schedule_fingerprint = 0U;
		std::uint32_t partition_version = MULTIWAY_PARTITION_VERSION;
		std::uint32_t merge_order_version = MULTIWAY_MERGE_ORDER_VERSION;
		std::uint64_t preflop_rows = 0U;
		std::uint64_t flop_rows = 0U;
		std::uint64_t turn_rows = 0U;
		std::uint64_t river_rows = 0U;
		std::uint64_t terminal_visits = 0U;
		std::uint64_t leaf_visits = 0U;
		std::array<std::uint64_t, 4> street_visits{};
		std::uint64_t discarded_trajectories = 0U;
		std::uint64_t deterministic_merge_fingerprint = 0U;
		bool failed = false;

		void validate() const;
	};

	[[nodiscard]] MultiwayTrainingReport make_multiway_training_report(
		const MultiwayBlueprintTrainingStatus& status,
		std::uint64_t deterministic_merge_fingerprint,
		MultiwayEvidenceHeader evidence,
		std::uint64_t process_memory_limit_bytes = 0U,
		std::uint64_t total_wrapper_wall_nanoseconds = 0U) noexcept;
	void save_multiway_training_report_atomic(
		const std::filesystem::path& path,
		const MultiwayTrainingReport& report);

} // namespace texas::solver::multiway
