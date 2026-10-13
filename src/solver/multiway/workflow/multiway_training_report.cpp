#include "solver/multiway/workflow/multiway_training_report.hpp"

#include "core/atomic_publish.hpp"

#include <fstream>
#include <cmath>
#include <stdexcept>

namespace texas::solver::multiway
{

	void MultiwayTrainingReport::validate() const
	{
		const auto worker_vectors_match = [this] {
			const auto worker_count = worker_trajectory_counts.size();
			const auto matches = [worker_count](std::size_t size) {
				return size == 0U || size == worker_count;
			};
			return matches(worker_active_nanoseconds_by_worker.size()) &&
				matches(worker_sort_nanoseconds_by_worker.size()) &&
				matches(worker_wake_latency_nanoseconds_by_worker.size()) &&
				matches(worker_completion_nanoseconds_by_worker.size()) &&
				matches(worker_tail_idle_nanoseconds_by_worker.size()) &&
				matches(worker_regret_delta_entries.size()) &&
				matches(worker_continuation_delta_entries.size()) &&
				matches(worker_discovery_public_state_counts.size()) &&
				matches(worker_discovery_row_counts.size()) &&
				matches(worker_profiles.size());
		};
		if (accepted_trajectories > trajectories || discarded_trajectories > trajectories || (batches != 0U && deterministic_merge_fingerprint == 0U && !failed))
		{
			throw std::invalid_argument("invalid multiway training report");
		}
		evidence.validate(MULTIWAY_TRAINING_REPORT_SCHEMA_VERSION);
		if (process_rss_available && current_process_rss_bytes == 0U)
		{
			throw std::invalid_argument("available process RSS must be non-zero");
		}
		if (peak_process_rss_bytes < current_process_rss_bytes || requested_worker_count > 16U || effective_worker_count > requested_worker_count || ((requested_worker_count != 0U || effective_worker_count != 0U) && (requested_worker_count == 0U || effective_worker_count == 0U || trajectories_per_batch == 0U)) || minimum_worker_trajectories > maximum_worker_trajectories || minimum_worker_active_nanoseconds > maximum_worker_active_nanoseconds || barrier_wait_nanoseconds > coordinator_wait_nanoseconds || !worker_vectors_match() || (batches != 0U && effective_worker_count != 0U && worker_trajectory_counts.size() != effective_worker_count) || !std::isfinite(trajectories_per_second()) || static_cast<unsigned>(capacity_exhaustion_stage) > static_cast<unsigned>(MultiwayTrainingCapacityStage::Discovery))
		{
			throw std::invalid_argument("invalid multiway training telemetry");
		}
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
		if (elapsed_wall_nanoseconds == 0U)
			return 0.0;
		return static_cast<double>(trajectories) * 1'000'000'000.0 / static_cast<double>(elapsed_wall_nanoseconds);
	}

