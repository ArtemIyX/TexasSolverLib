#include "solver/multiway/workflow/multiway_training_report.hpp"
#include "multiway_training_report_detail.hpp"

#include "core/atomic_publish.hpp"
#include "util/tracy.hpp"

#include <fstream>
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <stdexcept>

namespace texas::solver::multiway
{
	namespace
	{
		void derive_process_summary(MultiwayTrainingReport& report) noexcept
		{
			if (!report.process_memory_snapshots.valid() || report.process_memory_snapshots.count == 0U)
				return;
			const auto last = static_cast<std::size_t>(report.process_memory_snapshots.count - 1U);
			const auto& final = report.process_memory_snapshots.snapshots[last];
			report.current_working_set_available = final.current_working_set_available;
			report.current_working_set_bytes = final.current_working_set_available ? final.current_working_set_bytes : 0U;
			report.private_committed_available = final.private_committed_available;
			report.private_committed_bytes = final.private_committed_available ? final.private_committed_bytes : 0U;
			report.peak_private_committed_available = final.peak_private_committed_available;
			report.peak_private_committed_bytes = final.peak_private_committed_available ? final.peak_private_committed_bytes : 0U;
			report.minor_page_faults_available = final.minor_page_faults_available;
			report.minor_page_faults = final.minor_page_faults_available ? final.minor_page_faults : 0U;
			report.major_page_faults_available = final.major_page_faults_available;
			report.major_page_faults = final.major_page_faults_available ? final.major_page_faults : 0U;
			report.total_page_faults_available = final.total_page_faults_available;
			report.total_page_faults = final.total_page_faults_available ? final.total_page_faults : 0U;
			for (std::size_t index = 0U; index < report.process_memory_snapshots.count; ++index)
			{
				const auto& snapshot = report.process_memory_snapshots.snapshots[index];
				if (snapshot.peak_working_set_available)
				{
					report.peak_process_working_set_available = true;
					report.peak_process_working_set_bytes = std::max(
						report.peak_process_working_set_bytes, snapshot.peak_working_set_bytes);
				}
			}
			report.current_process_rss_bytes = report.current_working_set_bytes;
			report.process_rss_available = report.current_working_set_available;
			if (report.peak_process_working_set_available)
			{
				report.peak_working_set_bytes = report.peak_process_working_set_bytes;
				report.peak_process_rss_bytes = report.peak_process_working_set_bytes;
			}
		}
		void derive_unattributed_private_bytes(MultiwayTrainingReport& report) noexcept
		{
			report.unattributed_private_bytes_available = false;
			report.unattributed_private_bytes = 0U;
			if (!report.process_memory_snapshots.valid() || !report.memory_subsystems.valid() || report.process_memory_snapshots.count == 0U || report.memory_subsystems.count == 0U || report.process_memory_snapshots.count != report.memory_subsystems.count)
				return;
			const auto process_index = static_cast<std::size_t>(report.process_memory_snapshots.count - 1U);
			const auto& process = report.process_memory_snapshots.snapshots[process_index];
			const auto& subsystems = report.memory_subsystems.snapshots[process_index];
			if (process.phase_id != subsystems.phase_id || !process.private_committed_available || process.private_committed_bytes < subsystems.retained_capacity_bytes)
				return;
			report.unattributed_private_bytes = process.private_committed_bytes - subsystems.retained_capacity_bytes;
			report.unattributed_private_bytes_available = true;
		}
	} // namespace

