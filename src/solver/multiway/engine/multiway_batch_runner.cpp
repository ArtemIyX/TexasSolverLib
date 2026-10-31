#include "solver/multiway/engine/multiway_traversal.hpp"
#include "solver/multiway/engine/multiway_memory.hpp"
#include "util/profiling.hpp"
#include "util/tracy.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <limits>
#include <memory>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <thread>
#include <utility>

#if defined(_WIN32)
	#ifndef NOMINMAX
		#define NOMINMAX
	#endif
	#include <windows.h>
#elif defined(__linux__)
	#include <sched.h>
#endif

namespace texas::solver::multiway
{
	namespace
	{
		constexpr std::uint32_t kInvalidProcessor = 0xffffffffU;

		[[nodiscard]] std::uint32_t current_processor_id() noexcept
		{
#if defined(_WIN32)
			PROCESSOR_NUMBER processor{};
			::GetCurrentProcessorNumberEx(&processor);
			return static_cast<std::uint32_t>(processor.Group) * (sizeof(KAFFINITY) * 8U) + processor.Number;
#elif defined(__linux__)
			const auto processor = ::sched_getcpu();
			return processor < 0 ? kInvalidProcessor : static_cast<std::uint32_t>(processor);
#else
			return kInvalidProcessor;
#endif
		}

		[[nodiscard]] std::uint32_t current_logical_processor_identity() noexcept
		{
#if defined(_WIN32)
			PROCESSOR_NUMBER processor{};
			::GetCurrentProcessorNumberEx(&processor);
			return (static_cast<std::uint32_t>(processor.Group) << 16U) | static_cast<std::uint32_t>(processor.Number);
#elif defined(__linux__)
			const auto processor = ::sched_getcpu();
			return processor < 0 ? kInvalidProcessor : static_cast<std::uint32_t>(processor);
#else
			return kInvalidProcessor;
#endif
		}

		[[nodiscard]] const MultiwayWorkerTopologyRecord* find_current_topology_record(
			const MultiwayWorkerTopologyInventory& inventory, std::uint32_t processor) noexcept
		{
			if (processor == kInvalidProcessor)
				return nullptr;
			return inventory.find(
				static_cast<std::uint16_t>(processor >> 16U),
				static_cast<std::uint16_t>(processor & 0xffffU));
		}

		[[nodiscard]] std::uint64_t steady_now_nanoseconds() noexcept
		{
			return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
				std::chrono::steady_clock::now().time_since_epoch())
					.count());
		}

		class MultiwayAdmissionDiagnosticsScope final
		{
		public:
			MultiwayAdmissionDiagnosticsScope(
				MultiwaySolverCoordinator& coordinator,
				MultiwayAdmissionDiagnostics& diagnostics) noexcept
				: coordinator_(coordinator)
			{
				coordinator_.set_admission_diagnostics(&diagnostics);
			}

			~MultiwayAdmissionDiagnosticsScope()
			{
				coordinator_.set_admission_diagnostics(nullptr);
			}

			MultiwayAdmissionDiagnosticsScope(const MultiwayAdmissionDiagnosticsScope&) = delete;
			MultiwayAdmissionDiagnosticsScope& operator=(const MultiwayAdmissionDiagnosticsScope&) = delete;

		private:
			MultiwaySolverCoordinator& coordinator_;
		};
	} // namespace

#pragma pack(push, 8)
	struct MultiwayRootBatchRunner::BatchRunContext
	{
		std::uint64_t first_trajectory_id = 0U;
		std::uint64_t trajectory_count = 0U;
		std::uint64_t seed = 0U;
		double iteration_weight = 1.0;
		std::chrono::steady_clock::time_point deadline = std::chrono::steady_clock::time_point::max();
		std::size_t batch_count = 0U;
		std::chrono::steady_clock::time_point coordinator_wait_start{};
		std::uint64_t coordinator_lock_wait_before = 0U;
		std::uint64_t terminal_visits = 0U;
		std::uint64_t leaf_visits = 0U;
		std::uint64_t missing_lookup_requests = 0U;
		MultiwayRootBatchResult result;
		std::optional<MultiwaySearchProfile> batch_profile;
		std::optional<MultiwayAllocationProbe> coordinator_allocation_probe;
	};