	MultiwayTrainingReport make_multiway_training_report(
		const MultiwayBlueprintTrainingStatus& status, std::uint64_t fingerprint,
		MultiwayEvidenceHeader evidence, std::uint64_t process_memory_limit_bytes) noexcept
	{
		MultiwayTrainingReport report;
		report.batches = status.batches;
		report.trajectories = status.trajectories;
		report.discarded_trajectories = status.discarded_trajectories;
		report.accepted_trajectories = status.trajectories >= status.discarded_trajectories
			? status.trajectories - status.discarded_trajectories
			: 0U;
		report.preflop_rows = status.preflop_rows;
		report.flop_rows = status.flop_rows;
		report.turn_rows = status.turn_rows;
		report.river_rows = status.river_rows;
		report.terminal_visits = status.terminal_visits;
		report.leaf_visits = status.leaf_visits;
		report.street_visits = status.street_visits;
		report.deterministic_merge_fingerprint = fingerprint;
		report.evidence = evidence;
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
		report.peak_compact_storage_bytes = status.peak_compact_storage_bytes;
		report.configured_max_public_states = status.configured_max_public_states;
		report.configured_max_sparse_rows = status.configured_max_sparse_rows;
		report.configured_max_sparse_values = status.configured_max_sparse_values;
		report.configured_worker_delta_capacity = status.configured_worker_delta_capacity;
		report.cumulative_worker_delta_entries = status.cumulative_worker_delta_entries;
		report.worker_active_nanoseconds = status.worker_active_nanoseconds;
		report.worker_wake_latency_nanoseconds = status.worker_wake_latency_nanoseconds;
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
		report.worker_completion_nanoseconds_by_worker = status.worker_completion_nanoseconds_by_worker;
		report.worker_tail_idle_nanoseconds_by_worker = status.worker_tail_idle_nanoseconds_by_worker;
		report.worker_regret_delta_entries = status.worker_regret_delta_entries;
		report.worker_continuation_delta_entries = status.worker_continuation_delta_entries;
		report.worker_discovery_public_state_counts = status.worker_discovery_public_state_counts;
		report.worker_discovery_row_counts = status.worker_discovery_row_counts;
		report.worker_profiles = status.worker_profiles;
		report.memory_preflight_estimate_bytes = status.memory_preflight_estimate_bytes;
		report.memory_ledger = status.memory_ledger;
		report.capacity_exhaustion_stage = status.capacity_exhaustion_stage;
		report.admitted_rows_per_batch = status.admitted_rows_per_batch;
		report.current_process_rss_bytes = status.current_process_rss_bytes;
		report.peak_process_rss_bytes = status.peak_process_rss_bytes;
		report.process_memory_limit_bytes = process_memory_limit_bytes;
		report.process_rss_available = status.process_rss_available;
		report.started_at_preflop = status.preflop_rows > 0U;
		report.elapsed_wall_nanoseconds = status.elapsed_wall_nanoseconds;
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
		output << "{\n  \"evidence\": " << serialize_multiway_evidence_header(report.evidence)
			   << ",\n  \"batches\": " << report.batches
			   << ",\n  \"trajectories\": " << report.trajectories
			   << ",\n  \"accepted_trajectories\": " << report.accepted_trajectories
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
			   << ",\n  \"elapsed_wall_nanoseconds\": " << report.elapsed_wall_nanoseconds
			   << ",\n  \"trajectories_per_second\": " << report.trajectories_per_second()
			   << ",\n  \"current_process_rss_bytes\": " << report.current_process_rss_bytes
			   << ",\n  \"peak_process_rss_bytes\": " << report.peak_process_rss_bytes
			   << ",\n  \"process_memory_limit_bytes\": " << report.process_memory_limit_bytes
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
			   << ",\n  \"peak_compact_storage_bytes\": " << report.peak_compact_storage_bytes
			   << ",\n  \"configured_max_public_states\": " << report.configured_max_public_states
			   << ",\n  \"configured_max_sparse_rows\": " << report.configured_max_sparse_rows
			   << ",\n  \"configured_max_sparse_values\": " << report.configured_max_sparse_values
			   << ",\n  \"configured_worker_delta_capacity\": " << report.configured_worker_delta_capacity
			   << ",\n  \"cumulative_worker_delta_entries\": " << report.cumulative_worker_delta_entries
			   << ",\n  \"worker_active_nanoseconds\": " << report.worker_active_nanoseconds
			   << ",\n  \"worker_wake_latency_nanoseconds\": " << report.worker_wake_latency_nanoseconds
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
		write_u64_array("worker_completion_nanoseconds_by_worker", report.worker_completion_nanoseconds_by_worker);
		write_u64_array("worker_tail_idle_nanoseconds_by_worker", report.worker_tail_idle_nanoseconds_by_worker);
		write_u64_array("worker_regret_delta_entries", report.worker_regret_delta_entries);
		write_u64_array("worker_continuation_delta_entries", report.worker_continuation_delta_entries);
		write_u64_array("worker_discovery_public_state_counts", report.worker_discovery_public_state_counts);
		write_u64_array("worker_discovery_row_counts", report.worker_discovery_row_counts);
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
			   << ",\n  \"memory_ledger\": {\"resident_baseline_bytes\": " << report.memory_ledger.resident_baseline_bytes
			   << ", \"bucket_registry_bytes\": " << report.memory_ledger.bucket_registry_bytes
			   << ", \"mapped_bucket_artifact_bytes\": " << report.memory_ledger.mapped_bucket_artifact_bytes
			   << ", \"public_catalog_bytes\": " << report.memory_ledger.public_catalog_bytes
			   << ", \"row_metadata_bytes\": " << report.memory_ledger.row_metadata_bytes
			   << ", \"regret_page_bytes\": " << report.memory_ledger.regret_page_bytes
			   << ", \"worker_stream_bytes\": " << report.memory_ledger.worker_stream_bytes
			   << ", \"merge_scratch_bytes\": " << report.memory_ledger.merge_scratch_bytes
			   << ", \"checkpoint_peak_bytes\": " << report.memory_ledger.checkpoint_peak_bytes
			   << ", \"export_peak_bytes\": " << report.memory_ledger.export_peak_bytes
			   << ", \"allocator_overhead_bytes\": " << report.memory_ledger.allocator_overhead_bytes
			   << ", \"live_bytes\": " << report.memory_ledger.live_bytes
			   << ", \"reserved_bytes\": " << report.memory_ledger.reserved_bytes
			   << ", \"committed_bytes\": " << report.memory_ledger.committed_bytes << "}"
			   << ",\n  \"checkpoint_bytes\": " << report.checkpoint_bytes
			   << ",\n  \"checkpoint_write_nanoseconds\": " << report.checkpoint_write_nanoseconds
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