	void MultiwayTrainingReport::validate() const
	{
		export_telemetry.validate();
		if (instrumentation_kind != "none" && instrumentation_kind != "tracy")
			throw std::invalid_argument("invalid multiway training instrumentation kind");
		if (instrumentation_kind == "none" && (!tracy_version.empty() || !trace_capture_id.empty() || !allocation_probe_valid || tracy_memory_events_enabled))
			throw std::invalid_argument("unprofiled training report has Tracy provenance");
		if (instrumentation_kind == "tracy" && (tracy_version.empty() || allocation_probe_valid))
			throw std::invalid_argument("Tracy training report has invalid provenance");
		if (tracy_memory_events_enabled && instrumentation_kind != "tracy")
			throw std::invalid_argument("Tracy memory events require Tracy instrumentation");
		system_memory_guard_config.validate();
		const auto worker_vectors_match = [this] {
			const auto worker_count = worker_trajectory_counts.size();
			const auto matches = [worker_count](std::size_t size) {
				return size == 0U || size == worker_count;
			};
			return matches(worker_active_nanoseconds_by_worker.size()) && matches(worker_sort_nanoseconds_by_worker.size()) && matches(worker_wake_latency_nanoseconds_by_worker.size()) && matches(worker_cv_wait_nanoseconds_by_worker.size()) && matches(worker_scheduler_claim_nanoseconds_by_worker.size()) && matches(worker_completion_nanoseconds_by_worker.size()) && matches(worker_tail_idle_nanoseconds_by_worker.size()) && matches(worker_regret_delta_entries.size()) && matches(worker_continuation_delta_entries.size()) && matches(worker_discovery_public_state_counts.size()) && matches(worker_discovery_row_counts.size()) && matches(admission_diagnostics.worker_raw_public_state_candidates_by_worker.size()) && matches(admission_diagnostics.worker_raw_row_candidates_by_worker.size()) && matches(admission_diagnostics.worker_local_unique_public_state_candidates_by_worker.size()) && matches(admission_diagnostics.worker_local_unique_row_candidates_by_worker.size()) && matches(worker_allocation_counts.size()) && matches(worker_allocated_bytes.size()) && matches(worker_requested_processor.size()) && matches(worker_resolved_processor.size()) && matches(worker_requested_cpu_set.size()) && matches(worker_resolved_cpu_set.size()) && matches(worker_requested_physical_core.size()) && matches(worker_resolved_physical_core.size()) && matches(worker_requested_smt_sibling.size()) && matches(worker_resolved_smt_sibling.size()) && matches(worker_requested_cache_domain.size()) && matches(worker_resolved_cache_domain.size()) && matches(worker_migration_sampled.size()) && matches(worker_migration_detected.size()) && matches(worker_affinity_masks.size()) && matches(worker_affinity_succeeded.size()) && matches(worker_profiles.size());
		};
		if (accepted_trajectories > trajectories || discarded_trajectories > trajectories || (batches != 0U && deterministic_merge_fingerprint == 0U && !failed))
		{
			throw std::invalid_argument("invalid multiway training report");
		}
		if (!std::isfinite(external_storage_compaction_read_amplification) || !std::isfinite(external_storage_compaction_write_amplification) || external_storage_compaction_read_amplification < 0.0 || external_storage_compaction_write_amplification < 0.0 || (external_storage_counter_scope != "not_available" && external_storage_counter_scope != "current_process"))
			throw std::invalid_argument("invalid external storage compaction amplification");
		if (!multiway_scheduler_mode_valid(scheduler_mode) || dynamic_chunk_size == 0U || scheduler_policy_version == 0U || static_block_scheduler_version == 0U || partition_version == 0U || merge_order_version == 0U)
		{
			throw std::invalid_argument("invalid multiway scheduler evidence");
		}
		if (failed_batch_available)
		{
			const auto& batch = failed_batch;
			const auto worker_count = batch.worker_trajectory_counts.size();
			const auto matches = [worker_count](std::size_t size) {
				return size == 0U || size == worker_count;
			};
			if (batch.trajectories_attempted != batch.trajectories_accepted + batch.trajectories_discarded || !matches(batch.worker_active_nanoseconds_by_worker.size()) || !matches(batch.worker_sort_nanoseconds_by_worker.size()) || !matches(batch.worker_wake_latency_nanoseconds_by_worker.size()) || !matches(batch.worker_cv_wait_nanoseconds_by_worker.size()) || !matches(batch.worker_scheduler_claim_nanoseconds_by_worker.size()) || !matches(batch.worker_completion_nanoseconds_by_worker.size()) || !matches(batch.worker_tail_idle_nanoseconds_by_worker.size()) || !matches(batch.worker_regret_delta_entries.size()) || !matches(batch.worker_continuation_delta_entries.size()) || !matches(batch.worker_discovery_public_state_counts.size()) || !matches(batch.worker_discovery_row_counts.size()) || !matches(batch.worker_allocation_counts.size()) || !matches(batch.worker_allocated_bytes.size()) || !matches(batch.worker_requested_processor.size()) || !matches(batch.worker_resolved_processor.size()) || !matches(batch.worker_requested_cpu_set.size()) || !matches(batch.worker_resolved_cpu_set.size()) || !matches(batch.worker_requested_physical_core.size()) || !matches(batch.worker_resolved_physical_core.size()) || !matches(batch.worker_requested_smt_sibling.size()) || !matches(batch.worker_resolved_smt_sibling.size()) || !matches(batch.worker_requested_cache_domain.size()) || !matches(batch.worker_resolved_cache_domain.size()) || !matches(batch.worker_migration_sampled.size()) || !matches(batch.worker_migration_detected.size()) || !matches(batch.worker_affinity_masks.size()) || !matches(batch.worker_affinity_succeeded.size()))
			{
				throw std::invalid_argument("invalid failed multiway batch telemetry");
			}
		}
		evidence.validate(MULTIWAY_TRAINING_REPORT_SCHEMA_VERSION);
		if (!process_memory_snapshots.valid() || (!failed && process_memory_snapshots.enabled && !process_memory_snapshots.completed))
		{
			throw std::invalid_argument("invalid multiway process memory snapshots");
		}
		if (!memory_subsystems.valid() || (!failed && memory_subsystems.enabled && !memory_subsystems.completed))
		{
			throw std::invalid_argument("invalid multiway subsystem memory snapshots");
		}
		if (unattributed_private_bytes_available)
		{
			if (process_memory_snapshots.count == 0U || memory_subsystems.count == 0U || process_memory_snapshots.count != memory_subsystems.count)
				throw std::invalid_argument("unattributed private bytes lack matching snapshots");
			const auto index = static_cast<std::size_t>(process_memory_snapshots.count - 1U);
			const auto& process = process_memory_snapshots.snapshots[index];
			const auto& subsystems = memory_subsystems.snapshots[index];
			if (!process.private_committed_available || process.phase_id != subsystems.phase_id || process.private_committed_bytes < subsystems.retained_capacity_bytes || unattributed_private_bytes != process.private_committed_bytes - subsystems.retained_capacity_bytes)
				throw std::invalid_argument("invalid unattributed private bytes derivation");
		}
		if (process_rss_available && current_process_rss_bytes == 0U)
		{
			throw std::invalid_argument("available process RSS must be non-zero");
		}
		const auto check_available = [](bool available, std::uint64_t value) {
			return available || value == 0U;
		};
		if (!check_available(current_working_set_available, current_working_set_bytes) || !check_available(peak_process_working_set_available, peak_process_working_set_bytes) || !check_available(private_committed_available, private_committed_bytes) || !check_available(peak_private_committed_available, peak_private_committed_bytes) || !check_available(minor_page_faults_available, minor_page_faults) || !check_available(major_page_faults_available, major_page_faults) || !check_available(total_page_faults_available, total_page_faults) || (peak_process_working_set_available && current_working_set_available && peak_process_working_set_bytes < current_working_set_bytes) || (total_page_faults_available && minor_page_faults_available && major_page_faults_available && total_page_faults < multiway_memory_saturating_add(minor_page_faults, major_page_faults)) || (total_wrapper_wall_available && total_wrapper_wall_nanoseconds < multiway_memory_saturating_add(multiway_memory_saturating_add(training_wall_nanoseconds, checkpoint_wall_nanoseconds), export_wall_nanoseconds)))
		{
			throw std::invalid_argument("invalid authoritative multiway phase telemetry");
		}
		if (peak_process_rss_bytes < current_process_rss_bytes)
			throw std::invalid_argument("multiway training peak RSS is below current RSS");
		if (requested_worker_count > 16U || effective_worker_count > requested_worker_count || ((requested_worker_count != 0U || effective_worker_count != 0U) && (requested_worker_count == 0U || effective_worker_count == 0U || trajectories_per_batch == 0U)))
			throw std::invalid_argument("invalid multiway training worker counts");
		if (minimum_worker_trajectories > maximum_worker_trajectories)
			throw std::invalid_argument("invalid multiway worker trajectory range");
		if (minimum_worker_active_nanoseconds > maximum_worker_active_nanoseconds)
			throw std::invalid_argument("invalid multiway worker active-time range");
		if (barrier_wait_nanoseconds > coordinator_wait_nanoseconds)
			throw std::invalid_argument("multiway barrier wait exceeds coordinator wait");
		if (!worker_vectors_match())
			throw std::invalid_argument("multiway per-worker telemetry vector sizes differ");
		const bool external_checkpoint_has_no_worker_counts =
			external_checkpoint_generation != 0U && worker_trajectory_counts.empty();
		const bool compact_checkpoint_only_export_has_no_worker_counts =
			checkpoint_bytes != 0U && training_wall_nanoseconds == 0U && worker_trajectory_counts.empty();
		if (batches != 0U && effective_worker_count != 0U && worker_trajectory_counts.size() != effective_worker_count
			&& !external_checkpoint_has_no_worker_counts && !compact_checkpoint_only_export_has_no_worker_counts)
			throw std::invalid_argument("multiway worker trajectory vector size differs from worker count");
		if (!std::isfinite(trajectories_per_second()))
			throw std::invalid_argument("multiway training throughput is not finite");
		if (static_cast<unsigned>(placement_policy) > static_cast<unsigned>(MultiwayWorkerPlacementPolicy::DiagnosticHardAffinity))
			throw std::invalid_argument("invalid multiway worker placement policy");
		if (static_cast<unsigned>(capacity_exhaustion_stage) > static_cast<unsigned>(MultiwayTrainingCapacityStage::Discovery))
			throw std::invalid_argument("invalid multiway capacity exhaustion stage");
		if (static_cast<unsigned>(working_set_gate.phase) > static_cast<unsigned>(MultiwayWorkingSetGatePhase::Runtime))
			throw std::invalid_argument("invalid multiway working-set gate phase");
		if (static_cast<unsigned>(working_set_gate.status) > static_cast<unsigned>(MultiwayWorkingSetGateStatus::Unavailable))
			throw std::invalid_argument("invalid multiway working-set gate status");
		if (static_cast<unsigned>(working_set_gate.admission_stage) > static_cast<unsigned>(MultiwayMemoryAdmissionStage::OptionalContinuationCache))
			throw std::invalid_argument("invalid multiway working-set admission stage");
		if (static_cast<unsigned>(system_memory_guard.status) > static_cast<unsigned>(MultiwaySystemMemoryGuardStatus::Unavailable))
			throw std::invalid_argument("invalid multiway system memory guard status");
		for (std::size_t index = 1U; index < admitted_rows_per_batch.size(); ++index)
		{
			if (admitted_rows_per_batch[index] < admitted_rows_per_batch[index - 1U])
			{
				throw std::invalid_argument("training row-growth series is not monotonic");
			}
		}
	}

