#include "solver/multiway/workflow/multiway_training_report.hpp"

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
		const auto write_u64_array = [&output](const char* name, const std::vector<std::uint64_t>& values, bool leading_comma = true) {
			output << (leading_comma ? ",\n  \"" : "\n  \"") << name << "\": [";
			for (std::size_t index = 0U; index < values.size(); ++index)
			{
				if (index != 0U)
					output << ',';
				output << values[index];
			}
			output << ']';
		};
		const auto write_memory_component = [&output](const char* name, const MultiwayMemoryComponent& component, bool leading_comma = true) {
			output << (leading_comma ? ", \"" : "\"") << name << "\": {\"live_element_bytes\": "
				   << component.live_element_bytes << ", \"retained_capacity_bytes\": "
				   << component.retained_capacity_bytes << ", \"allocation_count\": "
				   << component.allocation_count << ", \"allocation_count_known\": "
				   << (component.allocation_count_known ? "true" : "false")
				   << ", \"arithmetic_saturated\": " << (component.arithmetic_saturated ? "true" : "false")
				   << ", \"grouping_only\": " << (component.grouping_only ? "true" : "false") << '}';
		};
		output << "{\n  \"evidence\": " << serialize_multiway_evidence_header(report.evidence)
			   << ",\n  \"instrumentation_kind\": \"" << report.instrumentation_kind
			   << "\",\n  \"tracy_version\": \"" << report.tracy_version
			   << "\",\n  \"trace_capture_id\": \"" << report.trace_capture_id
			   << "\",\n  \"allocation_probe_valid\": "
			   << (report.allocation_probe_valid ? "true" : "false")
			   << ",\n  \"tracy_memory_events_enabled\": "
			   << (report.tracy_memory_events_enabled ? "true" : "false")
			   << ",\n  \"batches\": " << report.batches
			   << ",\n  \"trajectories\": " << report.trajectories
			   << ",\n  \"accepted_trajectories\": " << report.accepted_trajectories
			   << ",\n  \"scheduler_mode\": \"" << multiway_scheduler_mode_name(report.scheduler_mode) << '\"'
			   << ",\n  \"dynamic_chunk_size\": " << report.dynamic_chunk_size
			   << ",\n  \"scheduler_policy_version\": " << report.scheduler_policy_version
			   << ",\n  \"static_block_scheduler_version\": " << report.static_block_scheduler_version
			   << ",\n  \"schedule_fingerprint\": " << report.schedule_fingerprint
			   << ",\n  \"partition_version\": " << report.partition_version
			   << ",\n  \"merge_order_version\": " << report.merge_order_version
			   << ",\n  \"preflop_rows\": " << report.preflop_rows
			   << ",\n  \"flop_rows\": " << report.flop_rows
			   << ",\n  \"turn_rows\": " << report.turn_rows
			   << ",\n  \"river_rows\": " << report.river_rows
			   << ",\n  \"terminal_visits\": " << report.terminal_visits
			   << ",\n  \"leaf_visits\": " << report.leaf_visits
			   << ",\n  \"street_visits\": [" << report.street_visits[0U] << ','
			   << report.street_visits[1U] << ',' << report.street_visits[2U] << ','
			   << report.street_visits[3U] << ']'
			   << ",\n  \"discarded_trajectories\": " << report.discarded_trajectories
			   << ",\n  \"deterministic_merge_fingerprint\": " << report.deterministic_merge_fingerprint
			   << ",\n  \"failed\": " << (report.failed ? "true" : "false")
			   << ",\n  \"failed_batch_available\": " << (report.failed_batch_available ? "true" : "false")
			   << ",\n  \"elapsed_wall_nanoseconds\": " << report.elapsed_wall_nanoseconds
			   << ",\n  \"training_wall_nanoseconds\": " << report.training_wall_nanoseconds
			   << ",\n  \"checkpoint_wall_nanoseconds\": " << report.checkpoint_wall_nanoseconds
			   << ",\n  \"export_wall_nanoseconds\": " << report.export_wall_nanoseconds
			   << ",\n  \"total_wrapper_wall_nanoseconds\": " << report.total_wrapper_wall_nanoseconds
			   << ",\n  \"total_wrapper_wall_available\": " << (report.total_wrapper_wall_available ? "true" : "false")
			   << ",\n  \"trajectories_per_second\": " << report.trajectories_per_second()
			   << ",\n  \"current_process_rss_bytes\": " << report.current_process_rss_bytes
			   << ",\n  \"peak_process_rss_bytes\": " << report.peak_process_rss_bytes
			   << ",\n  \"sampled_process_working_set_peak_bytes\": " << report.sampled_process_working_set_peak_bytes
			   << ",\n  \"current_working_set_bytes\": " << report.current_working_set_bytes
			   << ",\n  \"peak_process_working_set_bytes\": " << report.peak_process_working_set_bytes
			   << ",\n  \"private_committed_bytes\": " << report.private_committed_bytes
			   << ",\n  \"peak_private_committed_bytes\": " << report.peak_private_committed_bytes
			   << ",\n  \"minor_page_faults\": " << report.minor_page_faults
			   << ",\n  \"major_page_faults\": " << report.major_page_faults
			   << ",\n  \"total_page_faults\": " << report.total_page_faults
			   << ",\n  \"authoritative_memory_availability\": {\"current_working_set\": "
			   << (report.current_working_set_available ? "true" : "false")
			   << ", \"peak_process_working_set\": " << (report.peak_process_working_set_available ? "true" : "false")
			   << ", \"private_committed\": " << (report.private_committed_available ? "true" : "false")
			   << ", \"peak_private_committed\": " << (report.peak_private_committed_available ? "true" : "false")
			   << ", \"minor_page_faults\": " << (report.minor_page_faults_available ? "true" : "false")
			   << ", \"major_page_faults\": " << (report.major_page_faults_available ? "true" : "false")
			   << ", \"total_page_faults\": " << (report.total_page_faults_available ? "true" : "false") << "}"
			   << ",\n  \"process_memory_limit_bytes\": " << report.process_memory_limit_bytes
			   << ",\n  \"process_working_set_limit_bytes\": " << report.working_set_gate_config.hard_limit_bytes
			   << ",\n  \"process_working_set_resident_allowance_bytes\": " << report.working_set_gate_config.resident_allowance_bytes
			   << ",\n  \"process_private_commit_limit_bytes\": " << report.working_set_gate_config.private_commit_limit_bytes
			   << ",\n  \"working_set_gate_status\": " << static_cast<unsigned>(report.working_set_gate.status)
			   << ",\n  \"working_set_gate_abort_phase\": " << static_cast<unsigned>(report.working_set_gate.phase)
			   << ",\n  \"working_set_gate_admission_stage\": " << static_cast<unsigned>(report.working_set_gate.admission_stage)
			   << ",\n  \"working_set_gate_estimated_bytes\": " << report.working_set_gate.estimated_working_set_bytes
			   << ",\n  \"working_set_gate_compared_bytes\": " << report.working_set_gate.compared_working_set_bytes
			   << ",\n  \"working_set_gate_observed_bytes\": " << report.working_set_gate.observed_working_set_bytes
			   << ",\n  \"working_set_gate_observed_private_commit_bytes\": " << report.working_set_gate.observed_private_commit_bytes
			   << ",\n  \"working_set_gate_rejected_category\": \"" << report.working_set_gate.rejected_category << "\""
			   << ",\n  \"system_memory_guard_enabled\": " << (report.system_memory_guard_config.enabled ? "true" : "false")
			   << ",\n  \"system_memory_guard_minimum_available_bytes\": " << report.system_memory_guard_config.minimum_available_physical_bytes
			   << ",\n  \"system_memory_guard_status\": " << static_cast<unsigned>(report.system_memory_guard.status)
			   << ",\n  \"system_memory_guard_rejected_category\": \"" << report.system_memory_guard.rejected_category << "\""
			   << ",\n  \"system_memory_total_physical_bytes\": " << report.system_memory_guard.total_physical_bytes
			   << ",\n  \"system_memory_available_physical_bytes\": " << report.system_memory_guard.available_physical_bytes
			   << ",\n  \"system_memory_used_physical_bytes\": " << report.system_memory_guard.used_physical_bytes
			   << ",\n  \"system_memory_maximum_used_physical_bytes\": " << report.system_memory_guard.maximum_used_physical_bytes
			   << ",\n  \"export_telemetry_schema_version\": " << report.export_telemetry.schema_version
			   << ",\n  \"export_attempted\": " << (report.export_telemetry.attempted ? "true" : "false")
			   << ",\n  \"export_succeeded\": " << (report.export_telemetry.succeeded ? "true" : "false")
			   << ",\n  \"export_shape_count\": " << report.export_telemetry.plan.shape_count
			   << ",\n  \"export_row_count\": " << report.export_telemetry.plan.row_count
			   << ",\n  \"export_action_count\": " << report.export_telemetry.plan.action_count
			   << ",\n  \"export_serialized_bytes\": " << report.export_telemetry.plan.serialized_bytes
			   << ",\n  \"export_order_index_bytes\": " << report.export_telemetry.plan.order_index_bytes
			   << ",\n  \"export_compact_direct_order\": " << (report.export_telemetry.plan.compact_direct_order ? "true" : "false")
			   << ",\n  \"export_scratch_order_index_bytes\": " << report.export_telemetry.scratch.order_index_bytes
			   << ",\n  \"export_scratch_strategy_bytes\": " << report.export_telemetry.scratch.strategy_scratch_bytes
			   << ",\n  \"export_scratch_action_bytes\": " << report.export_telemetry.scratch.action_scratch_bytes
			   << ",\n  \"export_scratch_encoder_buffer_bytes\": " << report.export_telemetry.scratch.encoder_buffer_bytes
			   << ",\n  \"export_scratch_headroom_bytes\": " << report.export_telemetry.scratch.exporter_headroom_bytes
			   << ",\n  \"export_scratch_total_bytes\": " << report.export_telemetry.scratch.total_bytes
			   << ",\n  \"export_scratch_arithmetic_saturated\": " << (report.export_telemetry.scratch.arithmetic_saturated ? "true" : "false")
			   << ",\n  \"export_process_gate_status\": " << static_cast<unsigned>(report.export_telemetry.process_gate.status)
			   << ",\n  \"export_process_gate_reason\": \"" << multiway_export_process_rejection_reason_name(report.export_telemetry.process_gate.rejection_reason) << "\""
			   << ",\n  \"export_process_gate_limit_bytes\": " << report.export_telemetry.process_gate.hard_limit_bytes
			   << ",\n  \"export_process_gate_estimated_peak_bytes\": " << report.export_telemetry.process_gate.compared_process_working_set_bytes
			   << ",\n  \"export_disk_gate_status\": " << static_cast<unsigned>(report.export_telemetry.disk_gate.status)
			   << ",\n  \"export_disk_gate_reason\": " << static_cast<unsigned>(report.export_telemetry.disk_gate.rejection_reason)
			   << ",\n  \"export_disk_gate_required_free_bytes\": " << report.export_telemetry.disk_gate.required_free_bytes
			   << ",\n  \"export_disk_gate_available_free_bytes\": " << report.export_telemetry.disk_gate.available_free_bytes
			   << ",\n  \"export_disk_gate_output_bytes\": " << report.export_telemetry.disk_gate.serialized_output_bytes
			   << ",\n  \"export_disk_gate_temporary_overlap_bytes\": " << report.export_telemetry.disk_gate.temporary_overlap_bytes
			   << ",\n  \"export_disk_gate_reserve_bytes\": " << report.export_telemetry.disk_gate.reserve_bytes
			   << ",\n  \"export_sharded_output\": " << (report.export_telemetry.sharded_output ? "true" : "false")
			   << ",\n  \"export_shard_count\": " << report.export_telemetry.shard_count
			   << ",\n  \"export_manifest_bytes\": " << report.export_telemetry.manifest_bytes
			   << ",\n  \"export_sharding_metadata_bytes\": " << report.export_telemetry.plan.sharding_metadata_bytes
			   << ",\n  \"export_process_current_working_set_bytes\": " << report.export_telemetry.process_estimate.current_working_set_bytes
			   << ",\n  \"export_process_order_index_bytes\": " << report.export_telemetry.process_estimate.order_index_bytes
			   << ",\n  \"export_process_sharding_metadata_bytes\": " << report.export_telemetry.process_estimate.sharding_metadata_bytes
			   << ",\n  \"export_process_strategy_scratch_bytes\": " << report.export_telemetry.process_estimate.strategy_scratch_bytes
			   << ",\n  \"export_process_action_scratch_bytes\": " << report.export_telemetry.process_estimate.action_scratch_bytes
			   << ",\n  \"export_process_encoder_buffer_bytes\": " << report.export_telemetry.process_estimate.encoder_buffer_bytes
			   << ",\n  \"export_process_headroom_bytes\": " << report.export_telemetry.process_estimate.exporter_headroom_bytes
			   << ",\n  \"export_process_estimate_arithmetic_saturated\": " << (report.export_telemetry.process_estimate.arithmetic_saturated ? "true" : "false")
			   << ",\n  \"export_process_gate_current_working_set_available\": " << (report.export_telemetry.process_estimate.current_working_set_available ? "true" : "false")
			   << ",\n  \"export_process_gate_mapped_residency_allowance_bytes\": " << report.export_telemetry.process_estimate.mapped_residency_allowance_bytes
			   << ",\n  \"export_system_gate_status\": " << static_cast<unsigned>(report.export_telemetry.system_gate.status)
			   << ",\n  \"export_system_gate_reason\": \"" << multiway_export_system_rejection_reason_name(report.export_telemetry.system_gate.rejection_reason) << "\""
			   << ",\n  \"export_system_before_available\": " << (report.export_telemetry.system_before.available_physical_available ? "true" : "false")
			   << ",\n  \"export_system_after_available\": " << (report.export_telemetry.system_after.available_physical_available ? "true" : "false")
			   << ",\n  \"export_system_available_physical_bytes\": " << report.export_telemetry.system_gate.available_physical_bytes
			   << ",\n  \"export_system_output_bytes\": " << report.export_telemetry.system_gate.serialized_output_bytes
			   << ",\n  \"export_system_scratch_bytes\": " << report.export_telemetry.system_gate.exporter_scratch_bytes
			   << ",\n  \"export_system_reserve_bytes\": " << report.export_telemetry.system_gate.system_reserve_bytes
			   << ",\n  \"export_system_required_available_bytes\": " << report.export_telemetry.system_gate.required_available_bytes
			   << ",\n  \"export_observed_before_working_set_bytes\": " << report.export_telemetry.observed_delta.before_working_set_bytes
			   << ",\n  \"export_observed_after_working_set_bytes\": " << report.export_telemetry.observed_delta.after_working_set_bytes
			   << ",\n  \"export_observed_working_set_delta_bytes\": " << report.export_telemetry.observed_delta.working_set_delta_bytes
			   << ",\n  \"export_observed_before_private_committed_bytes\": " << report.export_telemetry.observed_delta.before_private_committed_bytes
			   << ",\n  \"export_observed_after_private_committed_bytes\": " << report.export_telemetry.observed_delta.after_private_committed_bytes
			   << ",\n  \"export_observed_private_committed_delta_bytes\": " << report.export_telemetry.observed_delta.private_committed_delta_bytes
			   << ",\n  \"export_result_bytes\": " << report.export_telemetry.result_bytes
			   << ",\n  \"export_result_payload_hash\": " << report.export_telemetry.payload_hash
			   << ",\n  \"export_policy_content_hash\": " << report.export_telemetry.policy_content_hash
			   << ",\n  \"export_duration_nanoseconds\": " << report.export_telemetry.duration_nanoseconds
			   << ",\n  \"external_export_profile_enabled\": " << (report.export_telemetry.external_profile.enabled ? "true" : "false")
			   << ",\n  \"external_export_profile_shapes\": " << report.export_telemetry.external_profile.shapes
			   << ",\n  \"external_export_profile_pages\": " << report.export_telemetry.external_profile.pages
			   << ",\n  \"external_export_profile_rows\": " << report.export_telemetry.external_profile.rows
			   << ",\n  \"external_export_profile_actions\": " << report.export_telemetry.external_profile.actions
			   << ",\n  \"external_export_profile_encoded_bytes\": " << report.export_telemetry.external_profile.encoded_bytes
			   << ",\n  \"external_export_profile_stream_write_bytes\": " << report.export_telemetry.external_profile.stream_write_bytes
			   << ",\n  \"external_export_ordered_visit_inclusive_nanoseconds\": " << report.export_telemetry.external_profile.ordered_visit_nanoseconds
			   << ",\n  \"external_export_visitor_inclusive_nanoseconds\": " << report.export_telemetry.external_profile.visitor_nanoseconds
			   << ",\n  \"external_export_menu_inclusive_nanoseconds\": " << report.export_telemetry.external_profile.menu_nanoseconds
			   << ",\n  \"external_export_pin_inclusive_nanoseconds\": " << report.export_telemetry.external_profile.pin_nanoseconds
			   << ",\n  \"external_export_unpin_inclusive_nanoseconds\": " << report.export_telemetry.external_profile.unpin_nanoseconds
			   << ",\n  \"external_export_page_locate_nanoseconds\": " << report.export_telemetry.external_profile.page_locate_nanoseconds
			   << ",\n  \"external_export_page_cache_find_nanoseconds\": " << report.export_telemetry.external_profile.page_cache_find_nanoseconds
			   << ",\n  \"external_export_page_victim_nanoseconds\": " << report.export_telemetry.external_profile.page_victim_nanoseconds
			   << ",\n  \"external_export_page_store_lookup_inclusive_nanoseconds\": " << report.export_telemetry.external_profile.page_store_lookup_nanoseconds
			   << ",\n  \"external_export_page_decode_validation_nanoseconds\": " << report.export_telemetry.external_profile.page_decode_nanoseconds
			   << ",\n  \"external_export_page_zero_nanoseconds\": " << report.export_telemetry.external_profile.page_zero_nanoseconds
			   << ",\n  \"external_export_page_decoded_count\": " << report.export_telemetry.external_profile.page_decoded_count
			   << ",\n  \"external_export_page_zero_count\": " << report.export_telemetry.external_profile.page_zero_count
			   << ",\n  \"external_export_page_index_opens\": " << report.export_telemetry.external_profile.page_index_opens
			   << ",\n  \"external_export_page_index_reads\": " << report.export_telemetry.external_profile.page_index_reads
			   << ",\n  \"external_export_page_run_range_skips\": " << report.export_telemetry.external_profile.page_run_range_skips
			   << ",\n  \"external_export_page_membership_misses\": " << report.export_telemetry.external_profile.page_membership_misses
			   << ",\n  \"external_export_page_data_opens\": " << report.export_telemetry.external_profile.page_data_opens
			   << ",\n  \"external_export_page_data_reads\": " << report.export_telemetry.external_profile.page_data_reads
			   << ",\n  \"external_export_page_requested_read_bytes\": " << report.export_telemetry.external_profile.page_requested_read_bytes
			   << ",\n  \"external_export_page_index_open_nanoseconds\": " << report.export_telemetry.external_profile.page_index_open_nanoseconds
			   << ",\n  \"external_export_page_index_search_nanoseconds\": " << report.export_telemetry.external_profile.page_index_search_nanoseconds
			   << ",\n  \"external_export_page_data_open_nanoseconds\": " << report.export_telemetry.external_profile.page_data_open_nanoseconds
			   << ",\n  \"external_export_page_data_read_nanoseconds\": " << report.export_telemetry.external_profile.page_data_read_nanoseconds
			   << ",\n  \"external_export_finish_inclusive_nanoseconds\": " << report.export_telemetry.external_profile.finish_nanoseconds
			   << ",\n  \"external_export_sampled_rows\": " << report.export_telemetry.external_profile.sampled_rows
			   << ",\n  \"external_export_sampled_average_nanoseconds\": " << report.export_telemetry.external_profile.sampled_average_nanoseconds
			   << ",\n  \"external_export_sampled_quantize_nanoseconds\": " << report.export_telemetry.external_profile.sampled_quantize_nanoseconds
			   << ",\n  \"external_export_sampled_writer_inclusive_nanoseconds\": " << report.export_telemetry.external_profile.sampled_writer_nanoseconds
			   << ",\n  \"external_export_writer_sampled_rows\": " << report.export_telemetry.external_profile.writer_sampled_rows
			   << ",\n  \"external_export_writer_sampled_validation_nanoseconds\": " << report.export_telemetry.external_profile.writer_sampled_validation_nanoseconds
			   << ",\n  \"external_export_writer_sampled_payload_hash_nanoseconds\": " << report.export_telemetry.external_profile.writer_sampled_payload_hash_nanoseconds
			   << ",\n  \"external_export_writer_sampled_policy_hash_nanoseconds\": " << report.export_telemetry.external_profile.writer_sampled_policy_hash_nanoseconds
			   << ",\n  \"external_export_writer_sampled_encoding_inclusive_nanoseconds\": " << report.export_telemetry.external_profile.writer_sampled_encoding_nanoseconds
			   << ",\n  \"external_export_writer_flushes\": " << report.export_telemetry.external_profile.writer_flushes
			   << ",\n  \"external_export_catalog_lookups\": " << report.export_telemetry.external_profile.catalog.lookups
			   << ",\n  \"external_export_catalog_cache_hits\": " << report.export_telemetry.external_profile.catalog.cache_hits
			   << ",\n  \"external_export_catalog_cache_misses\": " << report.export_telemetry.external_profile.catalog.cache_misses
			   << ",\n  \"external_export_catalog_run_probes\": " << report.export_telemetry.external_profile.catalog.run_probes
			   << ",\n  \"external_export_catalog_miss_nanoseconds\": " << report.export_telemetry.external_profile.catalog.read_nanoseconds
			   << ",\n  \"external_export_row_index_lookups\": " << report.export_telemetry.external_profile.row_index.lookups
			   << ",\n  \"external_export_row_index_cache_hits\": " << report.export_telemetry.external_profile.row_index.cache_hits
			   << ",\n  \"external_export_row_index_cache_misses\": " << report.export_telemetry.external_profile.row_index.cache_misses
			   << ",\n  \"external_export_row_index_run_probes\": " << report.export_telemetry.external_profile.row_index.run_probes
			   << ",\n  \"external_export_row_index_miss_nanoseconds\": " << report.export_telemetry.external_profile.row_index.read_nanoseconds
			   << ",\n  \"external_export_page_store_lookups\": " << report.export_telemetry.external_profile.page_store.lookups
			   << ",\n  \"external_export_page_store_cache_hits\": " << report.export_telemetry.external_profile.page_store.cache_hits
			   << ",\n  \"external_export_page_store_cache_misses\": " << report.export_telemetry.external_profile.page_store.cache_misses
			   << ",\n  \"external_export_page_store_run_probes\": " << report.export_telemetry.external_profile.page_store.run_probes
			   << ",\n  \"external_export_page_store_miss_nanoseconds\": " << report.export_telemetry.external_profile.page_store.read_nanoseconds
			   << ",\n  \"external_export_page_cache_hits\": " << report.export_telemetry.external_profile.page_store.page_hits
			   << ",\n  \"external_export_page_cache_misses\": " << report.export_telemetry.external_profile.page_store.page_misses
			   << ",\n  \"export_publication_mode\": \"" << core::atomic_publish_mode_name(report.export_telemetry.publication_mode) << "\""
			   << ",\n  \"process_rss_available\": " << (report.process_rss_available ? "true" : "false")
			   << ",\n  \"started_at_preflop\": " << (report.started_at_preflop ? "true" : "false")
			   << ",\n  \"peak_public_states\": " << report.peak_public_states
			   << ",\n  \"peak_sparse_rows\": " << report.peak_sparse_rows
			   << ",\n  \"peak_sparse_values\": " << report.peak_sparse_values
			   << ",\n  \"peak_worker_delta_entries\": " << report.peak_worker_delta_entries
			   << ",\n  \"total_peak_regret_delta_entries\": " << report.total_peak_regret_delta_entries
			   << ",\n  \"total_peak_continuation_delta_entries\": " << report.total_peak_continuation_delta_entries
			   << ",\n  \"total_peak_delta_entries\": " << report.total_peak_delta_entries
			   << ",\n  \"generated_update_count\": " << report.generated_update_count
			   << ",\n  \"generated_discovery_public_state_count\": " << report.generated_discovery_public_state_count
			   << ",\n  \"generated_discovery_row_count\": " << report.generated_discovery_row_count
			   << ",\n  \"coordinator_admission_nanoseconds\": " << report.coordinator_admission_nanoseconds
			   << ",\n  \"trainer_only_nanoseconds\": " << report.trainer_only_nanoseconds
			   << ",\n  \"process_cpu_nanoseconds\": " << report.process_cpu_nanoseconds
			   << ",\n  \"peak_working_set_bytes\": " << report.peak_working_set_bytes
			   << ",\n  \"placement_policy\": \"" << multiway_worker_placement_policy_name(report.placement_policy) << "\""
			   << ",\n  \"affinity_requested\": " << (report.affinity_requested ? "true" : "false")
			   << ",\n  \"peak_compact_storage_bytes\": " << report.peak_compact_storage_bytes
			   << ",\n  \"configured_max_public_states\": " << report.configured_max_public_states
			   << ",\n  \"configured_max_sparse_rows\": " << report.configured_max_sparse_rows
			   << ",\n  \"configured_max_sparse_values\": " << report.configured_max_sparse_values
			   << ",\n  \"configured_worker_delta_capacity\": " << report.configured_worker_delta_capacity
			   << ",\n  \"configured_discovery_capacity\": " << report.configured_discovery_capacity
			   << ",\n  \"configured_max_regret_delta_entries_per_worker\": " << report.configured_max_regret_delta_entries_per_worker
			   << ",\n  \"configured_max_continuation_delta_entries_per_worker\": " << report.configured_max_continuation_delta_entries_per_worker
			   << ",\n  \"configured_max_discovery_public_states_per_worker\": " << report.configured_max_discovery_public_states_per_worker
			   << ",\n  \"configured_max_discovery_rows_per_worker\": " << report.configured_max_discovery_rows_per_worker
			   << ",\n  \"cumulative_worker_delta_entries\": " << report.cumulative_worker_delta_entries
			   << ",\n  \"worker_active_nanoseconds\": " << report.worker_active_nanoseconds
			   << ",\n  \"worker_wake_latency_nanoseconds\": " << report.worker_wake_latency_nanoseconds
			   << ",\n  \"worker_cv_wait_nanoseconds\": " << report.worker_cv_wait_nanoseconds
			   << ",\n  \"worker_scheduler_claim_nanoseconds\": " << report.worker_scheduler_claim_nanoseconds
			   << ",\n  \"worker_completion_nanoseconds\": " << report.worker_completion_nanoseconds
			   << ",\n  \"worker_tail_idle_nanoseconds\": " << report.worker_tail_idle_nanoseconds
			   << ",\n  \"minimum_worker_active_nanoseconds\": " << report.minimum_worker_active_nanoseconds
			   << ",\n  \"maximum_worker_active_nanoseconds\": " << report.maximum_worker_active_nanoseconds
			   << ",\n  \"coordinator_wait_nanoseconds\": " << report.coordinator_wait_nanoseconds
			   << ",\n  \"barrier_wait_nanoseconds\": " << report.barrier_wait_nanoseconds
			   << ",\n  \"delta_sort_nanoseconds\": " << report.delta_sort_nanoseconds
			   << ",\n  \"merge_nanoseconds\": " << report.merge_nanoseconds
			   << ",\n  \"minimum_worker_trajectories\": " << report.minimum_worker_trajectories
			   << ",\n  \"maximum_worker_trajectories\": " << report.maximum_worker_trajectories
			   << ",\n  \"requested_worker_count\": " << report.requested_worker_count
			   << ",\n  \"effective_worker_count\": " << report.effective_worker_count
			   << ",\n  \"trajectories_per_batch\": " << report.trajectories_per_batch
			   << ",\n  \"worker_trajectory_counts\": [";
		for (std::size_t index = 0U; index < report.worker_trajectory_counts.size(); ++index)
		{
			if (index != 0U)
				output << ',';
			output << report.worker_trajectory_counts[index];
		}
		output << "]"
			   << ",\n  \"worker_active_nanoseconds_by_worker\": [";
		for (std::size_t index = 0U; index < report.worker_active_nanoseconds_by_worker.size(); ++index)
		{
			if (index != 0U)
				output << ',';
			output << report.worker_active_nanoseconds_by_worker[index];
		}
		output << "]";
		write_u64_array("worker_sort_nanoseconds_by_worker", report.worker_sort_nanoseconds_by_worker);
		write_u64_array("worker_wake_latency_nanoseconds_by_worker", report.worker_wake_latency_nanoseconds_by_worker);
		write_u64_array("worker_cv_wait_nanoseconds_by_worker", report.worker_cv_wait_nanoseconds_by_worker);
		write_u64_array("worker_scheduler_claim_nanoseconds_by_worker", report.worker_scheduler_claim_nanoseconds_by_worker);
		write_u64_array("worker_completion_nanoseconds_by_worker", report.worker_completion_nanoseconds_by_worker);
		write_u64_array("worker_tail_idle_nanoseconds_by_worker", report.worker_tail_idle_nanoseconds_by_worker);
		write_u64_array("worker_regret_delta_entries", report.worker_regret_delta_entries);
		write_u64_array("worker_continuation_delta_entries", report.worker_continuation_delta_entries);
		write_u64_array("worker_discovery_public_state_counts", report.worker_discovery_public_state_counts);
		write_u64_array("worker_discovery_row_counts", report.worker_discovery_row_counts);
		write_u64_array("worker_allocation_counts", report.worker_allocation_counts);
		write_u64_array("worker_allocated_bytes", report.worker_allocated_bytes);
		write_u64_array("worker_requested_processor", std::vector<std::uint64_t>(report.worker_requested_processor.begin(), report.worker_requested_processor.end()));
		write_u64_array("worker_resolved_processor", std::vector<std::uint64_t>(report.worker_resolved_processor.begin(), report.worker_resolved_processor.end()));
		write_u64_array("worker_requested_cpu_set", std::vector<std::uint64_t>(report.worker_requested_cpu_set.begin(), report.worker_requested_cpu_set.end()));
		write_u64_array("worker_resolved_cpu_set", std::vector<std::uint64_t>(report.worker_resolved_cpu_set.begin(), report.worker_resolved_cpu_set.end()));
		write_u64_array("worker_requested_physical_core", std::vector<std::uint64_t>(report.worker_requested_physical_core.begin(), report.worker_requested_physical_core.end()));
		write_u64_array("worker_resolved_physical_core", std::vector<std::uint64_t>(report.worker_resolved_physical_core.begin(), report.worker_resolved_physical_core.end()));
		write_u64_array("worker_requested_smt_sibling", std::vector<std::uint64_t>(report.worker_requested_smt_sibling.begin(), report.worker_requested_smt_sibling.end()));
		write_u64_array("worker_resolved_smt_sibling", std::vector<std::uint64_t>(report.worker_resolved_smt_sibling.begin(), report.worker_resolved_smt_sibling.end()));
		write_u64_array("worker_requested_cache_domain", std::vector<std::uint64_t>(report.worker_requested_cache_domain.begin(), report.worker_requested_cache_domain.end()));
		write_u64_array("worker_resolved_cache_domain", std::vector<std::uint64_t>(report.worker_resolved_cache_domain.begin(), report.worker_resolved_cache_domain.end()));
		write_u64_array("worker_migration_sampled", std::vector<std::uint64_t>(report.worker_migration_sampled.begin(), report.worker_migration_sampled.end()));
		write_u64_array("worker_migration_detected", std::vector<std::uint64_t>(report.worker_migration_detected.begin(), report.worker_migration_detected.end()));
		write_u64_array("worker_affinity_masks", report.worker_affinity_masks);
		write_u64_array("worker_affinity_succeeded", std::vector<std::uint64_t>(report.worker_affinity_succeeded.begin(), report.worker_affinity_succeeded.end()));
		const auto& admission = report.admission_diagnostics;
		output << ",\n  \"admission_diagnostics\": {\"raw_public_state_candidates\": " << admission.raw_public_state_candidates
			   << ", \"raw_row_candidates\": " << admission.raw_row_candidates
			   << ", \"worker_local_unique_public_state_candidates\": " << admission.worker_local_unique_public_state_candidates
			   << ", \"worker_local_unique_row_candidates\": " << admission.worker_local_unique_row_candidates
			   << ", \"globally_unique_public_state_candidates\": " << admission.globally_unique_public_state_candidates
			   << ", \"globally_unique_row_candidates\": " << admission.globally_unique_row_candidates
			   << ", \"already_admitted_public_state_candidates\": " << admission.already_admitted_public_state_candidates
			   << ", \"already_admitted_row_candidates\": " << admission.already_admitted_row_candidates
			   << ", \"newly_admitted_public_state_candidates\": " << admission.newly_admitted_public_state_candidates
			   << ", \"newly_admitted_row_candidates\": " << admission.newly_admitted_row_candidates
			   << ", \"parent_resolution_passes\": " << admission.parent_resolution_passes
			   << ", \"parent_resolution_probes\": " << admission.parent_resolution_probes
			   << ", \"descriptor_reconstructions\": " << admission.descriptor_reconstructions
			   << ", \"packed_public_state_compatibility_checks\": " << admission.packed_public_state_compatibility_checks
			   << ", \"existing_public_state_checks\": " << admission.existing_public_state_checks
			   << ", \"new_public_state_validations\": " << admission.new_public_state_validations
			   << ", \"parent_catalog_resolutions\": " << admission.parent_catalog_resolutions
			   << ", \"row_batch_state_resolutions\": " << admission.row_batch_state_resolutions
			   << ", \"row_catalog_state_resolutions\": " << admission.row_catalog_state_resolutions
			   << ", \"lock_acquisitions\": " << admission.lock_acquisitions
			   << ", \"discovery_prepare_nanoseconds\": " << admission.discovery_prepare_nanoseconds
			   << ", \"discovery_merge_nanoseconds\": " << admission.discovery_merge_nanoseconds
			   << ", \"discovery_public_state_merge_nanoseconds\": " << admission.discovery_public_state_merge_nanoseconds
			   << ", \"discovery_row_merge_nanoseconds\": " << admission.discovery_row_merge_nanoseconds
			   << ", \"discovery_public_state_source_elements\": " << admission.discovery_public_state_source_elements
			   << ", \"discovery_row_source_elements\": " << admission.discovery_row_source_elements
			   << ", \"discovery_public_descriptor_copy_count\": " << admission.discovery_public_descriptor_copy_count
			   << ", \"discovery_public_descriptor_copy_bytes\": " << admission.discovery_public_descriptor_copy_bytes
			   << ", \"discovery_nested_descriptor_copy_count\": " << admission.discovery_nested_descriptor_copy_count
			   << ", \"discovery_nested_descriptor_copy_bytes\": " << admission.discovery_nested_descriptor_copy_bytes
			   << ", \"discovery_output_capacity_reuse_events\": " << admission.discovery_output_capacity_reuse_events
			   << ", \"discovery_output_capacity_reuse_bytes\": " << admission.discovery_output_capacity_reuse_bytes
			   << ", \"discovery_public_state_duplicate_candidates\": " << admission.discovery_public_state_duplicate_candidates
			   << ", \"discovery_row_duplicate_candidates\": " << admission.discovery_row_duplicate_candidates
			   << ", \"discovery_single_stream_batches\": " << admission.discovery_single_stream_batches
			   << ", \"discovery_one_stream_fast_path_uses\": " << admission.discovery_one_stream_fast_path_uses
			   << ", \"parent_order_nanoseconds\": " << admission.parent_order_nanoseconds
			   << ", \"state_catalog_classification_nanoseconds\": " << admission.state_catalog_classification_nanoseconds
			   << ", \"public_state_validation_nanoseconds\": " << admission.public_state_validation_nanoseconds
			   << ", \"parent_transition_validation_nanoseconds\": " << admission.parent_transition_validation_nanoseconds
			   << ", \"state_preflight_nanoseconds\": " << admission.state_preflight_nanoseconds
			   << ", \"row_state_resolution_nanoseconds\": " << admission.row_state_resolution_nanoseconds
			   << ", \"row_shape_validation_nanoseconds\": " << admission.row_shape_validation_nanoseconds
			   << ", \"row_preflight_nanoseconds\": " << admission.row_preflight_nanoseconds
			   << ", \"catalog_commit_nanoseconds\": " << admission.catalog_commit_nanoseconds
			   << ", \"storage_commit_nanoseconds\": " << admission.storage_commit_nanoseconds
			   << ", \"catalog_menu_lookup_nanoseconds\": " << admission.catalog_menu_lookup_nanoseconds
			   << ", \"catalog_menu_key_nanoseconds\": " << admission.catalog_menu_key_nanoseconds
			   << ", \"catalog_menu_index_lookup_nanoseconds\": " << admission.catalog_menu_index_lookup_nanoseconds
			   << ", \"catalog_menu_equality_nanoseconds\": " << admission.catalog_menu_equality_nanoseconds
			   << ", \"catalog_node_pack_nanoseconds\": " << admission.catalog_node_pack_nanoseconds
			   << ", \"catalog_reserve_nanoseconds\": " << admission.catalog_reserve_nanoseconds
			   << ", \"catalog_append_nanoseconds\": " << admission.catalog_append_nanoseconds
			   << ", \"catalog_index_merge_nanoseconds\": " << admission.catalog_index_merge_nanoseconds
			   << ", \"catalog_index_insertion_nanoseconds\": " << admission.catalog_index_insertion_nanoseconds
			   << ", \"catalog_menu_index_probes\": " << admission.catalog_menu_index_probes
			   << ", \"catalog_menu_equality_checks\": " << admission.catalog_menu_equality_checks
			   << ", \"catalog_existing_menu_hits\": " << admission.catalog_existing_menu_hits
			   << ", \"catalog_batch_menu_reuse_hits\": " << admission.catalog_batch_menu_reuse_hits
			   << ", \"catalog_new_menus\": " << admission.catalog_new_menus
			   << ", \"catalog_menu_id_collision_misses\": " << admission.catalog_menu_id_collision_misses
			   << ", \"catalog_output_capacity_reuse_events\": " << admission.catalog_output_capacity_reuse_events
			   << ", \"catalog_output_capacity_reuse_bytes\": " << admission.catalog_output_capacity_reuse_bytes
			   << ", \"catalog_public_descriptor_copy_count\": " << admission.catalog_public_descriptor_copy_count
			   << ", \"catalog_public_descriptor_copy_bytes\": " << admission.catalog_public_descriptor_copy_bytes
			   << ", \"catalog_nested_descriptor_copy_count\": " << admission.catalog_nested_descriptor_copy_count
			   << ", \"catalog_nested_descriptor_copy_bytes\": " << admission.catalog_nested_descriptor_copy_bytes
			   << ", \"public_state_betting_board_validation_nanoseconds\": " << admission.public_state_betting_board_validation_nanoseconds
			   << ", \"public_state_menu_validation_nanoseconds\": " << admission.public_state_menu_validation_nanoseconds
			   << ", \"public_state_history_identity_validation_nanoseconds\": " << admission.public_state_history_identity_validation_nanoseconds
			   << ", \"row_admission_nanoseconds\": " << admission.row_admission_nanoseconds
			   << ", \"parent_transition_fixed_state_nanoseconds\": " << admission.parent_transition_fixed_state_nanoseconds
			   << ", \"parent_transition_action_edge_nanoseconds\": " << admission.parent_transition_action_edge_nanoseconds
			   << ", \"parent_transition_board_chance_edge_nanoseconds\": " << admission.parent_transition_board_chance_edge_nanoseconds
			   << ", \"parent_transition_street_edge_nanoseconds\": " << admission.parent_transition_street_edge_nanoseconds
			   << ", \"parent_transition_action_edges\": " << admission.parent_transition_action_edges
			   << ", \"parent_transition_board_chance_edges\": " << admission.parent_transition_board_chance_edges
			   << ", \"parent_transition_street_edges\": " << admission.parent_transition_street_edges
			   << ", \"parent_transition_action_history_prefix_elements\": " << admission.parent_transition_action_history_prefix_elements
			   << ", \"parent_transition_action_menu_probes\": " << admission.parent_transition_action_menu_probes;
		const auto write_admission_array = [&output](const char* name, const std::vector<std::uint64_t>& values) {
			output << ", \"" << name << "\": [";
			for (std::size_t index = 0U; index < values.size(); ++index)
			{
				if (index != 0U)
					output << ',';
				output << values[index];
			}
			output << ']';
		};
		write_admission_array("worker_raw_public_state_candidates_by_worker", admission.worker_raw_public_state_candidates_by_worker);
		write_admission_array("worker_raw_row_candidates_by_worker", admission.worker_raw_row_candidates_by_worker);
		write_admission_array("worker_local_unique_public_state_candidates_by_worker", admission.worker_local_unique_public_state_candidates_by_worker);
		write_admission_array("worker_local_unique_row_candidates_by_worker", admission.worker_local_unique_row_candidates_by_worker);
		output << '}';
		if (report.failed_batch_available)
		{
			const auto& batch = report.failed_batch;
			output << ",\n  \"failed_batch\": {\"trajectories_attempted\": " << batch.trajectories_attempted
				   << ", \"trajectories_accepted\": " << batch.trajectories_accepted
				   << ", \"trajectories_discarded\": " << batch.trajectories_discarded
				   << ", \"delta_entries_merged\": " << batch.delta_entries_merged
				   << ", \"total_peak_delta_entries\": " << batch.total_peak_delta_entries
				   << ", \"generated_update_count\": " << batch.generated_update_count
				   << ", \"worker_active_nanoseconds\": " << batch.worker_active_nanoseconds
				   << ", \"worker_wake_latency_nanoseconds\": " << batch.worker_wake_latency_nanoseconds
				   << ", \"worker_cv_wait_nanoseconds\": " << batch.worker_cv_wait_nanoseconds
				   << ", \"worker_scheduler_claim_nanoseconds\": " << batch.worker_scheduler_claim_nanoseconds
				   << ", \"worker_completion_nanoseconds\": " << batch.worker_completion_nanoseconds
				   << ", \"worker_tail_idle_nanoseconds\": " << batch.worker_tail_idle_nanoseconds
				   << ", \"coordinator_wait_nanoseconds\": " << batch.coordinator_wait_nanoseconds
				   << ", \"coordinator_admission_nanoseconds\": " << batch.coordinator_admission_nanoseconds
				   << ", \"coordinator_allocation_count\": " << batch.coordinator_allocation_count
				   << ", \"coordinator_allocated_bytes\": " << batch.coordinator_allocated_bytes
				   << ", \"barrier_wait_nanoseconds\": " << batch.barrier_wait_nanoseconds
				   << ", \"delta_sort_nanoseconds\": " << batch.delta_sort_nanoseconds
				   << ", \"merge_nanoseconds\": " << batch.merge_nanoseconds
				   << ", \"regret_delta_capacity_exhausted\": "
				   << (batch.regret_delta_capacity_exhausted ? "true" : "false")
				   << ", \"continuation_delta_capacity_exhausted\": "
				   << (batch.continuation_delta_capacity_exhausted ? "true" : "false")
				   << ", \"discovery_public_state_capacity_exhausted\": "
				   << (batch.discovery_public_state_capacity_exhausted ? "true" : "false")
				   << ", \"discovery_row_capacity_exhausted\": "
				   << (batch.discovery_row_capacity_exhausted ? "true" : "false")
				   << ", \"capacity_exhausted\": "
				   << (batch.capacity_exhausted ? "true" : "false")
				   << ", \"discovery_capacity_exhausted\": "
				   << (batch.discovery_capacity_exhausted ? "true" : "false");
			const auto write_failed_array = [&output](const char* name, const std::vector<std::uint64_t>& values) {
				output << ", \"" << name << "\": [";
				for (std::size_t index = 0U; index < values.size(); ++index)
				{
					if (index != 0U)
						output << ',';
					output << values[index];
				}
				output << ']';
			};
			write_failed_array("worker_trajectory_counts", batch.worker_trajectory_counts);
			write_failed_array("worker_active_nanoseconds_by_worker", batch.worker_active_nanoseconds_by_worker);
			write_failed_array("worker_sort_nanoseconds_by_worker", batch.worker_sort_nanoseconds_by_worker);
			write_failed_array("worker_wake_latency_nanoseconds_by_worker", batch.worker_wake_latency_nanoseconds_by_worker);
			write_failed_array("worker_cv_wait_nanoseconds_by_worker", batch.worker_cv_wait_nanoseconds_by_worker);
			write_failed_array("worker_scheduler_claim_nanoseconds_by_worker", batch.worker_scheduler_claim_nanoseconds_by_worker);
			write_failed_array("worker_completion_nanoseconds_by_worker", batch.worker_completion_nanoseconds_by_worker);
			write_failed_array("worker_tail_idle_nanoseconds_by_worker", batch.worker_tail_idle_nanoseconds_by_worker);
			write_failed_array("worker_regret_delta_entries", batch.worker_regret_delta_entries);
			write_failed_array("worker_continuation_delta_entries", batch.worker_continuation_delta_entries);
			write_failed_array("worker_discovery_public_state_counts", batch.worker_discovery_public_state_counts);
			write_failed_array("worker_discovery_row_counts", batch.worker_discovery_row_counts);
			write_failed_array("worker_allocation_counts", batch.worker_allocation_counts);
			write_failed_array("worker_allocated_bytes", batch.worker_allocated_bytes);
			write_failed_array("worker_requested_processor", std::vector<std::uint64_t>(batch.worker_requested_processor.begin(), batch.worker_requested_processor.end()));
			write_failed_array("worker_resolved_processor", std::vector<std::uint64_t>(batch.worker_resolved_processor.begin(), batch.worker_resolved_processor.end()));
			write_failed_array("worker_requested_cpu_set", std::vector<std::uint64_t>(batch.worker_requested_cpu_set.begin(), batch.worker_requested_cpu_set.end()));
			write_failed_array("worker_resolved_cpu_set", std::vector<std::uint64_t>(batch.worker_resolved_cpu_set.begin(), batch.worker_resolved_cpu_set.end()));
			write_failed_array("worker_requested_physical_core", std::vector<std::uint64_t>(batch.worker_requested_physical_core.begin(), batch.worker_requested_physical_core.end()));
			write_failed_array("worker_resolved_physical_core", std::vector<std::uint64_t>(batch.worker_resolved_physical_core.begin(), batch.worker_resolved_physical_core.end()));
			write_failed_array("worker_requested_smt_sibling", std::vector<std::uint64_t>(batch.worker_requested_smt_sibling.begin(), batch.worker_requested_smt_sibling.end()));
			write_failed_array("worker_resolved_smt_sibling", std::vector<std::uint64_t>(batch.worker_resolved_smt_sibling.begin(), batch.worker_resolved_smt_sibling.end()));
			write_failed_array("worker_requested_cache_domain", std::vector<std::uint64_t>(batch.worker_requested_cache_domain.begin(), batch.worker_requested_cache_domain.end()));
			write_failed_array("worker_resolved_cache_domain", std::vector<std::uint64_t>(batch.worker_resolved_cache_domain.begin(), batch.worker_resolved_cache_domain.end()));
			write_failed_array("worker_migration_sampled", std::vector<std::uint64_t>(batch.worker_migration_sampled.begin(), batch.worker_migration_sampled.end()));
			write_failed_array("worker_migration_detected", std::vector<std::uint64_t>(batch.worker_migration_detected.begin(), batch.worker_migration_detected.end()));
			write_failed_array("worker_affinity_masks", batch.worker_affinity_masks);
			write_failed_array("worker_affinity_succeeded", std::vector<std::uint64_t>(batch.worker_affinity_succeeded.begin(), batch.worker_affinity_succeeded.end()));
			output << '}';
		}
		output << ",\n  \"worker_profiles\": [";
		for (std::size_t worker = 0U; worker < report.worker_profiles.size(); ++worker)
		{
			if (worker != 0U)
				output << ',';
			const auto& profile = report.worker_profiles[worker];
			output << "{\"mode\": " << static_cast<unsigned>(profile.mode) << ", \"checkpoints\": [";
			for (std::size_t stage = 0U; stage < profile.checkpoints.size(); ++stage)
			{
				if (stage != 0U)
					output << ',';
				output << "{\"elapsed_nanoseconds\": " << profile.checkpoints[stage].elapsed_nanoseconds
					   << ", \"calls\": " << profile.checkpoints[stage].calls << '}';
			}
			output << "]}";
		}
		output << "]"
			   << ",\n  \"memory_preflight_estimate_bytes\": " << report.memory_preflight_estimate_bytes
			   << ",\n  \"memory_estimate_schema_version\": " << report.memory_ledger.schema_version
			   << ",\n  \"memory_estimated_private_peak_bytes\": " << report.memory_ledger.estimated_private_peak_bytes
			   << ",\n  \"memory_estimated_working_set_peak_bytes\": " << report.memory_ledger.estimated_working_set_peak_bytes
			   << ",\n  \"memory_ledger\": {\"schema_version\": " << report.memory_ledger.schema_version
			   << ", \"existing_working_set_bytes\": " << report.memory_ledger.existing_working_set_bytes
			   << ", \"existing_private_committed_bytes\": " << report.memory_ledger.existing_private_committed_bytes
			   << ", \"additional_private_capacity_bytes\": " << report.memory_ledger.additional_private_capacity_bytes
			   << ", \"compact_reallocation_peak_bytes\": " << report.memory_ledger.compact_reallocation_peak_bytes
			   << ", \"external_storage_cache_bytes\": " << report.memory_ledger.external_storage_cache_bytes
			   << ", \"external_storage_scratch_bytes\": " << report.memory_ledger.external_storage_scratch_bytes
			   << ", \"external_storage_disk_reserve_bytes\": " << report.memory_ledger.external_storage_disk_reserve_bytes
			   << ", \"public_catalog_reallocation_peak_bytes\": " << report.memory_ledger.public_catalog_reallocation_peak_bytes
			   << ", \"file_cache_allowance_bytes\": " << report.memory_ledger.file_cache_allowance_bytes
			   << ", \"disk_output_bytes\": " << report.memory_ledger.disk_output_bytes
			   << ", \"mapped_virtual_bytes\": " << report.memory_ledger.mapped_virtual_bytes
			   << ", \"mapped_resident_allowance_bytes\": " << report.memory_ledger.mapped_resident_allowance_bytes
			   << ", \"checkpoint_transient_bytes\": " << report.memory_ledger.checkpoint_transient_bytes
			   << ", \"export_transient_bytes\": " << report.memory_ledger.export_transient_bytes
			   << ", \"export_scratch_bytes\": " << report.memory_ledger.export_scratch_bytes
			   << ", \"export_output_bytes\": " << report.memory_ledger.export_output_bytes
			   << ", \"export_process_peak_bytes\": " << report.memory_ledger.export_process_peak_bytes
			   << ", \"export_system_cache_allowance_bytes\": " << report.memory_ledger.export_system_cache_allowance_bytes
			   << ", \"export_observed_before_working_set_bytes\": " << report.memory_ledger.export_observed_before_working_set_bytes
			   << ", \"export_observed_after_working_set_bytes\": " << report.memory_ledger.export_observed_after_working_set_bytes
			   << ", \"configured_capacity_growth_bytes\": " << report.memory_ledger.configured_capacity_growth_bytes
			   << ", \"actual_retained_capacity_bytes\": " << report.memory_ledger.actual_retained_capacity_bytes
			   << ", \"estimated_private_peak_bytes\": " << report.memory_ledger.estimated_private_peak_bytes
			   << ", \"estimated_working_set_peak_bytes\": " << report.memory_ledger.estimated_working_set_peak_bytes
			   << ", \"resident_baseline_bytes\": " << report.memory_ledger.resident_baseline_bytes
			   << ", \"bucket_registry_bytes\": " << report.memory_ledger.bucket_registry_bytes
			   << ", \"mapped_bucket_artifact_bytes\": " << report.memory_ledger.mapped_bucket_artifact_bytes
			   << ", \"public_catalog_bytes\": " << report.memory_ledger.public_catalog_bytes
			   << ", \"row_metadata_bytes\": " << report.memory_ledger.row_metadata_bytes
			   << ", \"page_metadata_bytes\": " << report.memory_ledger.page_metadata_bytes
			   << ", \"regret_page_bytes\": " << report.memory_ledger.regret_page_bytes
			   << ", \"worker_stream_bytes\": " << report.memory_ledger.worker_stream_bytes
			   << ", \"discovery_stream_bytes\": " << report.memory_ledger.discovery_stream_bytes
			   << ", \"worker_allocation_count\": " << report.memory_ledger.worker_allocation_count
			   << ", \"worker_allocation_bytes\": " << report.memory_ledger.worker_allocation_bytes
			   << ", \"coordinator_allocation_count\": " << report.memory_ledger.coordinator_allocation_count
			   << ", \"coordinator_allocation_bytes\": " << report.memory_ledger.coordinator_allocation_bytes
			   << ", \"merge_scratch_bytes\": " << report.memory_ledger.merge_scratch_bytes
			   << ", \"checkpoint_peak_bytes\": " << report.memory_ledger.checkpoint_peak_bytes
			   << ", \"export_peak_bytes\": " << report.memory_ledger.export_peak_bytes
			   << ", \"allocator_overhead_bytes\": " << report.memory_ledger.allocator_overhead_bytes
			   << ", \"live_bytes\": " << report.memory_ledger.live_bytes
			   << ", \"reserved_bytes\": " << report.memory_ledger.reserved_bytes
			   << ", \"committed_bytes\": " << report.memory_ledger.committed_bytes << "}"
			   << ",\n  \"memory_schema_version\": " << MULTIWAY_PROCESS_MEMORY_SCHEMA_VERSION
			   << ",\n  \"memory_process_snapshots\": [";
		for (std::size_t index = 0U; index < report.process_memory_snapshots.count; ++index)
		{
			if (index != 0U)
				output << ',';
			const auto& snapshot = report.process_memory_snapshots.snapshots[index];
			output << "{\"phase_id\": " << static_cast<unsigned>(snapshot.phase_id)
				   << ", \"phase\": \"" << multiway_memory_phase_name(snapshot.phase_id)
				   << "\", \"current_working_set_bytes\": " << snapshot.current_working_set_bytes
				   << ", \"peak_working_set_bytes\": " << snapshot.peak_working_set_bytes
				   << ", \"private_committed_bytes\": " << snapshot.private_committed_bytes
				   << ", \"peak_private_committed_bytes\": " << snapshot.peak_private_committed_bytes
				   << ", \"virtual_address_bytes\": " << snapshot.virtual_address_bytes
				   << ", \"anonymous_resident_bytes\": " << snapshot.anonymous_resident_bytes
				   << ", \"file_resident_bytes\": " << snapshot.file_resident_bytes
				   << ", \"minor_page_faults\": " << snapshot.minor_page_faults
				   << ", \"major_page_faults\": " << snapshot.major_page_faults
				   << ", \"total_page_faults\": " << snapshot.total_page_faults
				   << ", \"availability\": {\"current_working_set\": "
				   << (snapshot.current_working_set_available ? "true" : "false")
				   << ", \"peak_working_set\": " << (snapshot.peak_working_set_available ? "true" : "false")
				   << ", \"private_committed\": " << (snapshot.private_committed_available ? "true" : "false")
				   << ", \"peak_private_committed\": " << (snapshot.peak_private_committed_available ? "true" : "false")
				   << ", \"virtual_address\": " << (snapshot.virtual_address_available ? "true" : "false")
				   << ", \"anonymous_resident\": " << (snapshot.anonymous_resident_available ? "true" : "false")
				   << ", \"file_resident\": " << (snapshot.file_resident_available ? "true" : "false")
				   << ", \"minor_page_faults\": " << (snapshot.minor_page_faults_available ? "true" : "false")
				   << ", \"major_page_faults\": " << (snapshot.major_page_faults_available ? "true" : "false")
				   << ", \"total_page_faults\": " << (snapshot.total_page_faults_available ? "true" : "false")
				   << "}}";
		}
		output << "]"
			   << ",\n  \"memory_subsystems_schema_version\": " << MULTIWAY_MEMORY_SUBSYSTEM_SCHEMA_VERSION
			   << ",\n  \"memory_subsystems\": {\"enabled\": "
			   << (report.memory_subsystems.enabled ? "true" : "false")
			   << ", \"completed\": " << (report.memory_subsystems.completed ? "true" : "false")
			   << ", \"snapshots\": [";
		for (std::size_t index = 0U; index < report.memory_subsystems.count; ++index)
		{
			if (index != 0U)
				output << ',';
			const auto& snapshot = report.memory_subsystems.snapshots[index];
			output << "{\"phase_id\": " << static_cast<unsigned>(snapshot.phase_id)
				   << ", \"phase\": \"" << multiway_memory_phase_name(snapshot.phase_id)
				   << "\", \"worker_count\": " << snapshot.worker_count
				   << ", \"live_bytes\": " << snapshot.live_bytes
				   << ", \"retained_capacity_bytes\": " << snapshot.retained_capacity_bytes
				   << ", \"arithmetic_saturated\": " << (snapshot.arithmetic_saturated ? "true" : "false")
				   << ", \"components\": {";
			bool first_component = true;
			const auto write_component = [&first_component, &write_memory_component](const char* name, const MultiwayMemoryComponent& component) {
				write_memory_component(name, component, !first_component);
				first_component = false;
			};
			write_component("bucket_registry_owned", snapshot.bucket_registry_owned);
			write_component("bucket_mapping_virtual", snapshot.bucket_mapping_virtual);
			write_component("public_catalog_nodes", snapshot.public_catalog_nodes);
			write_component("public_catalog_indexes", snapshot.public_catalog_indexes);
			write_component("public_catalog_menus", snapshot.public_catalog_menus);
			write_component("public_catalog_nested_payloads", snapshot.public_catalog_nested_payloads);
			write_component("public_catalog_scratch", snapshot.public_catalog_scratch);
			write_component("compact_row_metadata", snapshot.compact_row_metadata);
			write_component("compact_page_metadata", snapshot.compact_page_metadata);
			write_component("compact_regret_cells", snapshot.compact_regret_cells);
			write_component("compact_strategy_cells", snapshot.compact_strategy_cells);
			write_component("worker_regret_delta_stream", snapshot.worker_regret_delta_stream);
			write_component("worker_continuation_delta_stream", snapshot.worker_continuation_delta_stream);
			write_component("discovery_public_state_stream", snapshot.discovery_public_state_stream);
			write_component("discovery_row_stream", snapshot.discovery_row_stream);
			write_component("worker_action_menu_scratch", snapshot.worker_action_menu_scratch);
			write_component("discovery_merge_scratch", snapshot.discovery_merge_scratch);
			write_component("batch_runner_scratch", snapshot.batch_runner_scratch);
			write_component("coordinator_merge_scratch", snapshot.coordinator_merge_scratch);
			write_component("coordinator_admission_scratch", snapshot.coordinator_admission_scratch);
			write_component("checkpoint_retained_buffers", snapshot.checkpoint_retained_buffers);
			write_component("export_retained_buffers", snapshot.export_retained_buffers);
			write_component("continuation_cache", snapshot.continuation_cache);
			write_component("continuation_scratch", snapshot.continuation_scratch);
			output << "}, \"workers\": [";
			for (std::size_t worker = 0U; worker < snapshot.worker_count; ++worker)
			{
				if (worker != 0U)
					output << ',';
				output << '{';
				bool first_worker_component = true;
				const auto write_worker_component = [&first_worker_component, &write_memory_component](const char* name, const MultiwayMemoryComponent& component) {
					write_memory_component(name, component, !first_worker_component);
					first_worker_component = false;
				};
				const auto& worker_snapshot = snapshot.workers[worker];
				write_worker_component("regret_delta_stream", worker_snapshot.regret_delta_stream);
				write_worker_component("continuation_delta_stream", worker_snapshot.continuation_delta_stream);
				write_worker_component("discovery_public_states", worker_snapshot.discovery_public_states);
				write_worker_component("discovery_rows", worker_snapshot.discovery_rows);
				write_worker_component("action_menu_scratch", worker_snapshot.action_menu_scratch);
				write_worker_component("total", worker_snapshot.total);
				output << '}';
			}
			output << "]}";
		}
		output << "]}"
			   << (report.unattributed_private_bytes_available
						  ? ",\n  \"unattributed_private_bytes\": " + std::to_string(report.unattributed_private_bytes)
						  : std::string{})
			   << ",\n  \"checkpoint_bytes\": " << report.checkpoint_bytes
			   << ",\n  \"storage_backend\": \"" << report.storage_backend << "\""
			   << ",\n  \"checkpoint_schema_version\": " << report.checkpoint_schema_version
			   << ",\n  \"external_checkpoint_generation\": " << report.external_checkpoint_generation
			   << ",\n  \"external_checkpoint_segment_count\": " << report.external_checkpoint_segment_count
			   << ",\n  \"external_storage_identity_hash\": " << report.external_storage_identity_hash
			   << ",\n  \"external_storage_disk_preflight_bytes\": " << report.external_storage_disk_preflight_bytes
			   << ",\n  \"external_storage_cache_bytes\": " << report.external_storage_cache_bytes
			   << ",\n  \"external_storage_wal_limit_bytes\": " << report.external_storage_wal_limit_bytes
			   << ",\n  \"external_storage_compaction_scratch_bytes\": " << report.external_storage_compaction_scratch_bytes
			   << ",\n  \"external_storage_disk_reserve_bytes\": " << report.external_storage_disk_reserve_bytes
			   << ",\n  \"external_storage_cache_hits\": " << report.external_storage_cache_hits
			   << ",\n  \"external_storage_cache_misses\": " << report.external_storage_cache_misses
			   << ",\n  \"external_storage_cache_evictions\": " << report.external_storage_cache_evictions
			   << ",\n  \"external_storage_page_cache_hits\": " << report.external_storage_page_cache_hits
			   << ",\n  \"external_storage_page_cache_misses\": " << report.external_storage_page_cache_misses
			   << ",\n  \"external_storage_page_cache_evictions\": " << report.external_storage_page_cache_evictions
			   << ",\n  \"external_storage_compaction_read_bytes\": " << report.external_storage_compaction_read_bytes
			   << ",\n  \"external_storage_compaction_written_bytes\": " << report.external_storage_compaction_written_bytes
			   << ",\n  \"external_storage_compaction_read_amplification\": " << report.external_storage_compaction_read_amplification
			   << ",\n  \"external_storage_compaction_write_amplification\": " << report.external_storage_compaction_write_amplification
			   << ",\n  \"external_storage_live_segment_bytes\": " << report.external_storage_live_segment_bytes
			   << ",\n  \"external_storage_retained_segment_bytes\": " << report.external_storage_retained_segment_bytes
			   << ",\n  \"external_storage_counter_scope\": \"" << report.external_storage_counter_scope << "\""
			   << ",\n  \"external_checkpoint_restore_profiled\": " << (report.external_checkpoint_restore_profiled ? "true" : "false")
			   << ",\n  \"external_checkpoint_restore_nanoseconds\": " << report.external_checkpoint_restore_nanoseconds
			   << ",\n  \"external_checkpoint_session_resume_nanoseconds\": " << report.external_checkpoint_session_resume_nanoseconds
			   << ",\n  \"checkpoint_planned_bytes\": " << report.checkpoint_planned_bytes
			   << ",\n  \"checkpoint_scratch_bytes\": " << report.checkpoint_scratch_bytes
			   << ",\n  \"checkpoint_payload_hash\": " << report.checkpoint_payload_hash
			   << ",\n  \"checkpoint_publication_mode\": \"" << core::atomic_publish_mode_name(report.checkpoint_publication_mode) << '\"'
			   << ",\n  \"checkpoint_process_gate_status\": " << static_cast<unsigned>(report.checkpoint_process_gate_status)
			   << ",\n  \"checkpoint_system_gate_status\": " << static_cast<unsigned>(report.checkpoint_system_gate_status)
			   << ",\n  \"checkpoint_write_nanoseconds\": " << report.checkpoint_write_nanoseconds
			   << ",\n  \"checkpoint_telemetry\": {\"snapshot_lock_wait_nanoseconds\": " << report.checkpoint_telemetry.snapshot_lock_wait_nanoseconds
			   << ", \"public_catalog_export_nanoseconds\": " << report.checkpoint_telemetry.public_catalog_export_nanoseconds
			   << ", \"compact_storage_export_nanoseconds\": " << report.checkpoint_telemetry.compact_storage_export_nanoseconds
			   << ", \"canonicalization_nanoseconds\": " << report.checkpoint_telemetry.canonicalization_nanoseconds
			   << ", \"validation_nanoseconds\": " << report.checkpoint_telemetry.validation_nanoseconds
			   << ", \"payload_hash_nanoseconds\": " << report.checkpoint_telemetry.payload_hash_nanoseconds
			   << ", \"binary_encoding_write_nanoseconds\": " << report.checkpoint_telemetry.binary_encoding_write_nanoseconds
			   << ", \"stream_close_or_flush_nanoseconds\": " << report.checkpoint_telemetry.stream_close_or_flush_nanoseconds
			   << ", \"atomic_publication_nanoseconds\": " << report.checkpoint_telemetry.atomic_publication_nanoseconds
			   << ", \"atomic_publication_mode\": \"" << core::atomic_publish_mode_name(report.checkpoint_telemetry.atomic_publication_mode) << '\"'
			   << ", \"public_state_bytes\": " << report.checkpoint_telemetry.public_state_bytes
			   << ", \"row_shape_bytes\": " << report.checkpoint_telemetry.row_shape_bytes
			   << ", \"regret_bytes\": " << report.checkpoint_telemetry.regret_bytes
			   << ", \"strategy_sum_bytes\": " << report.checkpoint_telemetry.strategy_sum_bytes
			   << ", \"late_window_baseline_bytes\": " << report.checkpoint_telemetry.late_window_baseline_bytes << "}"
			   << ",\n  \"blueprint_bytes\": " << report.blueprint_bytes
			   << ",\n  \"blueprint_export_nanoseconds\": " << report.blueprint_export_nanoseconds
			   << ",\n  \"capacity_exhaustion_stage\": "
			   << static_cast<unsigned>(report.capacity_exhaustion_stage)
			   << ",\n  \"admitted_rows_per_batch\": [";
		for (std::size_t index = 0U; index < report.admitted_rows_per_batch.size(); ++index)
		{
			if (index != 0U)
				output << ',';
			output << report.admitted_rows_per_batch[index];
		}
		output << "]\n}\n";
		output.close();
		core::publish_atomic_replace(temporary, path, "cannot publish training report");
	}
} // namespace texas::solver::multiway
