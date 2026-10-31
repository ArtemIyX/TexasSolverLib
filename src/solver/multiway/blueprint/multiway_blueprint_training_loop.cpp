#include "solver/multiway/blueprint/multiway_blueprint_trainer.hpp"
#include "solver/multiway/engine/multiway_memory.hpp"
#include "util/tracy.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace texas::solver::multiway
{
	namespace
	{
		bool due(std::uint64_t batch, std::uint64_t interval) noexcept
		{
			return interval != 0U && batch % interval == 0U;
		}
	}

	void MultiwayBlueprintTrainer::record_process_memory_snapshot(
		MultiwayMemoryPhase phase,
		MultiwayMemoryComponent checkpoint_buffers,
		MultiwayMemoryComponent export_buffers)
	{
		if (status_.process_memory_snapshots.enabled && !status_.process_memory_snapshots.record_current(phase))
			throw std::logic_error("multiway training process memory snapshot phase order is invalid");
		if (status_.memory_subsystem_snapshots.enabled)
		{
			auto snapshot = capture_multiway_memory_subsystems(
				buckets_, coordinator_, batch_runner_, continuation_selector_, nullptr,
				checkpoint_buffers, export_buffers);
			if (!status_.memory_subsystem_snapshots.record(phase, snapshot))
				throw std::logic_error("multiway subsystem memory snapshot phase order is invalid");
		}
	}

	void MultiwayBlueprintTrainer::run_batches(
		std::uint64_t batch_count,
		std::uint64_t trajectories_per_batch,
		std::uint64_t seed,
		MultiwayBlueprintBatchProgressCallback progress_callback,
		void* progress_context)
	{
		if (trajectories_per_batch == 0U || seed != deterministic_seed_ || batch_count > (std::numeric_limits<std::uint64_t>::max() - status_.trajectories) / trajectories_per_batch || batch_count > std::numeric_limits<std::uint64_t>::max() - status_.batches || status_.batches > std::numeric_limits<std::uint64_t>::max() - seed || batch_count > std::numeric_limits<std::uint64_t>::max() - seed - status_.batches)
		{
			throw std::invalid_argument("multiway blueprint trainer has an invalid batch request");
		}
		for (std::uint64_t batch = 0; batch < batch_count; ++batch)
		{
			TEXASSOLVER_TRACY_ZONE_SCOPED("mw.trainer.batch");
			const auto system_memory = working_set_monitor_.system_memory_observer == nullptr
				? capture_multiway_system_memory_snapshot()
				: working_set_monitor_.system_memory_observer(working_set_monitor_.system_memory_observer_context);
			const auto system_memory_guard = evaluate_multiway_system_memory_guard(
				system_memory, working_set_monitor_.system_memory_guard);
			status_.system_memory_guard = system_memory_guard;
			if (system_memory_guard.blocked())
			{
				status_.failed = true;
				throw std::runtime_error(
					std::string("multiway blueprint training system memory abort: abort_phase=runtime; rejected_category=") + system_memory_guard.rejected_category + "; message=" + system_memory_guard.message + "; used_physical_bytes=" + std::to_string(system_memory_guard.used_physical_bytes) + "; maximum_used_physical_bytes=" + std::to_string(system_memory_guard.maximum_used_physical_bytes) + "; total_physical_bytes=" + std::to_string(system_memory_guard.total_physical_bytes) + "; available_physical_bytes=" + std::to_string(system_memory_guard.available_physical_bytes) + "; minimum_available_physical_bytes=" + std::to_string(system_memory_guard.minimum_available_physical_bytes));
			}
			const auto pre_batch_memory = working_set_monitor_.observer == nullptr
				? capture_multiway_process_memory_snapshot()
				: working_set_monitor_.observer(working_set_monitor_.observer_context);
			const auto pre_batch_gate = evaluate_multiway_working_set_runtime(
				pre_batch_memory, working_set_monitor_.gate);
			if (pre_batch_gate.blocked())
			{
				status_.working_set_gate = pre_batch_gate;
				throw std::runtime_error(
					std::string("multiway blueprint training working-set abort: abort_phase=runtime; rejected_category=") + pre_batch_gate.rejected_category + "; admission_stage=" + std::to_string(static_cast<unsigned>(pre_batch_gate.admission_stage)));
			}
			const auto one_based_batch = status_.batches + 1U;
			if (status_.late_window_start_batch != 0U && one_based_batch == status_.late_window_start_batch && late_window_baseline_.empty())
			{
				{
					TEXASSOLVER_TRACY_ZONE_SCOPED("mw.trainer.late_window_snapshot");
					late_window_baseline_ = coordinator_->export_root_strategy_sums();
				}
				status_.late_window_active = true;
			}
			batch_runner_->set_batch_number(one_based_batch);
			const auto batch_result = batch_runner_->run(
				status_.trajectories,
				trajectories_per_batch,
				seed + status_.batches,
				schedule_.strategy_weight(one_based_batch));
			if (!batch_result.clean)
			{
				status_.admission_diagnostics.accumulate(batch_result.admission_diagnostics);
				status_.failed_batch_available = true;
				status_.failed_batch = batch_result;
				if (batch_result.capacity_exhausted)
				{
					if (batch_result.regret_delta_capacity_exhausted || batch_result.continuation_delta_capacity_exhausted)
						status_.capacity_exhaustion_stage = MultiwayTrainingCapacityStage::WorkerDeltas;
					else
						status_.capacity_exhaustion_stage = MultiwayTrainingCapacityStage::Discovery;
				}
				else if (batch_result.discovery_capacity_exhausted)
					status_.capacity_exhaustion_stage = MultiwayTrainingCapacityStage::Discovery;
				throw std::runtime_error(
					"multiway blueprint training batch did not complete cleanly: attempted="
					+ std::to_string(batch_result.trajectories_attempted)
					+ " accepted=" + std::to_string(batch_result.trajectories_accepted)
					+ " discarded=" + std::to_string(batch_result.trajectories_discarded)
					+ " delta_entries=" + std::to_string(batch_result.delta_entries_merged)
					+ " discovery_capacity_exhausted="
					+ (batch_result.discovery_capacity_exhausted ? "true" : "false"));
			}
			status_.trajectories += trajectories_per_batch;
			++status_.batches;
			{
				TEXASSOLVER_TRACY_ZONE_SCOPED("mw.trainer.discount_prune");
				const auto trainer_phase_start = std::chrono::steady_clock::now();
				const bool discount_due = schedule_.discount_regrets && due(status_.batches, schedule_.discount_interval_batches);
				const bool pruning_due = schedule_.prune_negative_regrets && status_.batches > schedule_.pruning_warmup_batches && due(status_.batches - schedule_.pruning_warmup_batches, schedule_.pruning_interval_batches);
				if (pruning_due)
				{
					const auto pruning_batch = status_.batches - schedule_.pruning_warmup_batches;
					const bool recovery = schedule_.recovery_interval_batches != 0U && pruning_batch % schedule_.recovery_interval_batches == 0U;
					if (!recovery && discount_due)
					{
						status_.pruned_negative_regrets += coordinator_->scale_and_prune_regrets(
							schedule_.regret_discount_factor, schedule_.pruning_threshold, schedule_.regret_floor);
					}
					else if (!recovery)
					{
						status_.pruned_negative_regrets += coordinator_->prune_negative_regrets(
							schedule_.pruning_threshold, schedule_.regret_floor);
					}
					else if (discount_due)
					{
						coordinator_->scale_regrets(schedule_.regret_discount_factor);
					}
				}
				else if (discount_due)
				{
					coordinator_->scale_regrets(schedule_.regret_discount_factor);
				}
				status_.trainer_only_nanoseconds += static_cast<std::uint64_t>(
					std::chrono::duration_cast<std::chrono::nanoseconds>(
						std::chrono::steady_clock::now() - trainer_phase_start)
						.count());
			}
			const auto& diagnostics = coordinator_->diagnostics();
			status_.visited_public_descriptors = diagnostics.public_states_admitted;
			status_.admitted_rows = diagnostics.sparse_rows_admitted;
			status_.preflop_rows = diagnostics.preflop_rows_admitted;
			status_.flop_rows = diagnostics.flop_rows_admitted;
			status_.turn_rows = diagnostics.turn_rows_admitted;
			status_.river_rows = diagnostics.river_rows_admitted;
			status_.admitted_action_cells = coordinator_->compact_storage() != nullptr
				? coordinator_->compact_storage()->value_count()
				: coordinator_->storage().value_count();
			status_.terminal_visits = diagnostics.terminal_visits;
			status_.leaf_visits = diagnostics.leaf_visits;
			status_.street_visits = diagnostics.street_visits;
			status_.missing_lookup_requests = diagnostics.missing_lookup_requests;
			status_.discarded_trajectories = diagnostics.trajectories_discarded;
			status_.merged_stream_fingerprint = diagnostics.last_merged_stream_fingerprint;
			status_.schedule_fingerprint = batch_result.run.schedule_fingerprint;
			status_.peak_public_states = std::max(status_.peak_public_states,
				diagnostics.public_states_admitted);
			status_.peak_sparse_rows = std::max(status_.peak_sparse_rows,
				diagnostics.sparse_rows_admitted);
			status_.peak_sparse_values = std::max(status_.peak_sparse_values,
				static_cast<std::uint64_t>(coordinator_->compact_storage() != nullptr
						? coordinator_->compact_storage()->value_count()
						: coordinator_->storage().value_count()));
			status_.peak_worker_delta_entries = std::max(status_.peak_worker_delta_entries,
				batch_result.maximum_worker_delta_entries);
			status_.total_peak_regret_delta_entries = std::max(
				status_.total_peak_regret_delta_entries, batch_result.total_peak_regret_delta_entries);
			status_.total_peak_continuation_delta_entries = std::max(
				status_.total_peak_continuation_delta_entries, batch_result.total_peak_continuation_delta_entries);
			status_.total_peak_delta_entries = std::max(
				status_.total_peak_delta_entries, batch_result.total_peak_delta_entries);
			status_.generated_update_count += batch_result.generated_update_count;
			status_.generated_discovery_public_state_count += batch_result.generated_discovery_public_state_count;
			status_.generated_discovery_row_count += batch_result.generated_discovery_row_count;
			status_.coordinator_admission_nanoseconds += batch_result.coordinator_admission_nanoseconds;
			status_.memory_ledger.coordinator_allocation_count += batch_result.coordinator_allocation_count;
			status_.memory_ledger.coordinator_allocation_bytes += batch_result.coordinator_allocated_bytes;
			status_.admission_diagnostics.accumulate(batch_result.admission_diagnostics);
			status_.placement_policy = batch_result.placement_policy;
			status_.affinity_requested = batch_result.affinity_requested;
			status_.cumulative_worker_delta_entries = diagnostics.worker_delta_entries_merged;
			status_.worker_active_nanoseconds += batch_result.worker_active_nanoseconds;
			status_.worker_wake_latency_nanoseconds += batch_result.worker_wake_latency_nanoseconds;
			status_.worker_cv_wait_nanoseconds += batch_result.worker_cv_wait_nanoseconds;
			status_.worker_scheduler_claim_nanoseconds += batch_result.worker_scheduler_claim_nanoseconds;
			status_.worker_completion_nanoseconds += batch_result.worker_completion_nanoseconds;
			status_.worker_tail_idle_nanoseconds += batch_result.worker_tail_idle_nanoseconds;
			status_.minimum_worker_active_nanoseconds = status_.batches == 1U
				? batch_result.minimum_worker_active_nanoseconds
				: std::min(status_.minimum_worker_active_nanoseconds,
					  batch_result.minimum_worker_active_nanoseconds);
			status_.maximum_worker_active_nanoseconds = std::max(
				status_.maximum_worker_active_nanoseconds, batch_result.maximum_worker_active_nanoseconds);
			status_.coordinator_wait_nanoseconds += batch_result.coordinator_wait_nanoseconds;
			status_.barrier_wait_nanoseconds += batch_result.barrier_wait_nanoseconds;
			status_.coordinator_lock_wait_nanoseconds += batch_result.coordinator_lock_wait_nanoseconds;
			status_.delta_sort_nanoseconds += batch_result.delta_sort_nanoseconds;
			status_.merge_nanoseconds += batch_result.merge_nanoseconds;
			if (status_.worker_trajectory_counts.size() != batch_result.worker_trajectory_counts.size())
			{
				status_.worker_trajectory_counts.assign(batch_result.worker_trajectory_counts.size(), 0U);
				status_.worker_active_nanoseconds_by_worker.assign(
					batch_result.worker_active_nanoseconds_by_worker.size(), 0U);
				status_.worker_sort_nanoseconds_by_worker.assign(batch_result.worker_sort_nanoseconds_by_worker.size(), 0U);
				status_.worker_wake_latency_nanoseconds_by_worker.assign(batch_result.worker_wake_latency_nanoseconds_by_worker.size(), 0U);
				status_.worker_cv_wait_nanoseconds_by_worker.assign(batch_result.worker_cv_wait_nanoseconds_by_worker.size(), 0U);
				status_.worker_scheduler_claim_nanoseconds_by_worker.assign(batch_result.worker_scheduler_claim_nanoseconds_by_worker.size(), 0U);
				status_.worker_completion_nanoseconds_by_worker.assign(batch_result.worker_completion_nanoseconds_by_worker.size(), 0U);
				status_.worker_tail_idle_nanoseconds_by_worker.assign(batch_result.worker_tail_idle_nanoseconds_by_worker.size(), 0U);
				status_.worker_regret_delta_entries.assign(batch_result.worker_regret_delta_entries.size(), 0U);
				status_.worker_continuation_delta_entries.assign(batch_result.worker_continuation_delta_entries.size(), 0U);
				status_.worker_discovery_public_state_counts.assign(batch_result.worker_discovery_public_state_counts.size(), 0U);
				status_.worker_discovery_row_counts.assign(batch_result.worker_discovery_row_counts.size(), 0U);
				status_.worker_allocation_counts.assign(batch_result.worker_allocation_counts.size(), 0U);
				status_.worker_allocated_bytes.assign(batch_result.worker_allocated_bytes.size(), 0U);
				status_.worker_requested_processor.assign(batch_result.worker_requested_processor.size(), 0xffffffffU);
				status_.worker_resolved_processor.assign(batch_result.worker_resolved_processor.size(), 0U);
				status_.worker_requested_cpu_set.assign(batch_result.worker_requested_cpu_set.size(), kMultiwayUnknownTopologyId);
				status_.worker_resolved_cpu_set.assign(batch_result.worker_resolved_cpu_set.size(), kMultiwayUnknownTopologyId);
				status_.worker_requested_physical_core.assign(batch_result.worker_requested_physical_core.size(), kMultiwayUnknownTopologyId);
				status_.worker_resolved_physical_core.assign(batch_result.worker_resolved_physical_core.size(), kMultiwayUnknownTopologyId);
				status_.worker_requested_smt_sibling.assign(batch_result.worker_requested_smt_sibling.size(), kMultiwayUnknownTopologyIndex);
				status_.worker_resolved_smt_sibling.assign(batch_result.worker_resolved_smt_sibling.size(), kMultiwayUnknownTopologyIndex);
				status_.worker_requested_cache_domain.assign(batch_result.worker_requested_cache_domain.size(), kMultiwayUnknownTopologyIndex);
				status_.worker_resolved_cache_domain.assign(batch_result.worker_resolved_cache_domain.size(), kMultiwayUnknownTopologyIndex);
				status_.worker_migration_sampled.assign(batch_result.worker_migration_sampled.size(), 0U);
				status_.worker_migration_detected.assign(batch_result.worker_migration_detected.size(), 0U);
				status_.worker_affinity_masks.assign(batch_result.worker_affinity_masks.size(), 0U);
				status_.worker_affinity_succeeded.assign(batch_result.worker_affinity_succeeded.size(), 0U);
				status_.worker_profiles.assign(batch_result.worker_profiles.size(), MultiwaySearchProfileSnapshot{});
			}
			for (std::size_t worker_index = 0U;
				worker_index < batch_result.worker_trajectory_counts.size(); ++worker_index)
			{
				status_.worker_trajectory_counts[worker_index] +=
					batch_result.worker_trajectory_counts[worker_index];
				status_.worker_active_nanoseconds_by_worker[worker_index] +=
					batch_result.worker_active_nanoseconds_by_worker[worker_index];
				status_.worker_sort_nanoseconds_by_worker[worker_index] +=
					batch_result.worker_sort_nanoseconds_by_worker[worker_index];
				status_.worker_wake_latency_nanoseconds_by_worker[worker_index] +=
					batch_result.worker_wake_latency_nanoseconds_by_worker[worker_index];
				status_.worker_cv_wait_nanoseconds_by_worker[worker_index] +=
					batch_result.worker_cv_wait_nanoseconds_by_worker[worker_index];
				status_.worker_scheduler_claim_nanoseconds_by_worker[worker_index] +=
					batch_result.worker_scheduler_claim_nanoseconds_by_worker[worker_index];
				status_.worker_completion_nanoseconds_by_worker[worker_index] +=
					batch_result.worker_completion_nanoseconds_by_worker[worker_index];
				status_.worker_tail_idle_nanoseconds_by_worker[worker_index] +=
					batch_result.worker_tail_idle_nanoseconds_by_worker[worker_index];
				status_.worker_regret_delta_entries[worker_index] = std::max(
					status_.worker_regret_delta_entries[worker_index], batch_result.worker_regret_delta_entries[worker_index]);
				status_.worker_continuation_delta_entries[worker_index] = std::max(
					status_.worker_continuation_delta_entries[worker_index], batch_result.worker_continuation_delta_entries[worker_index]);
				status_.worker_discovery_public_state_counts[worker_index] = std::max(
					status_.worker_discovery_public_state_counts[worker_index], batch_result.worker_discovery_public_state_counts[worker_index]);
				status_.worker_discovery_row_counts[worker_index] = std::max(
					status_.worker_discovery_row_counts[worker_index], batch_result.worker_discovery_row_counts[worker_index]);
				status_.worker_allocation_counts[worker_index] += batch_result.worker_allocation_counts[worker_index];
				status_.worker_allocated_bytes[worker_index] += batch_result.worker_allocated_bytes[worker_index];
				status_.memory_ledger.worker_allocation_count += batch_result.worker_allocation_counts[worker_index];
				status_.memory_ledger.worker_allocation_bytes += batch_result.worker_allocated_bytes[worker_index];
				status_.worker_resolved_processor[worker_index] = batch_result.worker_resolved_processor[worker_index];
				status_.worker_requested_processor[worker_index] = batch_result.worker_requested_processor[worker_index];
				status_.worker_affinity_masks[worker_index] = batch_result.worker_affinity_masks[worker_index];
				status_.worker_affinity_succeeded[worker_index] = batch_result.worker_affinity_succeeded[worker_index];
				status_.worker_requested_cpu_set[worker_index] = batch_result.worker_requested_cpu_set[worker_index];
				status_.worker_resolved_cpu_set[worker_index] = batch_result.worker_resolved_cpu_set[worker_index];
				status_.worker_requested_physical_core[worker_index] = batch_result.worker_requested_physical_core[worker_index];
				status_.worker_resolved_physical_core[worker_index] = batch_result.worker_resolved_physical_core[worker_index];
				status_.worker_requested_smt_sibling[worker_index] = batch_result.worker_requested_smt_sibling[worker_index];
				status_.worker_resolved_smt_sibling[worker_index] = batch_result.worker_resolved_smt_sibling[worker_index];
				status_.worker_requested_cache_domain[worker_index] = batch_result.worker_requested_cache_domain[worker_index];
				status_.worker_resolved_cache_domain[worker_index] = batch_result.worker_resolved_cache_domain[worker_index];
				status_.worker_migration_sampled[worker_index] = std::max(
					status_.worker_migration_sampled[worker_index], batch_result.worker_migration_sampled[worker_index]);
				status_.worker_migration_detected[worker_index] = std::max(
					status_.worker_migration_detected[worker_index], batch_result.worker_migration_detected[worker_index]);
				if (batch_result.worker_profiles[worker_index].profiled())
				{
					status_.worker_profiles[worker_index].mode = batch_result.worker_profiles[worker_index].mode;
					for (std::size_t stage = 0U; stage < status_.worker_profiles[worker_index].checkpoints.size(); ++stage)
					{
						status_.worker_profiles[worker_index].checkpoints[stage].elapsed_nanoseconds +=
							batch_result.worker_profiles[worker_index].checkpoints[stage].elapsed_nanoseconds;
						status_.worker_profiles[worker_index].checkpoints[stage].calls +=
							batch_result.worker_profiles[worker_index].checkpoints[stage].calls;
					}
				}
			}
			status_.minimum_worker_trajectories = status_.batches == 1U
				? batch_result.minimum_worker_trajectories
				: std::min(status_.minimum_worker_trajectories, batch_result.minimum_worker_trajectories);
			status_.maximum_worker_trajectories = std::max(
				status_.maximum_worker_trajectories, batch_result.maximum_worker_trajectories);
			status_.peak_compact_storage_bytes = std::max(status_.peak_compact_storage_bytes,
				static_cast<std::uint64_t>(coordinator_->compact_storage() != nullptr
						? coordinator_->compact_storage()->memory_bytes()
						: 0U));
			{
				TEXASSOLVER_TRACY_ZONE_SCOPED("mw.telemetry.process_sample");
				const auto process_memory = capture_multiway_process_memory_snapshot();
				const auto runtime_gate = evaluate_multiway_working_set_runtime(
					working_set_monitor_.observer == nullptr
						? process_memory
						: working_set_monitor_.observer(working_set_monitor_.observer_context),
					working_set_monitor_.gate);
				status_.working_set_gate = runtime_gate;
				if (runtime_gate.blocked())
				{
					throw std::runtime_error(
						std::string("multiway blueprint training working-set abort: abort_phase=runtime; rejected_category=") + runtime_gate.rejected_category + "; admission_stage=" + std::to_string(static_cast<unsigned>(runtime_gate.admission_stage)));
				}
				status_.current_process_rss_bytes = process_memory.current_working_set_available
					? process_memory.current_working_set_bytes
					: 0U;
				status_.process_rss_available = process_memory.current_working_set_available;
				status_.peak_process_rss_bytes = std::max(status_.peak_process_rss_bytes,
					status_.current_process_rss_bytes);
				const auto process_cpu_now = observed_multiway_training_process_cpu_nanoseconds();
				status_.process_cpu_nanoseconds = process_cpu_now >= process_cpu_started_nanoseconds_
					? process_cpu_now - process_cpu_started_nanoseconds_
					: 0U;
				status_.peak_working_set_bytes = std::max(status_.peak_working_set_bytes,
					process_memory.peak_working_set_available ? process_memory.peak_working_set_bytes : 0U);
				if (status_.process_memory_snapshots.enabled && status_.process_memory_snapshots.count == static_cast<std::uint8_t>(MultiwayMemoryPhase::AfterColdBatch))
				{
					if (!status_.process_memory_snapshots.record(MultiwayMemoryPhase::AfterColdBatch, process_memory))
						throw std::logic_error("multiway training memory snapshot phase order is invalid");
				}
				if (status_.memory_subsystem_snapshots.enabled && status_.memory_subsystem_snapshots.count == static_cast<std::uint8_t>(MultiwayMemoryPhase::AfterColdBatch))
				{
					if (!status_.memory_subsystem_snapshots.record(
							MultiwayMemoryPhase::AfterColdBatch,
							capture_multiway_memory_subsystems(buckets_, coordinator_, batch_runner_, continuation_selector_)))
						throw std::logic_error("multiway subsystem memory snapshot phase order is invalid");
				}
				if (status_.process_memory_snapshots.enabled && status_.process_memory_snapshots.count == static_cast<std::uint8_t>(MultiwayMemoryPhase::AfterSteadyBatch) && coordinator_->limits().max_batches != 0U && status_.batches >= coordinator_->limits().max_batches)
				{
					if (!status_.process_memory_snapshots.record(MultiwayMemoryPhase::AfterSteadyBatch, process_memory))
						throw std::logic_error("multiway training memory snapshot phase order is invalid");
				}
				if (status_.memory_subsystem_snapshots.enabled && status_.memory_subsystem_snapshots.count == static_cast<std::uint8_t>(MultiwayMemoryPhase::AfterSteadyBatch) && coordinator_->limits().max_batches != 0U && status_.batches >= coordinator_->limits().max_batches)
				{
					if (!status_.memory_subsystem_snapshots.record(
							MultiwayMemoryPhase::AfterSteadyBatch,
							capture_multiway_memory_subsystems(buckets_, coordinator_, batch_runner_, continuation_selector_)))
						throw std::logic_error("multiway subsystem memory snapshot phase order is invalid");
				}
			}
			status_.elapsed_wall_nanoseconds = static_cast<std::uint64_t>(
				std::chrono::duration_cast<std::chrono::nanoseconds>(
					std::chrono::steady_clock::now() - started_at_)
					.count());
			if (status_.admitted_rows_per_batch.size() < 256U)
			{
				status_.admitted_rows_per_batch.push_back(status_.admitted_rows);
			}
			else
			{
				status_.admitted_rows_per_batch.back() = status_.admitted_rows;
			}
			if (status_.configured_max_public_states != 0U && status_.peak_public_states >= status_.configured_max_public_states)
			{
				status_.capacity_exhaustion_stage = MultiwayTrainingCapacityStage::PublicStates;
			}
			else if (status_.configured_max_sparse_rows != 0U && status_.peak_sparse_rows >= status_.configured_max_sparse_rows)
			{
				status_.capacity_exhaustion_stage = MultiwayTrainingCapacityStage::SparseRows;
			}
			else if (status_.configured_max_sparse_values != 0U && status_.peak_sparse_values >= status_.configured_max_sparse_values)
			{
				status_.capacity_exhaustion_stage = MultiwayTrainingCapacityStage::SparseValues;
			}
			else if (status_.configured_worker_delta_capacity != 0U && status_.peak_worker_delta_entries >= status_.configured_worker_delta_capacity)
			{
				status_.capacity_exhaustion_stage = MultiwayTrainingCapacityStage::WorkerDeltas;
			}
			TEXASSOLVER_TRACY_FRAME_MARK("mw.batch");
			TEXASSOLVER_TRACY_PLOT("mw.trajectories.accepted",
				status_.trajectories >= status_.discarded_trajectories
					? status_.trajectories - status_.discarded_trajectories
					: 0U);
			TEXASSOLVER_TRACY_PLOT("mw.worker.active_ns.total", status_.worker_active_nanoseconds);
			TEXASSOLVER_TRACY_PLOT("mw.worker.active_ns.max", status_.maximum_worker_active_nanoseconds);
			TEXASSOLVER_TRACY_PLOT("mw.worker.tail_idle_ns.total", status_.worker_tail_idle_nanoseconds);
			TEXASSOLVER_TRACY_PLOT("mw.delta.regret_entries", status_.total_peak_regret_delta_entries);
			TEXASSOLVER_TRACY_PLOT("mw.delta.continuation_entries", status_.total_peak_continuation_delta_entries);
			TEXASSOLVER_TRACY_PLOT("mw.discovery.public_states", status_.peak_public_states);
			TEXASSOLVER_TRACY_PLOT("mw.discovery.rows", status_.peak_sparse_rows);
			TEXASSOLVER_TRACY_PLOT("mw.phase.admission_ns", status_.coordinator_admission_nanoseconds);
			TEXASSOLVER_TRACY_PLOT("mw.phase.merge_ns", status_.merge_nanoseconds);
			TEXASSOLVER_TRACY_PLOT("mw.phase.trainer_only_ns", status_.trainer_only_nanoseconds);
			TEXASSOLVER_TRACY_PLOT("mw.memory.rss_bytes", status_.current_process_rss_bytes);
			TEXASSOLVER_TRACY_PLOT("mw.memory.current_working_set_bytes", status_.current_process_rss_bytes);
			TEXASSOLVER_TRACY_PLOT("mw.memory.private_committed_bytes",
				status_.process_memory_snapshots.count == 0U || !status_.process_memory_snapshots.snapshots[status_.process_memory_snapshots.count - 1U].private_committed_available
					? 0U
					: status_.process_memory_snapshots.snapshots[status_.process_memory_snapshots.count - 1U].private_committed_bytes);
			TEXASSOLVER_TRACY_PLOT("mw.memory.peak_working_set_bytes", status_.peak_working_set_bytes);
			TEXASSOLVER_TRACY_PLOT("mw.process.cpu_ns.delta", status_.process_cpu_nanoseconds);
			if (progress_callback != nullptr)
				progress_callback(status_, progress_context);
		}
	}

	void MultiwayBlueprintTrainingSession::run_batches(
		std::uint64_t batch_count,
		MultiwayBlueprintBatchProgressCallback progress_callback,
		void* progress_context)
	{
		trainer_->run_batches(
			batch_count, config_.limits.trajectories_per_batch, config_.deterministic_seed,
			progress_callback, progress_context);
	}
}
