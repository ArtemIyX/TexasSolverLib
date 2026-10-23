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
		std::uint64_t elapsed_wall_nanoseconds = 0U;
		std::uint64_t current_process_rss_bytes = 0U;
		std::uint64_t peak_process_rss_bytes = 0U;
		std::uint64_t process_memory_limit_bytes = 0U;
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
		std::vector<std::uint64_t> worker_affinity_masks;
		std::vector<std::uint8_t> worker_affinity_succeeded;
		bool affinity_requested = false;
		std::vector<MultiwaySearchProfileSnapshot> worker_profiles;
		std::uint64_t memory_preflight_estimate_bytes = 0U;
		MultiwayMemoryLedger memory_ledger{};
		std::uint64_t checkpoint_bytes = 0U;
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
		std::uint64_t process_memory_limit_bytes = 0U) noexcept;
	void save_multiway_training_report_atomic(
		const std::filesystem::path& path,
		const MultiwayTrainingReport& report);

} // namespace texas::solver::multiway