	double MultiwayTrainingReport::trajectories_per_second() const noexcept
	{
		const auto training_wall = training_wall_nanoseconds != 0U
			? training_wall_nanoseconds
			: elapsed_wall_nanoseconds;
		if (training_wall == 0U)
			return 0.0;
		return static_cast<double>(trajectories) * 1'000'000'000.0 / static_cast<double>(training_wall);
	}

	MultiwayTrainingReport make_multiway_training_report(
		const MultiwayBlueprintTrainingStatus& status, std::uint64_t fingerprint,
		MultiwayEvidenceHeader evidence, std::uint64_t process_memory_limit_bytes,
		std::uint64_t total_wrapper_wall_nanoseconds) noexcept
	{
		MultiwayTrainingReport report;
		report.batches = status.batches;
		report.trajectories = status.trajectories;
		report.discarded_trajectories = status.discarded_trajectories;
		report.accepted_trajectories = status.trajectories >= status.discarded_trajectories
			? status.trajectories - status.discarded_trajectories
			: 0U;
		report.scheduler_mode = status.scheduler_mode;
		report.dynamic_chunk_size = status.dynamic_chunk_size;
		report.scheduler_policy_version = status.scheduler_policy_version;
		report.static_block_scheduler_version = status.static_block_scheduler_version;
		report.schedule_fingerprint = status.schedule_fingerprint;
		report.partition_version = status.partition_version;
		report.merge_order_version = status.merge_order_version;
		report.preflop_rows = status.preflop_rows;
		report.flop_rows = status.flop_rows;
		report.turn_rows = status.turn_rows;
		report.river_rows = status.river_rows;
		report.terminal_visits = status.terminal_visits;
		report.leaf_visits = status.leaf_visits;
		report.street_visits = status.street_visits;
		report.deterministic_merge_fingerprint = fingerprint;
		report.evidence = evidence;
		report.tracy_memory_events_enabled = texas::util::tracy_memory_events_enabled;
#if defined(TEXASSOLVER_TRACY_ENABLED) && TEXASSOLVER_TRACY_ENABLED
		report.instrumentation_kind = "tracy";
		report.tracy_version = texas::util::TEXASSOLVER_TRACY_VERSION;
		report.allocation_probe_valid = false;
		const auto* capture_id = std::getenv("TEXASSOLVER_TRACE_CAPTURE_ID");
		report.trace_capture_id = capture_id == nullptr || *capture_id == '\0'
			? "not-captured"
			: capture_id;
#endif
		report.peak_public_states = status.peak_public_states;
		report.peak_sparse_rows = status.peak_sparse_rows;
		report.peak_sparse_values = status.peak_sparse_values;
		report.peak_worker_delta_entries = status.peak_worker_delta_entries;
		report.total_peak_regret_delta_entries = status.total_peak_regret_delta_entries;
		report.total_peak_continuation_delta_entries = status.total_peak_continuation_delta_entries;
		report.total_peak_delta_entries = status.total_peak_delta_entries;
		report.generated_update_count = status.generated_update_count;
		report.generated_discovery_public_state_count = status.generated_discovery_public_state_count;
		report.generated_discovery_row_count = status.generated_discovery_row_count;
		report.coordinator_admission_nanoseconds = status.coordinator_admission_nanoseconds;
		report.admission_diagnostics = status.admission_diagnostics;
		report.trainer_only_nanoseconds = status.trainer_only_nanoseconds;
		report.process_cpu_nanoseconds = status.process_cpu_nanoseconds;
		report.peak_working_set_bytes = status.peak_working_set_bytes;
		report.peak_compact_storage_bytes = status.peak_compact_storage_bytes;
		report.configured_max_public_states = status.configured_max_public_states;
		report.configured_max_sparse_rows = status.configured_max_sparse_rows;
		report.configured_max_sparse_values = status.configured_max_sparse_values;
		report.configured_worker_delta_capacity = status.configured_worker_delta_capacity;
		report.configured_discovery_capacity = status.configured_discovery_capacity;
		report.configured_max_regret_delta_entries_per_worker = status.configured_max_regret_delta_entries_per_worker;
		report.configured_max_continuation_delta_entries_per_worker = status.configured_max_continuation_delta_entries_per_worker;
		report.configured_max_discovery_public_states_per_worker = status.configured_max_discovery_public_states_per_worker;
		report.configured_max_discovery_rows_per_worker = status.configured_max_discovery_rows_per_worker;
		report.cumulative_worker_delta_entries = status.cumulative_worker_delta_entries;
		report.worker_active_nanoseconds = status.worker_active_nanoseconds;
		report.worker_wake_latency_nanoseconds = status.worker_wake_latency_nanoseconds;
		report.worker_cv_wait_nanoseconds = status.worker_cv_wait_nanoseconds;
		report.worker_scheduler_claim_nanoseconds = status.worker_scheduler_claim_nanoseconds;
		report.worker_completion_nanoseconds = status.worker_completion_nanoseconds;
		report.worker_tail_idle_nanoseconds = status.worker_tail_idle_nanoseconds;
		report.minimum_worker_active_nanoseconds = status.minimum_worker_active_nanoseconds;
		report.maximum_worker_active_nanoseconds = status.maximum_worker_active_nanoseconds;
		report.coordinator_wait_nanoseconds = status.coordinator_wait_nanoseconds;
		report.barrier_wait_nanoseconds = status.barrier_wait_nanoseconds;
		report.delta_sort_nanoseconds = status.delta_sort_nanoseconds;
		report.merge_nanoseconds = status.merge_nanoseconds;
		report.minimum_worker_trajectories = status.minimum_worker_trajectories;
		report.maximum_worker_trajectories = status.maximum_worker_trajectories;
		report.requested_worker_count = status.requested_worker_count;
		report.effective_worker_count = status.effective_worker_count;
		report.trajectories_per_batch = status.trajectories_per_batch;
		report.worker_trajectory_counts = status.worker_trajectory_counts;
		report.worker_active_nanoseconds_by_worker = status.worker_active_nanoseconds_by_worker;
		report.worker_sort_nanoseconds_by_worker = status.worker_sort_nanoseconds_by_worker;
		report.worker_wake_latency_nanoseconds_by_worker = status.worker_wake_latency_nanoseconds_by_worker;
		report.worker_cv_wait_nanoseconds_by_worker = status.worker_cv_wait_nanoseconds_by_worker;
		report.worker_scheduler_claim_nanoseconds_by_worker = status.worker_scheduler_claim_nanoseconds_by_worker;
		report.worker_completion_nanoseconds_by_worker = status.worker_completion_nanoseconds_by_worker;
		report.worker_tail_idle_nanoseconds_by_worker = status.worker_tail_idle_nanoseconds_by_worker;
		report.worker_regret_delta_entries = status.worker_regret_delta_entries;
		report.worker_continuation_delta_entries = status.worker_continuation_delta_entries;
		report.worker_discovery_public_state_counts = status.worker_discovery_public_state_counts;
		report.worker_discovery_row_counts = status.worker_discovery_row_counts;
		report.worker_allocation_counts = status.worker_allocation_counts;
		report.worker_allocated_bytes = status.worker_allocated_bytes;
		report.worker_requested_processor = status.worker_requested_processor;
		report.worker_resolved_processor = status.worker_resolved_processor;
		report.worker_requested_cpu_set = status.worker_requested_cpu_set;
		report.worker_resolved_cpu_set = status.worker_resolved_cpu_set;
		report.worker_requested_physical_core = status.worker_requested_physical_core;
		report.worker_resolved_physical_core = status.worker_resolved_physical_core;
		report.worker_requested_smt_sibling = status.worker_requested_smt_sibling;
		report.worker_resolved_smt_sibling = status.worker_resolved_smt_sibling;
		report.worker_requested_cache_domain = status.worker_requested_cache_domain;
		report.worker_resolved_cache_domain = status.worker_resolved_cache_domain;
		report.worker_migration_sampled = status.worker_migration_sampled;
		report.worker_migration_detected = status.worker_migration_detected;
		report.worker_affinity_masks = status.worker_affinity_masks;
		report.worker_affinity_succeeded = status.worker_affinity_succeeded;
		report.placement_policy = status.placement_policy;
		report.affinity_requested = status.affinity_requested;
		report.worker_profiles = status.worker_profiles;
		report.memory_preflight_estimate_bytes = status.memory_preflight_estimate_bytes;
		report.memory_ledger = status.memory_ledger;
		report.process_memory_snapshots = status.process_memory_snapshots;
		report.training_wall_nanoseconds = status.elapsed_wall_nanoseconds;
		report.checkpoint_wall_nanoseconds = status.checkpoint_wall_nanoseconds;
		report.export_wall_nanoseconds = status.export_wall_nanoseconds;
		report.total_wrapper_wall_nanoseconds = total_wrapper_wall_nanoseconds;
		report.total_wrapper_wall_available = total_wrapper_wall_nanoseconds != 0U;
		report.memory_subsystems = status.memory_subsystem_snapshots;
		report.export_telemetry = status.export_telemetry;
		if (report.memory_subsystems.count != 0U)
			report.memory_ledger.actual_retained_capacity_bytes = report.memory_subsystems.snapshots[report.memory_subsystems.count - 1U].retained_capacity_bytes;
		report.capacity_exhaustion_stage = status.capacity_exhaustion_stage;
		report.failed = status.failed;
		report.failed_batch_available = status.failed_batch_available;
		report.failed_batch = status.failed_batch;
		report.admitted_rows_per_batch = status.admitted_rows_per_batch;
		report.current_process_rss_bytes = status.current_process_rss_bytes;
		report.peak_process_rss_bytes = status.peak_process_rss_bytes;
		report.sampled_process_working_set_peak_bytes = status.peak_process_rss_bytes;
		report.process_memory_limit_bytes = process_memory_limit_bytes;
		report.working_set_gate_config = status.working_set_gate_config;
		report.working_set_gate = status.working_set_gate;
		report.system_memory_guard_config = status.system_memory_guard_config;
		report.system_memory_guard = status.system_memory_guard;
		report.process_rss_available = status.process_rss_available;
		report.started_at_preflop = status.preflop_rows > 0U;
		report.elapsed_wall_nanoseconds = status.elapsed_wall_nanoseconds;
		if (report.checkpoint_wall_nanoseconds != 0U)
			report.checkpoint_write_nanoseconds = report.checkpoint_wall_nanoseconds;
		if (report.export_wall_nanoseconds != 0U)
			report.blueprint_export_nanoseconds = report.export_wall_nanoseconds;
		derive_process_summary(report);
		derive_unattributed_private_bytes(report);
		return report;
	}

	void save_multiway_training_report_atomic(const std::filesystem::path& path,
		const MultiwayTrainingReport& report)
	{
		report.validate();
		const auto temporary = path.string() + ".tmp";
		std::ofstream output(temporary, std::ios::trunc);
		if (!output)
			throw std::runtime_error("cannot open training report");
		training_report_detail::write_json(output, report);
		output.close();
		core::publish_atomic_replace(temporary, path, "cannot publish training report");
	}
} // namespace texas::solver::multiway