#pragma pack(pop)

	MultiwayRootBatchRunner::MultiwayRootBatchRunner(
		MultiwayRootExternalSamplingTraversal traversal,
		MultiwaySolverCoordinator& coordinator,
		std::uint32_t worker_count,
		std::size_t worker_delta_capacity,
		MultiwaySearchProfileMode profile_mode,
		bool pin_workers, bool immutable_epochs, MultiwaySchedulerMode scheduler_mode,
		std::uint32_t dynamic_chunk_size)
		: MultiwayRootBatchRunner(
			  std::move(traversal), coordinator, worker_count,
			  { worker_delta_capacity, worker_delta_capacity, 0U, 0U },
			  profile_mode,
			  MultiwayWorkerPlacementConfig{
				  pin_workers ? MultiwayWorkerPlacementPolicy::DiagnosticHardAffinity
							  : MultiwayWorkerPlacementPolicy::OsDefault,
				  0U },
			  immutable_epochs, scheduler_mode, dynamic_chunk_size)
	{
	}

	MultiwayRootBatchRunner::MultiwayRootBatchRunner(
		MultiwayRootExternalSamplingTraversal traversal,
		MultiwaySolverCoordinator& coordinator,
		std::uint32_t worker_count,
		MultiwayWorkerStreamCapacities capacities,
		MultiwaySearchProfileMode profile_mode,
		bool pin_workers, bool immutable_epochs, MultiwaySchedulerMode scheduler_mode,
		std::uint32_t dynamic_chunk_size)
		: MultiwayRootBatchRunner(
			  std::move(traversal), coordinator, worker_count, capacities, profile_mode,
			  MultiwayWorkerPlacementConfig{
				  pin_workers ? MultiwayWorkerPlacementPolicy::DiagnosticHardAffinity
							  : MultiwayWorkerPlacementPolicy::OsDefault,
				  0U },
			  immutable_epochs, scheduler_mode, dynamic_chunk_size)
	{
	}

	MultiwayRootBatchRunner::MultiwayRootBatchRunner(
		MultiwayRootExternalSamplingTraversal traversal,
		MultiwaySolverCoordinator& coordinator,
		std::uint32_t worker_count,
		MultiwayWorkerStreamCapacities capacities,
		MultiwaySearchProfileMode profile_mode,
		MultiwayWorkerPlacementConfig placement,
		bool immutable_epochs, MultiwaySchedulerMode scheduler_mode,
		std::uint32_t dynamic_chunk_size)
		: traversal_(std::move(traversal)), coordinator_(&coordinator), worker_count_(worker_count), stream_capacities_(capacities), profile_mode_(profile_mode), placement_config_(placement), immutable_epochs_(immutable_epochs), scheduler_mode_(scheduler_mode), dynamic_chunk_size_(dynamic_chunk_size)
	{
		placement_config_.validate();
		if (worker_count_ == 0U || stream_capacities_.regret_delta_entries == 0U || stream_capacities_.continuation_delta_entries == 0U)
		{
			throw std::invalid_argument("multiway root batch runner requires positive worker limits");
		}
		if (worker_count_ != coordinator.limits().worker_count || stream_capacities_.regret_delta_entries > coordinator.limits().regret_delta_capacity() || stream_capacities_.continuation_delta_entries > coordinator.limits().continuation_delta_capacity())
		{
			throw std::invalid_argument("multiway root batch runner limits must fit its coordinator");
		}
		if (!multiway_scheduler_mode_valid(scheduler_mode_))
		{
			throw std::invalid_argument("multiway root batch runner has an invalid scheduler mode");
		}
		if (dynamic_chunk_size_ == 0U)
		{
			throw std::invalid_argument("multiway root batch runner requires a positive dynamic chunk size");
		}
		worker_affinity_groups_.assign(worker_count_, 0U);
		worker_affinity_masks_.assign(worker_count_, 0U);
		worker_affinity_processors_.assign(worker_count_, kInvalidProcessor);
		placement_inventory_ = discover_multiway_worker_topology();
		const auto coordinator_identity = current_logical_processor_identity();
		if (coordinator_identity == kInvalidProcessor)
		{
			placement_plan_ = resolve_multiway_worker_placement(
				placement_inventory_, worker_count_, placement_config_.policy);
		}
		else
		{
			placement_plan_ = resolve_multiway_worker_placement(
				placement_inventory_, worker_count_, placement_config_.policy, coordinator_identity);
		}
		for (std::size_t worker = 0U; worker < placement_plan_.requested.size() && worker < worker_count_; ++worker)
		{
			const auto& requested = placement_plan_.requested[worker];
			worker_affinity_groups_[worker] = requested.processor_group;
			worker_affinity_processors_[worker] =
				static_cast<std::uint32_t>(requested.processor_group) * (sizeof(std::uint64_t) * 8U) + requested.logical_processor_index;
			if (requested.logical_processor_index < sizeof(std::uint64_t) * 8U)
				worker_affinity_masks_[worker] = std::uint64_t{ 1U } << requested.logical_processor_index;
		}
		worker_scratch_.reserve(worker_count_);
		worker_stream_views_.reserve(worker_count_);
		continuation_stream_views_.reserve(worker_count_);
		discovery_stream_views_.reserve(worker_count_);
		discovery_merge_scratch_.reserve(worker_count_);
		worker_batches_.resize(worker_count_);
		threads_.reserve(worker_count_);
		worker_work_cvs_.reserve(worker_count_);
		for (std::uint32_t worker = 0U; worker < worker_count_; ++worker)
		{
			worker_work_cvs_.push_back(std::make_unique<std::condition_variable>());
		}
		const auto legacy_discovery_capacity = stream_capacities_.regret_delta_entries > std::numeric_limits<std::size_t>::max() / 4U
			? std::numeric_limits<std::size_t>::max()
			: stream_capacities_.regret_delta_entries * 4U;
		if (immutable_epochs_)
		{
			const auto fallback_discovery_capacity = std::max<std::size_t>(64U, legacy_discovery_capacity);
			if (stream_capacities_.discovery_public_states == 0U)
				stream_capacities_.discovery_public_states = coordinator.limits().discovery_public_state_capacity() != 0U
					? coordinator.limits().discovery_public_state_capacity()
					: fallback_discovery_capacity;
			if (stream_capacities_.discovery_rows == 0U)
				stream_capacities_.discovery_rows = coordinator.limits().discovery_row_capacity() != 0U
					? coordinator.limits().discovery_row_capacity()
					: fallback_discovery_capacity;
			if (stream_capacities_.discovery_public_states == 0U || stream_capacities_.discovery_rows == 0U || (stream_capacities_.discovery_public_states > coordinator.limits().discovery_public_state_capacity() && coordinator.limits().discovery_public_state_capacity() != 0U) || (stream_capacities_.discovery_rows > coordinator.limits().discovery_row_capacity() && coordinator.limits().discovery_row_capacity() != 0U))
				throw std::invalid_argument("multiway root batch runner discovery limits must fit its coordinator");
		}
		else
		{
			stream_capacities_.discovery_public_states = 0U;
			stream_capacities_.discovery_rows = 0U;
		}
		if (immutable_epochs_)
		{
			if (stream_capacities_.discovery_public_states > std::numeric_limits<std::size_t>::max() / worker_count_ || stream_capacities_.discovery_rows > std::numeric_limits<std::size_t>::max() / worker_count_)
				throw std::overflow_error("multiway root batch runner discovery output capacity overflows size_t");
			discovery_public_states_.reserve(static_cast<std::size_t>(worker_count_) * stream_capacities_.discovery_public_states);
			discovery_rows_.reserve(static_cast<std::size_t>(worker_count_) * stream_capacities_.discovery_rows);
		}
		for (std::uint32_t worker = 0; worker < worker_count_; ++worker)
		{
			worker_scratch_.emplace_back(worker, stream_capacities_);
			worker_stream_views_.push_back(&worker_scratch_.back().stream);
			continuation_stream_views_.push_back(&worker_scratch_.back().continuation_stream);
			discovery_stream_views_.push_back(&worker_scratch_.back().discovery);
			threads_.emplace_back(&MultiwayRootBatchRunner::worker_loop, this, worker);
		}
	}

	MultiwayRootBatchRunner::~MultiwayRootBatchRunner()
	{
		{
			std::lock_guard<std::mutex> lock(pool_mutex_);
			stop_workers_ = true;
		}
		for (const auto& work_cv : worker_work_cvs_)
			work_cv->notify_one();
		for (auto& thread : threads_)
		{
			if (thread.joinable())
				thread.join();
		}
	}

	MultiwayRootBatchMemorySnapshot MultiwayRootBatchRunner::memory_snapshot() const noexcept
	{
		MultiwayRootBatchMemorySnapshot result;
		result.worker_count = std::min<std::uint32_t>(worker_count_, static_cast<std::uint32_t>(MULTIWAY_MEMORY_MAX_WORKERS));
		for (std::size_t index = 0U; index < result.worker_count; ++index)
		{
			const auto& scratch = worker_scratch_[index];
			result.workers[index].regret_delta_stream = scratch.stream.memory_snapshot();
			result.workers[index].continuation_delta_stream = scratch.continuation_stream.memory_snapshot();
			const auto& public_states = scratch.discovery.public_states();
			const auto& rows = scratch.discovery.rows();
			result.workers[index].discovery_public_states.add_buffer(public_states.size(), public_states.capacity(), sizeof(MultiwayPublicStateDescriptor));
			for (const auto& state : public_states)
			{
				result.workers[index].discovery_public_states.add_buffer(state.incoming_edge.dealt_cards.size(), state.incoming_edge.dealt_cards.capacity(), sizeof(std::uint8_t));
				result.workers[index].discovery_public_states.add_buffer(state.incoming_edge.transition_board.size(), state.incoming_edge.transition_board.capacity(), sizeof(std::uint8_t));
				result.workers[index].discovery_public_states.add_buffer(state.board.size(), state.board.capacity(), sizeof(std::uint8_t));
				result.workers[index].discovery_public_states.add_buffer(state.history.size(), state.history.capacity(), sizeof(MultiwayPublicHistoryEntry));
				result.workers[index].discovery_public_states.add_buffer(state.legal_actions.size(), state.legal_actions.capacity(), sizeof(MultiwayActionDescriptor));
			}
			result.workers[index].discovery_rows.add_buffer(rows.size(), rows.capacity(), sizeof(MultiwaySparseRowShape));
			result.workers[index].action_menu_scratch.add_buffer(
				scratch.action_menu_scratch.size(), scratch.action_menu_scratch.capacity(), sizeof(MultiwayActionDescriptor));
			result.workers[index].total = result.workers[index].regret_delta_stream;
			result.workers[index].total.accumulate(result.workers[index].continuation_delta_stream);
			result.workers[index].total.accumulate(result.workers[index].discovery_public_states);
			result.workers[index].total.accumulate(result.workers[index].discovery_rows);
			result.workers[index].total.accumulate(result.workers[index].action_menu_scratch);
			result.workers[index].total.grouping_only = true;
		}
		result.stream_views.add_buffer(worker_stream_views_.size(), worker_stream_views_.capacity(), sizeof(const MultiwayWorkerDeltaStream*));
		result.stream_views.add_buffer(continuation_stream_views_.size(), continuation_stream_views_.capacity(), sizeof(const MultiwayContinuationDeltaStream*));
		result.stream_views.add_buffer(discovery_stream_views_.size(), discovery_stream_views_.capacity(), sizeof(const MultiwayDiscoveryStream*));
		result.discovery_merge_scratch.add_buffer(discovery_merge_scratch_.cursors.size(), discovery_merge_scratch_.cursors.capacity(), sizeof(std::size_t));
		result.discovery_merge_scratch.add_buffer(discovery_merge_scratch_.tournament.size(), discovery_merge_scratch_.tournament.capacity(), sizeof(std::size_t));
		result.discovery_merge_scratch.add_buffer(admission_order_scratch_.order.size(), admission_order_scratch_.order.capacity(), sizeof(std::size_t));
		result.discovery_merge_scratch.add_buffer(admission_order_scratch_.parent_indexes.size(), admission_order_scratch_.parent_indexes.capacity(), sizeof(std::size_t));
		result.discovery_merge_scratch.add_buffer(admission_order_scratch_.depths.size(), admission_order_scratch_.depths.capacity(), sizeof(std::uint32_t));
		result.discovery_merge_scratch.add_buffer(admission_order_scratch_.colors.size(), admission_order_scratch_.colors.capacity(), sizeof(std::uint8_t));
		result.discovery_merge_scratch.add_buffer(admission_order_scratch_.stack.size(), admission_order_scratch_.stack.capacity(), sizeof(std::size_t));
		result.discovery_outputs.add_buffer(discovery_public_states_.size(), discovery_public_states_.capacity(), sizeof(MultiwayPublicStateDescriptor));
		for (const auto& state : discovery_public_states_)
		{
			result.discovery_outputs.add_buffer(state.incoming_edge.dealt_cards.size(), state.incoming_edge.dealt_cards.capacity(), sizeof(std::uint8_t));
			result.discovery_outputs.add_buffer(state.incoming_edge.transition_board.size(), state.incoming_edge.transition_board.capacity(), sizeof(std::uint8_t));
			result.discovery_outputs.add_buffer(state.board.size(), state.board.capacity(), sizeof(std::uint8_t));
			result.discovery_outputs.add_buffer(state.history.size(), state.history.capacity(), sizeof(MultiwayPublicHistoryEntry));
			result.discovery_outputs.add_buffer(state.legal_actions.size(), state.legal_actions.capacity(), sizeof(MultiwayActionDescriptor));
		}
		result.discovery_outputs.add_buffer(discovery_rows_.size(), discovery_rows_.capacity(), sizeof(MultiwaySparseRowShape));
		result.worker_batches.add_buffer(worker_batches_.size(), worker_batches_.capacity(), sizeof(MultiwayWorkerBatch));
		result.thread_scratch.add_buffer(worker_scratch_.size(), worker_scratch_.capacity(), sizeof(WorkerScratch));
		result.thread_scratch.add_buffer(threads_.size(), threads_.capacity(), sizeof(std::thread));
		result.thread_scratch.add_buffer(worker_work_cvs_.size(), worker_work_cvs_.capacity(), sizeof(std::unique_ptr<std::condition_variable>));
		result.thread_scratch.add_buffer(worker_affinity_groups_.size(), worker_affinity_groups_.capacity(), sizeof(std::uint16_t));
		result.thread_scratch.add_buffer(worker_affinity_masks_.size(), worker_affinity_masks_.capacity(), sizeof(std::uint64_t));
		result.thread_scratch.add_buffer(worker_affinity_processors_.size(), worker_affinity_processors_.capacity(), sizeof(std::uint32_t));
		result.thread_scratch.add_buffer(worker_work_cvs_.size(), worker_work_cvs_.size(), sizeof(std::condition_variable));
		result.total = result.stream_views;
		result.total.accumulate(result.discovery_merge_scratch);
		result.total.accumulate(result.discovery_outputs);
		result.total.accumulate(result.worker_batches);
		result.total.accumulate(result.thread_scratch);
		for (std::size_t index = 0U; index < result.worker_count; ++index)
			result.total.accumulate(result.workers[index].total);
		result.total.grouping_only = true;
		return result;
	}

	void MultiwayRootBatchRunner::worker_loop(std::size_t worker_index)
	{
		auto& worker_scratch = worker_scratch_[worker_index];
		[[maybe_unused]] static constexpr const char* worker_names[] = {
			"mw.worker.00", "mw.worker.01", "mw.worker.02", "mw.worker.03",
			"mw.worker.04", "mw.worker.05", "mw.worker.06", "mw.worker.07",
			"mw.worker.08", "mw.worker.09", "mw.worker.10", "mw.worker.11",
			"mw.worker.12", "mw.worker.13", "mw.worker.14", "mw.worker.15"
		};
		TEXASSOLVER_TRACY_SET_THREAD_NAME(
			worker_index < std::size(worker_names) ? worker_names[worker_index] : "mw.worker");
		if (placement_config_.policy != MultiwayWorkerPlacementPolicy::OsDefault && worker_index < placement_plan_.requested.size())
		{
			worker_scratch.requested_placement = placement_plan_.requested[worker_index];
			worker_scratch.requested_processor = worker_affinity_processors_[worker_index];
			worker_scratch.affinity_mask = worker_affinity_masks_[worker_index];
#if defined(_WIN32)
			if (placement_config_.policy == MultiwayWorkerPlacementPolicy::DiagnosticHardAffinity)
			{
				GROUP_AFFINITY affinity{};
				affinity.Mask = static_cast<KAFFINITY>(worker_affinity_masks_[worker_index]);
				affinity.Group = worker_affinity_groups_[worker_index];
				worker_scratch.affinity_succeeded = affinity.Mask != 0U && ::SetThreadGroupAffinity(::GetCurrentThread(), &affinity, nullptr) != FALSE;
			}
			else if (worker_scratch.requested_placement.cpu_set_id != kMultiwayUnknownTopologyId)
			{
				const ULONG cpu_set_id = static_cast<ULONG>(worker_scratch.requested_placement.cpu_set_id);
				worker_scratch.affinity_succeeded = ::SetThreadSelectedCpuSets(
														::GetCurrentThread(), &cpu_set_id, 1U)
					!= FALSE;
			}
#elif defined(__linux__)
			if (placement_config_.policy == MultiwayWorkerPlacementPolicy::DiagnosticHardAffinity && worker_scratch.requested_processor < CPU_SETSIZE)
			{
				cpu_set_t affinity{};
				CPU_ZERO(&affinity);
				CPU_SET(static_cast<int>(worker_scratch.requested_processor), &affinity);
				worker_scratch.affinity_succeeded = ::sched_setaffinity(
														0, sizeof(affinity), &affinity)
					== 0;
			}
#endif
		}
		worker_scratch.resolved_processor = current_processor_id();
		if (const auto* resolved = find_current_topology_record(placement_inventory_, current_logical_processor_identity()))
			worker_scratch.resolved_placement = *resolved;
		std::uint64_t observed_generation = 0U;
		while (true)
		{
			std::size_t batch_count = 0U;
			std::uint64_t first_trajectory_id = 0U;
			std::uint64_t trajectory_count = 0U;
			std::size_t chunk_count = 0U;
			std::uint64_t seed = 0U;
			double iteration_weight = 1.0;
			auto deadline = std::chrono::steady_clock::time_point::max();
			const auto cv_wait_start = steady_now_nanoseconds();
			{
				TEXASSOLVER_TRACY_ZONE_SCOPED("mw.worker.cv_wait");
				std::unique_lock<std::mutex> lock(pool_mutex_);
				worker_work_cvs_[worker_index]->wait(lock, [this, observed_generation] {
					return stop_workers_ || (batch_active_ && batch_generation_ != observed_generation);
				});
				if (stop_workers_)
					return;
				observed_generation = batch_generation_;
				batch_count = active_batch_count_;
				first_trajectory_id = active_first_trajectory_id_;
				trajectory_count = active_trajectory_count_;
				chunk_count = active_chunk_count_;
				seed = active_seed_;
				iteration_weight = active_iteration_weight_;
				deadline = active_deadline_;
			}
			const auto cv_wait_end = steady_now_nanoseconds();
			worker_scratch_[worker_index].cv_wait_nanoseconds = cv_wait_end >= cv_wait_start
				? cv_wait_end - cv_wait_start
				: 0U;
			if (worker_index < batch_count)
			{
				const auto now = steady_now_nanoseconds();
				const auto notified = notification_timestamp_nanoseconds_;
				worker_scratch_[worker_index].wake_latency_nanoseconds = now >= notified ? now - notified : 0U;
			}

			try
			{
				if (worker_index < batch_count)
				{
					auto& scratch = worker_scratch_[worker_index];
#if !defined(TEXASSOLVER_TRACY_ENABLED) || !TEXASSOLVER_TRACY_ENABLED
					MultiwayAllocationProbeScope allocation_scope(scratch.allocation_probe);
#endif
					{
						TEXASSOLVER_TRACY_ZONE_SCOPED("mw.worker.active");
						const auto active_start = std::chrono::steady_clock::now();
						const auto process_range = [&](const MultiwayTrajectoryRange& range) {
							for (auto local_id = range.begin;
								local_id < range.end;
								local_id += range.stride)
							{
								const bool has_deadline = deadline != std::chrono::steady_clock::time_point::max();
								const bool cancellation_requested =
									((local_id - range.begin) & 7U) == 0U && cancelled_.load(std::memory_order_relaxed);
								if (cancellation_requested || (has_deadline && std::chrono::steady_clock::now() >= deadline))
								{
									cancelled_.store(true, std::memory_order_release);
									break;
								}
								const auto trajectory_id = first_trajectory_id + local_id;
								++scratch.attempted;
								const auto accepted = traversal_.run(
									traversal_.traverser_for_trajectory(trajectory_id),
									trajectory_id,
									multiway_deterministic_trajectory_seed(seed, trajectory_id),
									scratch.stream,
									iteration_weight,
									&scratch.profile,
									&scratch.continuation_stream,
									active_batch_number_,
									nullptr,
									&scratch.action_menu_scratch,
									immutable_epochs_ ? &scratch.discovery : nullptr);
								scratch.peak_regret_delta_entries = std::max(
									scratch.peak_regret_delta_entries, static_cast<std::uint64_t>(scratch.stream.size()));
								scratch.peak_continuation_delta_entries = std::max(
									scratch.peak_continuation_delta_entries, static_cast<std::uint64_t>(scratch.continuation_stream.size()));
								scratch.peak_discovery_public_state_count = std::max(
									scratch.peak_discovery_public_state_count, static_cast<std::uint64_t>(scratch.discovery.public_state_count()));
								scratch.peak_discovery_row_count = std::max(
									scratch.peak_discovery_row_count, static_cast<std::uint64_t>(scratch.discovery.row_count()));
								if (accepted)
								{
									++scratch.accepted;
								}
								else
								{
									++scratch.discarded;
								}
							}
						};
						{
							TEXASSOLVER_TRACY_ZONE_SCOPED("mw.worker.traversal");
							if (scheduler_mode_ == MultiwaySchedulerMode::Fixed)
							{
								TEXASSOLVER_TRACY_ZONE_SCOPED("mw.worker.scheduler.fixed");
								process_range(worker_batches_[worker_index].trajectories);
							}
							else if (scheduler_mode_ == MultiwaySchedulerMode::StaticBlock)
							{
								TEXASSOLVER_TRACY_ZONE_SCOPED("mw.worker.scheduler.static_block");
								process_range(worker_batches_[worker_index].trajectories);
							}
							else
							{
								TEXASSOLVER_TRACY_ZONE_SCOPED("mw.worker.scheduler.dynamic");
								while (!cancelled_.load(std::memory_order_relaxed))
								{
									const auto claim_started = steady_now_nanoseconds();
									const auto chunk_index = next_dynamic_chunk_.fetch_add(1U, std::memory_order_relaxed);
									const auto claim_finished = steady_now_nanoseconds();
									scratch.scheduler_claim_nanoseconds += claim_finished >= claim_started
										? claim_finished - claim_started
										: 0U;
									if (chunk_index >= chunk_count)
										break;
									process_range(MultiwayScheduler::dynamic_chunk_range(trajectory_count, chunk_index, dynamic_chunk_size_));
								}
							}
						}
						scratch.active_nanoseconds = static_cast<std::uint64_t>(
							std::chrono::duration_cast<std::chrono::nanoseconds>(
								std::chrono::steady_clock::now() - active_start)
								.count());
					}
					const auto sort_start = std::chrono::steady_clock::now();
					{
						TEXASSOLVER_TRACY_ZONE_SCOPED("mw.worker.delta_sort");
						scratch.stream.sort_fixed_order();
						scratch.continuation_stream.sort_fixed_order();
					}
					scratch.sort_nanoseconds = static_cast<std::uint64_t>(
						std::chrono::duration_cast<std::chrono::nanoseconds>(
							std::chrono::steady_clock::now() - sort_start)
							.count());
					if (placement_config_.migration_sample_interval_batches != 0U && active_batch_number_ % placement_config_.migration_sample_interval_batches == 0U)
					{
						scratch.migration_sampled = true;
						const auto before = scratch.resolved_processor;
						scratch.resolved_processor = current_processor_id();
						if (scratch.resolved_processor != before)
							scratch.migration_detected = true;
						if (const auto* resolved = find_current_topology_record(placement_inventory_, current_logical_processor_identity()))
							scratch.resolved_placement = *resolved;
					}
				}
			}
			catch (...)
			{
				cancelled_.store(true, std::memory_order_release);
				std::lock_guard<std::mutex> lock(pool_mutex_);
				if (worker_error_ == nullptr)
					worker_error_ = std::current_exception();
			}

			if (worker_index < batch_count)
			{
				TEXASSOLVER_TRACY_ZONE_SCOPED("mw.worker.complete");
				const auto completion_start = steady_now_nanoseconds();
				const auto completed = completed_workers_.fetch_add(1U, std::memory_order_release) + 1U;
				const auto completion_end = steady_now_nanoseconds();
				worker_scratch_[worker_index].completion_nanoseconds = completion_end - completion_start;
				worker_scratch_[worker_index].completion_timestamp_nanoseconds = completion_end;
				if (completed == batch_count)
				{
					batch_completion_timestamp_nanoseconds_.store(completion_end, std::memory_order_release);
					completion_cv_.notify_one();
				}
			}
		}
	}

	void MultiwayRootBatchRunner::prepare_batch_run(
		BatchRunContext& context,
		std::uint64_t first_trajectory_id,
		std::uint64_t trajectory_count,
		std::uint64_t seed,
		double iteration_weight,
		std::chrono::steady_clock::time_point deadline)
	{
		context.first_trajectory_id = first_trajectory_id;
		context.trajectory_count = trajectory_count;
		context.seed = seed;
		context.iteration_weight = iteration_weight;
		context.deadline = deadline;
		if (!std::isfinite(iteration_weight) || iteration_weight <= 0.0)
		{
			throw std::invalid_argument("multiway batch iteration weight must be finite and positive");
		}
		const auto batch_count = scheduler_mode_ == MultiwaySchedulerMode::Fixed
			? MultiwayScheduler::partition_round_robin_into(
				  trajectory_count,
				  worker_count_,
				  worker_batches_.data(),
				  worker_batches_.size())
			: scheduler_mode_ == MultiwaySchedulerMode::StaticBlock
			? MultiwayScheduler::partition_static_block_into(
				  trajectory_count,
				  worker_count_,
				  worker_batches_.data(),
				  worker_batches_.size())
			: trajectory_count == 0U
			? 1U
			: static_cast<std::size_t>(std::min<std::uint64_t>(trajectory_count, worker_count_));
		context.batch_count = batch_count;
		for (auto& scratch : worker_scratch_)
			scratch.reset(profile_mode_);

		context.coordinator_wait_start = std::chrono::steady_clock::now();
		context.coordinator_lock_wait_before = coordinator_->diagnostics().coordinator_lock_wait_nanoseconds;
	}

	void MultiwayRootBatchRunner::dispatch_batch(BatchRunContext& context)
	{
		const auto batch_count = context.batch_count;
		const auto trajectory_count = context.trajectory_count;
		const auto first_trajectory_id = context.first_trajectory_id;
		const auto seed = context.seed;
		const auto iteration_weight = context.iteration_weight;
		const auto deadline = context.deadline;
		{
			std::lock_guard<std::mutex> lock(pool_mutex_);
			if (batch_active_)
			{
				throw std::logic_error("multiway root batch runner does not support concurrent batches");
			}
			batch_active_ = true;
			++batch_generation_;
			completed_workers_.store(0U, std::memory_order_relaxed);
			active_batch_count_ = batch_count;
			active_trajectory_count_ = trajectory_count;
			active_chunk_count_ = scheduler_mode_ == MultiwaySchedulerMode::Dynamic
				? MultiwayScheduler::dynamic_chunk_count(trajectory_count, dynamic_chunk_size_)
				: 0U;
			next_dynamic_chunk_.store(0U, std::memory_order_relaxed);
			active_first_trajectory_id_ = first_trajectory_id;
			active_seed_ = seed;
			active_iteration_weight_ = iteration_weight;
			active_deadline_ = deadline;
			worker_error_ = nullptr;
			cancelled_.store(false, std::memory_order_release);
			notification_timestamp_nanoseconds_ = steady_now_nanoseconds();
			batch_completion_timestamp_nanoseconds_.store(0U, std::memory_order_relaxed);
		}
		for (std::size_t worker = 0U; worker < batch_count; ++worker)
		{
			worker_work_cvs_[worker]->notify_one();
		}
	}

	void MultiwayRootBatchRunner::wait_for_batch_workers()
	{
		{
			std::unique_lock<std::mutex> lock(pool_mutex_);
			completion_cv_.wait(lock, [this] {
				return completed_workers_.load(std::memory_order_acquire) == active_batch_count_;
			});
			batch_active_ = false;
			if (worker_error_ != nullptr)
				std::rethrow_exception(worker_error_);
		}
	}

	void MultiwayRootBatchRunner::collect_worker_counters(BatchRunContext& context)
	{
		auto& result = context.result;
		const auto first_trajectory_id = context.first_trajectory_id;
		const auto trajectory_count = context.trajectory_count;
		const auto seed = context.seed;
		const auto coordinator_wait_start = context.coordinator_wait_start;
		result.coordinator_wait_nanoseconds = static_cast<std::uint64_t>(
			std::chrono::duration_cast<std::chrono::nanoseconds>(
				std::chrono::steady_clock::now() - coordinator_wait_start)
				.count());
		result.run.worker_count = worker_count_;
		result.run.base_seed = seed;
		result.run.first_trajectory_id = first_trajectory_id;
		result.run.trajectory_count = trajectory_count;
		result.run.schedule_fingerprint = multiway_deterministic_schedule_fingerprint(
			worker_count_, seed, first_trajectory_id, trajectory_count,
			scheduler_mode_, dynamic_chunk_size_);
		result.run.scheduler_mode = scheduler_mode_;
		result.run.dynamic_chunk_size = dynamic_chunk_size_;
		context.batch_profile.emplace(profile_mode_);
		auto& batch_profile = *context.batch_profile;
		std::uint64_t terminal_visits = 0U;
		std::uint64_t leaf_visits = 0U;
		std::uint64_t missing_lookup_requests = 0U;
		result.minimum_worker_trajectories = std::numeric_limits<std::uint64_t>::max();
		result.minimum_worker_active_nanoseconds = std::numeric_limits<std::uint64_t>::max();
		result.worker_trajectory_counts.resize(worker_scratch_.size());
		result.worker_active_nanoseconds_by_worker.resize(worker_scratch_.size());
		result.worker_sort_nanoseconds_by_worker.resize(worker_scratch_.size());
		result.worker_wake_latency_nanoseconds_by_worker.resize(worker_scratch_.size());
		result.worker_cv_wait_nanoseconds_by_worker.resize(worker_scratch_.size());
		result.worker_scheduler_claim_nanoseconds_by_worker.resize(worker_scratch_.size());
		result.worker_completion_nanoseconds_by_worker.resize(worker_scratch_.size());
		result.worker_tail_idle_nanoseconds_by_worker.resize(worker_scratch_.size());
		result.worker_regret_delta_entries.resize(worker_scratch_.size());
		result.worker_continuation_delta_entries.resize(worker_scratch_.size());
		result.worker_discovery_public_state_counts.resize(worker_scratch_.size());
		result.worker_discovery_row_counts.resize(worker_scratch_.size());
		result.admission_diagnostics.worker_raw_public_state_candidates_by_worker.resize(worker_scratch_.size());
		result.admission_diagnostics.worker_raw_row_candidates_by_worker.resize(worker_scratch_.size());
		result.admission_diagnostics.worker_local_unique_public_state_candidates_by_worker.resize(worker_scratch_.size());
		result.admission_diagnostics.worker_local_unique_row_candidates_by_worker.resize(worker_scratch_.size());
		result.worker_allocation_counts.resize(worker_scratch_.size());
		result.worker_allocated_bytes.resize(worker_scratch_.size());
		result.worker_requested_processor.resize(worker_scratch_.size());
		result.worker_resolved_processor.resize(worker_scratch_.size());
		result.worker_requested_cpu_set.assign(worker_scratch_.size(), kMultiwayUnknownTopologyId);
		result.worker_resolved_cpu_set.assign(worker_scratch_.size(), kMultiwayUnknownTopologyId);
		result.worker_requested_physical_core.assign(worker_scratch_.size(), kMultiwayUnknownTopologyId);
		result.worker_resolved_physical_core.assign(worker_scratch_.size(), kMultiwayUnknownTopologyId);
		result.worker_requested_smt_sibling.assign(worker_scratch_.size(), kMultiwayUnknownTopologyIndex);
		result.worker_resolved_smt_sibling.assign(worker_scratch_.size(), kMultiwayUnknownTopologyIndex);
		result.worker_requested_cache_domain.assign(worker_scratch_.size(), kMultiwayUnknownTopologyIndex);
		result.worker_resolved_cache_domain.assign(worker_scratch_.size(), kMultiwayUnknownTopologyIndex);
		result.worker_migration_sampled.assign(worker_scratch_.size(), 0U);
		result.worker_migration_detected.assign(worker_scratch_.size(), 0U);
		result.worker_affinity_masks.resize(worker_scratch_.size());
		result.worker_affinity_succeeded.resize(worker_scratch_.size());
		result.placement_policy = placement_config_.policy;
		result.affinity_requested = placement_config_.policy != MultiwayWorkerPlacementPolicy::OsDefault;
		result.worker_profiles.resize(worker_scratch_.size());
		const auto batch_completion_timestamp = batch_completion_timestamp_nanoseconds_.load(std::memory_order_acquire);
		{
			TEXASSOLVER_TRACY_ZONE_SCOPED("mw.coordinator.collect");
			for (std::size_t worker_index = 0U; worker_index < worker_scratch_.size(); ++worker_index)
			{
				TEXASSOLVER_TRACY_ZONE_SCOPED("mw.coordinator.collect_worker");
				auto& scratch = worker_scratch_[worker_index];
				result.trajectories_attempted += scratch.attempted;
				result.trajectories_accepted += scratch.accepted;
				result.trajectories_discarded += scratch.discarded;
				result.delta_entries_merged += scratch.stream.size();
				const auto regret_entries = static_cast<std::uint64_t>(scratch.stream.size());
				const auto continuation_entries = static_cast<std::uint64_t>(scratch.continuation_stream.size());
				const auto discovery_public_states = static_cast<std::uint64_t>(scratch.discovery.public_state_count());
				const auto discovery_rows = static_cast<std::uint64_t>(scratch.discovery.row_count());
				result.total_peak_regret_delta_entries += scratch.peak_regret_delta_entries;
				result.total_peak_continuation_delta_entries += scratch.peak_continuation_delta_entries;
				result.total_peak_delta_entries += scratch.peak_regret_delta_entries + scratch.peak_continuation_delta_entries;
				result.generated_update_count += regret_entries + continuation_entries;
				result.generated_discovery_public_state_count += discovery_public_states;
				result.generated_discovery_row_count += discovery_rows;
				result.admission_diagnostics.raw_public_state_candidates += discovery_public_states;
				result.admission_diagnostics.raw_row_candidates += discovery_rows;
				result.admission_diagnostics.worker_raw_public_state_candidates_by_worker[worker_index] = discovery_public_states;
				result.admission_diagnostics.worker_raw_row_candidates_by_worker[worker_index] = discovery_rows;
				if (immutable_epochs_)
					scratch.discovery.prepare_for_merge();
				const auto local_unique_public_states = immutable_epochs_
					? static_cast<std::uint64_t>(scratch.discovery.public_state_count())
					: 0U;
				const auto local_unique_rows = immutable_epochs_
					? static_cast<std::uint64_t>(scratch.discovery.row_count())
					: 0U;
				result.admission_diagnostics.worker_local_unique_public_state_candidates_by_worker[worker_index] = local_unique_public_states;
				result.admission_diagnostics.worker_local_unique_row_candidates_by_worker[worker_index] = local_unique_rows;
				result.admission_diagnostics.worker_local_unique_public_state_candidates += local_unique_public_states;
				result.admission_diagnostics.worker_local_unique_row_candidates += local_unique_rows;
				result.maximum_worker_delta_entries = std::max(
					result.maximum_worker_delta_entries,
					std::max(scratch.peak_regret_delta_entries, scratch.peak_continuation_delta_entries));
				result.worker_active_nanoseconds += scratch.active_nanoseconds;
				result.worker_trajectory_counts[worker_index] = scratch.attempted;
				result.worker_active_nanoseconds_by_worker[worker_index] = scratch.active_nanoseconds;
				result.worker_sort_nanoseconds_by_worker[worker_index] = scratch.sort_nanoseconds;
				result.worker_wake_latency_nanoseconds_by_worker[worker_index] = scratch.wake_latency_nanoseconds;
				result.worker_cv_wait_nanoseconds_by_worker[worker_index] = scratch.cv_wait_nanoseconds;
				result.worker_scheduler_claim_nanoseconds_by_worker[worker_index] = scratch.scheduler_claim_nanoseconds;
				result.worker_completion_nanoseconds_by_worker[worker_index] = scratch.completion_nanoseconds;
				result.worker_tail_idle_nanoseconds_by_worker[worker_index] =
					batch_completion_timestamp > scratch.completion_timestamp_nanoseconds
					? batch_completion_timestamp - scratch.completion_timestamp_nanoseconds
					: 0U;
				result.worker_regret_delta_entries[worker_index] = scratch.peak_regret_delta_entries;
				result.worker_continuation_delta_entries[worker_index] = scratch.peak_continuation_delta_entries;
				result.worker_discovery_public_state_counts[worker_index] = scratch.peak_discovery_public_state_count;
				result.worker_discovery_row_counts[worker_index] = scratch.peak_discovery_row_count;
				const auto allocation_snapshot = scratch.allocation_probe.snapshot();
				result.worker_allocation_counts[worker_index] = allocation_snapshot.allocation_count;
				result.worker_allocated_bytes[worker_index] = allocation_snapshot.allocated_bytes;
				result.worker_requested_processor[worker_index] = scratch.requested_processor;
				result.worker_resolved_processor[worker_index] = scratch.resolved_processor;
				result.worker_requested_cpu_set[worker_index] = scratch.requested_placement.cpu_set_id;
				result.worker_resolved_cpu_set[worker_index] = scratch.resolved_placement.cpu_set_id;
				result.worker_requested_physical_core[worker_index] = scratch.requested_placement.physical_core_id;
				result.worker_resolved_physical_core[worker_index] = scratch.resolved_placement.physical_core_id;
				result.worker_requested_smt_sibling[worker_index] = scratch.requested_placement.smt_sibling_index;
				result.worker_resolved_smt_sibling[worker_index] = scratch.resolved_placement.smt_sibling_index;
				result.worker_requested_cache_domain[worker_index] = scratch.requested_placement.cache_domain_id;
				result.worker_resolved_cache_domain[worker_index] = scratch.resolved_placement.cache_domain_id;
				result.worker_migration_sampled[worker_index] = scratch.migration_sampled ? 1U : 0U;
				result.worker_migration_detected[worker_index] = scratch.migration_detected ? 1U : 0U;
				result.worker_affinity_masks[worker_index] = scratch.affinity_mask;
				result.worker_affinity_succeeded[worker_index] = scratch.affinity_succeeded ? 1U : 0U;
				result.regret_delta_capacity_exhausted = result.regret_delta_capacity_exhausted || scratch.stream.capacity_exhausted();
				result.continuation_delta_capacity_exhausted = result.continuation_delta_capacity_exhausted || scratch.continuation_stream.capacity_exhausted();
				result.discovery_public_state_capacity_exhausted = result.discovery_public_state_capacity_exhausted || scratch.discovery.public_state_capacity_exhausted();
				result.discovery_row_capacity_exhausted = result.discovery_row_capacity_exhausted || scratch.discovery.row_capacity_exhausted();
				result.worker_profiles[worker_index] = scratch.profile.snapshot();
				result.minimum_worker_active_nanoseconds = std::min(
					result.minimum_worker_active_nanoseconds, scratch.active_nanoseconds);
				result.maximum_worker_active_nanoseconds = std::max(
					result.maximum_worker_active_nanoseconds, scratch.active_nanoseconds);
				result.delta_sort_nanoseconds += scratch.sort_nanoseconds;
				result.worker_wake_latency_nanoseconds += scratch.wake_latency_nanoseconds;
				result.worker_cv_wait_nanoseconds += scratch.cv_wait_nanoseconds;
				result.worker_scheduler_claim_nanoseconds += scratch.scheduler_claim_nanoseconds;
				result.worker_completion_nanoseconds += scratch.completion_nanoseconds;
				result.worker_tail_idle_nanoseconds += result.worker_tail_idle_nanoseconds_by_worker[worker_index];
				result.minimum_worker_trajectories = std::min(result.minimum_worker_trajectories, scratch.attempted);
				result.maximum_worker_trajectories = std::max(result.maximum_worker_trajectories, scratch.attempted);
				batch_profile.merge(scratch.profile.snapshot());
				terminal_visits += scratch.discovery.terminal_visits();
				leaf_visits += scratch.discovery.leaf_visits();
				missing_lookup_requests += scratch.discovery.missing_lookup_requests();
			}
		}
		context.terminal_visits = terminal_visits;
		context.leaf_visits = leaf_visits;
		context.missing_lookup_requests = missing_lookup_requests;
	}

	bool MultiwayRootBatchRunner::assemble_worker_result(BatchRunContext& context)
	{
		auto& result = context.result;
		auto& batch_profile = *context.batch_profile;
		if (result.minimum_worker_trajectories == std::numeric_limits<std::uint64_t>::max())
		{
			result.minimum_worker_trajectories = 0U;
		}
		if (result.minimum_worker_active_nanoseconds == std::numeric_limits<std::uint64_t>::max())
		{
			result.minimum_worker_active_nanoseconds = 0U;
		}
		result.discovery_capacity_exhausted = result.discovery_public_state_capacity_exhausted || result.discovery_row_capacity_exhausted;
		result.capacity_exhausted = result.regret_delta_capacity_exhausted || result.continuation_delta_capacity_exhausted || result.discovery_capacity_exhausted;
		result.barrier_wait_nanoseconds = result.coordinator_wait_nanoseconds > result.maximum_worker_active_nanoseconds
			? result.coordinator_wait_nanoseconds - result.maximum_worker_active_nanoseconds
			: 0U;
		if (cancelled_.load(std::memory_order_acquire))
		{
			result.profile = batch_profile.snapshot();
			return false;
		}
		if (result.trajectories_discarded != 0U || result.trajectories_attempted != result.trajectories_accepted)
		{
			result.clean = false;
			result.profile = batch_profile.snapshot();
			return false;
		}
		return true;
	}

	bool MultiwayRootBatchRunner::admit_discovery(BatchRunContext& context)
	{
		auto& result = context.result;
		auto& batch_profile = *context.batch_profile;
		auto& coordinator_allocation_probe = *context.coordinator_allocation_probe;
		const auto terminal_visits = context.terminal_visits;
		const auto leaf_visits = context.leaf_visits;
		const auto missing_lookup_requests = context.missing_lookup_requests;
		if (immutable_epochs_)
		{
			const auto admission_start = std::chrono::steady_clock::now();
			const auto public_states_before = coordinator_->diagnostics().public_states_admitted;
			const auto rows_before = coordinator_->diagnostics().sparse_rows_admitted;
			{
				const auto phase_started = std::chrono::steady_clock::now();
				TEXASSOLVER_TRACY_ZONE_SCOPED("mw.coordinator.discovery_prepare");
				for (const auto& scratch : worker_scratch_)
					if (scratch.discovery.overflowed())
					{
						result.discovery_capacity_exhausted = true;
						result.clean = false;
						const auto allocation_snapshot = coordinator_allocation_probe.snapshot();
						result.coordinator_allocation_count = allocation_snapshot.allocation_count;
						result.coordinator_allocated_bytes = allocation_snapshot.allocated_bytes;
						result.profile = batch_profile.snapshot();
						return false;
					}
				result.admission_diagnostics.discovery_prepare_nanoseconds += static_cast<std::uint64_t>(
					std::chrono::duration_cast<std::chrono::nanoseconds>(
						std::chrono::steady_clock::now() - phase_started)
						.count());
			}
			{
				const auto phase_started = std::chrono::steady_clock::now();
				TEXASSOLVER_TRACY_ZONE_SCOPED("mw.coordinator.discovery_merge");
				detail::merge_multiway_discovery_streams(
					discovery_stream_views_, discovery_public_states_, discovery_rows_,
					discovery_merge_scratch_, detail::MultiwayDiscoveryMergePath::Automatic,
					&result.admission_diagnostics);
				result.admission_diagnostics.discovery_merge_nanoseconds += static_cast<std::uint64_t>(
					std::chrono::duration_cast<std::chrono::nanoseconds>(
						std::chrono::steady_clock::now() - phase_started)
						.count());
			}
			result.admission_diagnostics.globally_unique_public_state_candidates =
				static_cast<std::uint64_t>(discovery_public_states_.size());
			result.admission_diagnostics.globally_unique_row_candidates =
				static_cast<std::uint64_t>(discovery_rows_.size());
			MultiwayAdmissionDiagnosticsScope admission_diagnostics_scope(
				*coordinator_, result.admission_diagnostics);
			{
				const auto phase_started = std::chrono::steady_clock::now();
				TEXASSOLVER_TRACY_ZONE_SCOPED("mw.coordinator.discovery_parent_order");
				detail::build_multiway_parent_first_order(
					discovery_public_states_, *coordinator_, result.admission_diagnostics,
					admission_order_scratch_);
				result.admission_diagnostics.parent_order_nanoseconds += static_cast<std::uint64_t>(
					std::chrono::duration_cast<std::chrono::nanoseconds>(
						std::chrono::steady_clock::now() - phase_started)
						.count());
			}
			{
				TEXASSOLVER_TRACY_ZONE_SCOPED("mw.coordinator.discovery_admit");
				coordinator_->admit_prepared_batch(
					discovery_public_states_, admission_order_scratch_.order, discovery_rows_,
					&admission_order_scratch_.parent_indexes);
			}
			const auto public_states_after = coordinator_->diagnostics().public_states_admitted;
			const auto rows_after = coordinator_->diagnostics().sparse_rows_admitted;
			result.admission_diagnostics.newly_admitted_public_state_candidates =
				public_states_after >= public_states_before
				? public_states_after - public_states_before
				: 0U;
			result.admission_diagnostics.newly_admitted_row_candidates =
				rows_after >= rows_before ? rows_after - rows_before : 0U;
			result.admission_diagnostics.already_admitted_public_state_candidates =
				result.admission_diagnostics.globally_unique_public_state_candidates >= result.admission_diagnostics.newly_admitted_public_state_candidates
				? result.admission_diagnostics.globally_unique_public_state_candidates - result.admission_diagnostics.newly_admitted_public_state_candidates
				: 0U;
			result.admission_diagnostics.already_admitted_row_candidates =
				result.admission_diagnostics.globally_unique_row_candidates >= result.admission_diagnostics.newly_admitted_row_candidates
				? result.admission_diagnostics.globally_unique_row_candidates - result.admission_diagnostics.newly_admitted_row_candidates
				: 0U;
			coordinator_->merge_traversal_counters(terminal_visits, leaf_visits, missing_lookup_requests);
			result.coordinator_admission_nanoseconds = static_cast<std::uint64_t>(
				std::chrono::duration_cast<std::chrono::nanoseconds>(
					std::chrono::steady_clock::now() - admission_start)
					.count());
		}
		return true;
	}

	void MultiwayRootBatchRunner::merge_batch_deltas(BatchRunContext& context)
	{
		auto& result = context.result;
		auto& batch_profile = *context.batch_profile;
		const auto merge_start = std::chrono::steady_clock::now();
		{
			TEXASSOLVER_TRACY_ZONE_SCOPED("mw.coordinator.delta_merge");
			MultiwaySearchProfileScope profile_scope(
				&batch_profile, MultiwaySearchProfileStage::DeltaMerge);
			coordinator_->merge_worker_streams(worker_stream_views_);
			if (const auto* selector = traversal_.continuation_selector())
			{
				selector->merge_worker_streams(continuation_stream_views_);
			}
		}
		result.merge_nanoseconds = static_cast<std::uint64_t>(
			std::chrono::duration_cast<std::chrono::nanoseconds>(
				std::chrono::steady_clock::now() - merge_start)
				.count());
	}

	void MultiwayRootBatchRunner::finalize_batch_result(BatchRunContext& context)
	{
		auto& result = context.result;
		auto& batch_profile = *context.batch_profile;
		auto& coordinator_allocation_probe = *context.coordinator_allocation_probe;
		result.run.merged_stream_fingerprint =
			coordinator_->diagnostics().last_merged_stream_fingerprint;
		result.coordinator_lock_wait_nanoseconds =
			coordinator_->diagnostics().coordinator_lock_wait_nanoseconds - context.coordinator_lock_wait_before;
		const auto allocation_snapshot = coordinator_allocation_probe.snapshot();
		result.coordinator_allocation_count = allocation_snapshot.allocation_count;
		result.coordinator_allocated_bytes = allocation_snapshot.allocated_bytes;
		result.profile = batch_profile.snapshot();
		result.clean = result.trajectories_discarded == 0U && result.trajectories_attempted == result.trajectories_accepted && (result.delta_entries_merged == 0U || result.run.merged_stream_fingerprint != 0U);
	}

	MultiwayRootBatchResult MultiwayRootBatchRunner::run(
		std::uint64_t first_trajectory_id,
		std::uint64_t trajectory_count,
		std::uint64_t seed,
		double iteration_weight,
		std::chrono::steady_clock::time_point deadline)
	{
		TEXASSOLVER_PROFILE_SCOPE("multiway.traversal.root_batch");
		BatchRunContext context;
		prepare_batch_run(context, first_trajectory_id, trajectory_count, seed, iteration_weight, deadline);
		{
			TEXASSOLVER_TRACY_ZONE_SCOPED("mw.batch.dispatch_wait");
			dispatch_batch(context);
			wait_for_batch_workers();
		}
		collect_worker_counters(context);
		if (!assemble_worker_result(context))
			return std::move(context.result);
		context.coordinator_allocation_probe.emplace();
		MultiwayAllocationProbeScope coordinator_allocation_scope(*context.coordinator_allocation_probe);
		if (!admit_discovery(context))
			return std::move(context.result);
		merge_batch_deltas(context);
		finalize_batch_result(context);
		return std::move(context.result);
	}
} // namespace texas::solver::multiway
