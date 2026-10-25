#pragma once

#include "solver/multiway/engine/multiway_cfr.hpp"
#include "solver/multiway/continuation/multiway_continuation_selector.hpp"
#include "solver/multiway/abstraction/multiway_action_abstraction.hpp"
#include "solver/multiway/abstraction/multiway_bucket_model.hpp"
#include "solver/multiway/continuation/multiway_leaf_evaluator.hpp"
#include "solver/multiway/abstraction/multiway_public_builder.hpp"
#include "solver/multiway/engine/multiway_scheduler.hpp"
#include "solver/multiway/session/multiway_search_profile.hpp"
#include "solver/multiway/engine/multiway_solver.hpp"
#include "solver/multiway/abstraction/multiway_terminal_adapter.hpp"
#include "solver/multiway/engine/multiway_allocation_probe.hpp"
#include "solver/multiway/engine/multiway_worker_topology.hpp"

#include <cstddef>
#include <cstdint>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <exception>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

namespace texas::solver::multiway
{

	class MultiwayBlueprintPolicyProvider;
	struct MultiwayBlueprintLookupAudit;
	class MultiwayFutureBucketArtifact;

	inline constexpr std::uint32_t MULTIWAY_MAX_DECISION_DEPTH = 64U;
	inline constexpr std::uint32_t MULTIWAY_MAX_PUBLIC_CHANCE_DEPTH = 3U;
	// The production abstraction emits at most five entries; one exact off-tree
	// insertion needs six. Two spare entries keep the boundary explicit.
	inline constexpr std::size_t MULTIWAY_MAX_TRAVERSAL_ACTIONS = MULTIWAY_MAX_ABSTRACTED_ACTIONS;

	class MultiwayDiscoveryStream
	{
	public:
		explicit MultiwayDiscoveryStream(std::size_t capacity = 0U)
			: MultiwayDiscoveryStream(capacity, capacity) {}

		MultiwayDiscoveryStream(std::size_t public_state_capacity, std::size_t row_capacity)
			: public_state_capacity_(public_state_capacity), row_capacity_(row_capacity)
		{
			public_states_.reserve(public_state_capacity_);
			rows_.reserve(row_capacity_);
		}

		[[nodiscard]] bool try_append_public_state(const MultiwayPublicStateDescriptor& state);
		[[nodiscard]] bool try_append_row(const MultiwaySparseRowShape& shape) noexcept;
		void rewind() noexcept;
		void sort_fixed_order() noexcept;
		void prepare_for_merge();
		[[nodiscard]] bool overflowed() const noexcept { return overflowed_; }
		[[nodiscard]] bool public_state_capacity_exhausted() const noexcept { return public_state_capacity_exhausted_; }
		[[nodiscard]] bool row_capacity_exhausted() const noexcept { return row_capacity_exhausted_; }
		[[nodiscard]] const std::vector<MultiwayPublicStateDescriptor>& public_states() const noexcept { return public_states_; }
		[[nodiscard]] const std::vector<MultiwaySparseRowShape>& rows() const noexcept { return rows_; }
		[[nodiscard]] std::size_t public_state_count() const noexcept { return public_states_.size(); }
		[[nodiscard]] std::size_t row_count() const noexcept { return rows_.size(); }
		[[nodiscard]] MultiwayMemoryComponent memory_snapshot() const noexcept;
		[[nodiscard]] std::uint64_t terminal_visits() const noexcept { return terminal_visits_; }
		[[nodiscard]] std::uint64_t leaf_visits() const noexcept { return leaf_visits_; }
		[[nodiscard]] std::uint64_t missing_lookup_requests() const noexcept { return missing_lookup_requests_; }
		void record_terminal_visit() noexcept { ++terminal_visits_; }
		void record_leaf_visit() noexcept { ++leaf_visits_; }
		void record_missing_lookup() noexcept { ++missing_lookup_requests_; }
		void rewind_to(
			std::size_t public_state_count,
			std::size_t row_count,
			std::uint64_t terminal_visits,
			std::uint64_t leaf_visits,
			std::uint64_t missing_lookup_requests) noexcept;

	private:
		std::size_t public_state_capacity_ = 0U;
		std::size_t row_capacity_ = 0U;
		bool overflowed_ = false;
		bool public_state_capacity_exhausted_ = false;
		bool row_capacity_exhausted_ = false;
		std::vector<MultiwayPublicStateDescriptor> public_states_;
		std::vector<MultiwaySparseRowShape> rows_;
		std::uint64_t terminal_visits_ = 0U;
		std::uint64_t leaf_visits_ = 0U;
		std::uint64_t missing_lookup_requests_ = 0U;
	};

	namespace detail
	{
		struct MultiwayDiscoveryParentOrderScratch
		{
			std::vector<std::size_t> order;
			std::vector<std::size_t> parent_indexes;
			std::vector<std::uint32_t> depths;
			std::vector<std::uint8_t> colors;
			std::vector<std::size_t> stack;
		};

		void merge_multiway_discovery_streams(
			const std::vector<const MultiwayDiscoveryStream*>& streams,
			std::vector<MultiwayPublicStateDescriptor>& public_states,
			std::vector<MultiwaySparseRowShape>& rows,
			std::vector<std::size_t>& cursors,
			MultiwayAdmissionDiagnostics* diagnostics = nullptr);

		void build_multiway_parent_first_order(
			const std::vector<MultiwayPublicStateDescriptor>& candidates,
			const MultiwaySolverCoordinator& coordinator,
			MultiwayAdmissionDiagnostics& diagnostics,
			MultiwayDiscoveryParentOrderScratch& scratch);
	} // namespace detail

	struct MultiwayTraversalPruningConfig
	{
		bool enabled = false;
		std::uint64_t warmup_batches = 0;
		std::uint64_t recovery_interval_batches = 20;
		double exploration_probability = 0.05;
		double action_probability_threshold = 0.0;
		double regret_threshold = 0.0;

		void validate() const;
		[[nodiscard]] bool recovery_batch(std::uint64_t batch_number) const noexcept;
		[[nodiscard]] bool should_explore_action(
			std::uint64_t batch_number, bool below_threshold,
			bool immediate_terminal, bool river) const noexcept;
	};

	// Bounded lazy traversal. It samples opponent and public-chance nodes,
	// enumerates traverser decisions, and stops at exact terminals or the typed
	// strategic leaf boundary.
	class MultiwayRootExternalSamplingTraversal
	{
	public:
		MultiwayRootExternalSamplingTraversal(
			MultiwaySolverCoordinator& coordinator,
			const MultiwayRootSnapshot& root,
			const MultiwayActionAbstraction& action_abstraction,
			const MultiwayBucketRegistry& buckets,
			const MultiwayLeafEvaluator* leaf_evaluator = nullptr,
			std::uint32_t max_decision_depth = 1U,
			std::uint32_t max_public_chance_depth = 0U,
			const MultiwayBlueprintPolicyProvider* blueprint_policy = nullptr,
			const MultiwayFixedContinuationSelector* continuation_selector = nullptr,
			const MultiwayFutureBucketArtifact* future_bucket_artifact = nullptr,
			MultiwayTraversalPruningConfig pruning = {});

		[[nodiscard]] bool run(
			PlayerId traverser,
			std::uint64_t trajectory_id,
			std::uint64_t seed,
			MultiwayWorkerDeltaStream& stream,
			double iteration_weight = 1.0,
			MultiwaySearchProfile* profile = nullptr,
			MultiwayContinuationDeltaStream* continuation_stream = nullptr,
			std::uint64_t batch_number = 0U,
			MultiwayBlueprintLookupAudit* lookup_audit = nullptr,
			std::vector<MultiwayActionDescriptor>* action_menu_scratch = nullptr,
			MultiwayDiscoveryStream* discovery_stream = nullptr) const;

		[[nodiscard]] const MultiwayFixedContinuationSelector* continuation_selector() const noexcept
		{
			return continuation_selector_;
		}

		[[nodiscard]] PlayerId root_traverser() const noexcept
		{
			return root_->public_state.betting.current_player;
		}

		[[nodiscard]] PlayerId traverser_for_trajectory(
			std::uint64_t trajectory_id) const noexcept
		{
			return root_->seat_order[trajectory_id % root_->seat_order.size()];
		}

	private:
		struct TraversalContext;

		[[nodiscard]] Value traverse_decision(
			const MultiwayPublicStateDescriptor& state,
			std::uint32_t decision_depth,
			std::uint32_t public_chance_depth,
			TraversalContext& context) const;

		[[nodiscard]] Value traverse_public_chance(
			const MultiwayPublicStateDescriptor& state,
			std::uint32_t decision_depth,
			std::uint32_t public_chance_depth,
			TraversalContext& context) const;

		[[nodiscard]] Value evaluate_leaf(
			const MultiwayPublicStateDescriptor& state,
			TraversalContext& context) const;

		MultiwaySolverCoordinator* coordinator_ = nullptr;
		const MultiwayRootSnapshot* root_ = nullptr;
		const MultiwayActionAbstraction* action_abstraction_ = nullptr;
		const MultiwayBucketRegistry* buckets_ = nullptr;
		const MultiwayLeafEvaluator* leaf_evaluator_ = nullptr;
		std::uint32_t max_decision_depth_ = 1U;
		std::uint32_t max_public_chance_depth_ = 0U;
		const MultiwayBlueprintPolicyProvider* blueprint_policy_ = nullptr;
		const MultiwayFixedContinuationSelector* continuation_selector_ = nullptr;
		const MultiwayFutureBucketArtifact* future_bucket_artifact_ = nullptr;
		MultiwayTraversalPruningConfig pruning_{};
		std::uint64_t range_model_identity_ = 0;
		MultiwayTerminalAdapter terminal_;
	};

	struct MultiwayRootBatchResult
	{
		std::uint64_t trajectories_attempted = 0;
		std::uint64_t trajectories_accepted = 0;
		std::uint64_t trajectories_discarded = 0;
		std::uint64_t delta_entries_merged = 0;
		std::uint64_t minimum_worker_trajectories = 0;
		std::uint64_t maximum_worker_trajectories = 0;
		std::uint64_t maximum_worker_delta_entries = 0;
		std::uint64_t total_peak_regret_delta_entries = 0;
		std::uint64_t total_peak_continuation_delta_entries = 0;
		std::uint64_t total_peak_delta_entries = 0;
		std::uint64_t generated_update_count = 0;
		std::uint64_t generated_discovery_public_state_count = 0;
		std::uint64_t generated_discovery_row_count = 0;
		std::uint64_t coordinator_admission_nanoseconds = 0;
		std::uint64_t coordinator_allocation_count = 0;
		std::uint64_t coordinator_allocated_bytes = 0;
		MultiwayAdmissionDiagnostics admission_diagnostics{};
		// Traversal time excluding worker delta sorting.
		std::uint64_t worker_active_nanoseconds = 0;
		std::uint64_t worker_wake_latency_nanoseconds = 0;
		std::uint64_t worker_cv_wait_nanoseconds = 0;
		std::uint64_t worker_scheduler_claim_nanoseconds = 0;
		std::uint64_t worker_completion_nanoseconds = 0;
		std::uint64_t worker_tail_idle_nanoseconds = 0;
		std::uint64_t minimum_worker_active_nanoseconds = 0;
		std::uint64_t maximum_worker_active_nanoseconds = 0;
		std::uint64_t coordinator_wait_nanoseconds = 0;
		// Coordinator wait not covered by the slowest worker's active interval.
		std::uint64_t barrier_wait_nanoseconds = 0;
		std::uint64_t coordinator_lock_wait_nanoseconds = 0;
		std::uint64_t delta_sort_nanoseconds = 0;
		std::uint64_t merge_nanoseconds = 0;
		// Attempted trajectories assigned to each worker; on a clean run these are completed.
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
		MultiwaySearchProfileSnapshot profile{};
		MultiwayRunMetadata run{};
		bool regret_delta_capacity_exhausted = false;
		bool continuation_delta_capacity_exhausted = false;
		bool discovery_public_state_capacity_exhausted = false;
		bool discovery_row_capacity_exhausted = false;
		bool capacity_exhausted = false;
		bool discovery_capacity_exhausted = false;
		bool clean = false;
	};

	struct MultiwayWorkerStreamCapacities
	{
		std::size_t regret_delta_entries = 0U;
		std::size_t continuation_delta_entries = 0U;
		std::size_t discovery_public_states = 0U;
		std::size_t discovery_rows = 0U;
	};

	// Deterministic root-only batch runner. Workers are kept logically separate
	// until coordinator merge; parallel dispatch can be introduced without
	// changing seeds, partitions, or merge order.
	class MultiwayRootBatchRunner
	{
	public:
		MultiwayRootBatchRunner(
			MultiwayRootExternalSamplingTraversal traversal,
			MultiwaySolverCoordinator& coordinator,
			std::uint32_t worker_count,
			std::size_t worker_delta_capacity,
			MultiwaySearchProfileMode profile_mode,
			MultiwayWorkerPlacementConfig placement,
			bool immutable_epochs = false,
			MultiwaySchedulerMode scheduler_mode = MultiwaySchedulerMode::Fixed,
			std::uint32_t dynamic_chunk_size = MULTIWAY_DYNAMIC_SCHEDULER_CHUNK_SIZE);
		MultiwayRootBatchRunner(
			MultiwayRootExternalSamplingTraversal traversal,
			MultiwaySolverCoordinator& coordinator,
			std::uint32_t worker_count,
			std::size_t worker_delta_capacity,
			MultiwaySearchProfileMode profile_mode = MultiwaySearchProfileMode::Disabled,
			bool pin_workers = false,
			bool immutable_epochs = false,
			MultiwaySchedulerMode scheduler_mode = MultiwaySchedulerMode::Fixed,
			std::uint32_t dynamic_chunk_size = MULTIWAY_DYNAMIC_SCHEDULER_CHUNK_SIZE);
		MultiwayRootBatchRunner(
			MultiwayRootExternalSamplingTraversal traversal,
			MultiwaySolverCoordinator& coordinator,
			std::uint32_t worker_count,
			MultiwayWorkerStreamCapacities capacities,
			MultiwaySearchProfileMode profile_mode,
			MultiwayWorkerPlacementConfig placement,
			bool immutable_epochs = false,
			MultiwaySchedulerMode scheduler_mode = MultiwaySchedulerMode::Fixed,
			std::uint32_t dynamic_chunk_size = MULTIWAY_DYNAMIC_SCHEDULER_CHUNK_SIZE);
		MultiwayRootBatchRunner(
			MultiwayRootExternalSamplingTraversal traversal,
			MultiwaySolverCoordinator& coordinator,
			std::uint32_t worker_count,
			MultiwayWorkerStreamCapacities capacities,
			MultiwaySearchProfileMode profile_mode = MultiwaySearchProfileMode::Disabled,
			bool pin_workers = false,
			bool immutable_epochs = false,
			MultiwaySchedulerMode scheduler_mode = MultiwaySchedulerMode::Fixed,
			std::uint32_t dynamic_chunk_size = MULTIWAY_DYNAMIC_SCHEDULER_CHUNK_SIZE);
		~MultiwayRootBatchRunner();

		MultiwayRootBatchRunner(const MultiwayRootBatchRunner&) = delete;
		MultiwayRootBatchRunner& operator=(const MultiwayRootBatchRunner&) = delete;

		[[nodiscard]] MultiwayRootBatchResult run(
			std::uint64_t first_trajectory_id,
			std::uint64_t trajectory_count,
			std::uint64_t seed,
			double iteration_weight = 1.0,
			std::chrono::steady_clock::time_point deadline =
				std::chrono::steady_clock::time_point::max());
		[[nodiscard]] std::uint32_t dynamic_chunk_size() const noexcept { return dynamic_chunk_size_; }
		[[nodiscard]] MultiwaySchedulerMode scheduler_mode() const noexcept { return scheduler_mode_; }
		[[nodiscard]] MultiwayRootBatchMemorySnapshot memory_snapshot() const noexcept;
		void set_batch_number(std::uint64_t batch_number) noexcept { active_batch_number_ = batch_number; }

	private:
		void worker_loop(std::size_t worker_index);

		struct alignas(64) WorkerScratch
		{
			explicit WorkerScratch(std::size_t worker_index, MultiwayWorkerStreamCapacities capacities)
				: stream(worker_index, capacities.regret_delta_entries), continuation_stream(worker_index, capacities.continuation_delta_entries), discovery(capacities.discovery_public_states, capacities.discovery_rows)
			{
				action_menu_scratch.reserve(MULTIWAY_MAX_TRAVERSAL_ACTIONS);
			}

			MultiwayWorkerDeltaStream stream;
			MultiwayContinuationDeltaStream continuation_stream;
			MultiwayDiscoveryStream discovery;
			std::uint64_t attempted = 0;
			std::uint64_t accepted = 0;
			std::uint64_t discarded = 0;
			std::uint64_t active_nanoseconds = 0;
			std::uint64_t sort_nanoseconds = 0;
			std::uint64_t wake_latency_nanoseconds = 0;
			std::uint64_t cv_wait_nanoseconds = 0;
			std::uint64_t scheduler_claim_nanoseconds = 0;
			std::uint64_t completion_nanoseconds = 0;
			std::uint64_t completion_timestamp_nanoseconds = 0;
			std::uint64_t tail_idle_nanoseconds = 0;
			std::uint64_t peak_regret_delta_entries = 0;
			std::uint64_t peak_continuation_delta_entries = 0;
			std::uint64_t peak_discovery_public_state_count = 0;
			std::uint64_t peak_discovery_row_count = 0;
			std::uint32_t resolved_processor = 0U;
			MultiwayWorkerTopologyRecord requested_placement{};
			MultiwayWorkerTopologyRecord resolved_placement{};
			bool migration_sampled = false;
			bool migration_detected = false;
			std::uint64_t affinity_mask = 0U;
			std::uint32_t requested_processor = 0xffffffffU;
			bool affinity_succeeded = false;
			MultiwayAllocationProbe allocation_probe;
			std::vector<MultiwayActionDescriptor> action_menu_scratch;
			MultiwaySearchProfile profile{};

			void reset(MultiwaySearchProfileMode profile_mode) noexcept
			{
				stream.rewind(0U);
				stream.clear_capacity_exhaustion();
				continuation_stream.rewind(0U);
				continuation_stream.clear_capacity_exhaustion();
				discovery.rewind();
				attempted = 0;
				accepted = 0;
				discarded = 0;
				active_nanoseconds = 0;
				sort_nanoseconds = 0;
				wake_latency_nanoseconds = 0;
				cv_wait_nanoseconds = 0;
				scheduler_claim_nanoseconds = 0;
				completion_nanoseconds = 0;
				completion_timestamp_nanoseconds = 0;
				tail_idle_nanoseconds = 0;
				peak_regret_delta_entries = 0;
				peak_continuation_delta_entries = 0;
				peak_discovery_public_state_count = 0;
				peak_discovery_row_count = 0;
				resolved_placement = {};
				migration_sampled = false;
				migration_detected = false;
				allocation_probe.reset();
				profile.reset(profile_mode);
			}
		};

		MultiwayRootExternalSamplingTraversal traversal_;
		MultiwaySolverCoordinator* coordinator_ = nullptr;
		std::uint32_t worker_count_ = 0;
		MultiwayWorkerStreamCapacities stream_capacities_{};
		MultiwaySearchProfileMode profile_mode_ = MultiwaySearchProfileMode::Disabled;
		MultiwayWorkerPlacementConfig placement_config_{};
		MultiwayWorkerTopologyInventory placement_inventory_{};
		MultiwayWorkerPlacementPlan placement_plan_{};
		bool immutable_epochs_ = false;
		MultiwaySchedulerMode scheduler_mode_ = MultiwaySchedulerMode::Fixed;
		std::uint32_t dynamic_chunk_size_ = MULTIWAY_DYNAMIC_SCHEDULER_CHUNK_SIZE;
		std::vector<WorkerScratch> worker_scratch_;
		std::vector<const MultiwayWorkerDeltaStream*> worker_stream_views_;
		std::vector<const MultiwayContinuationDeltaStream*> continuation_stream_views_;
		std::vector<const MultiwayDiscoveryStream*> discovery_stream_views_;
		std::vector<std::size_t> discovery_merge_cursors_;
		std::vector<MultiwayPublicStateDescriptor> discovery_public_states_;
		std::vector<MultiwaySparseRowShape> discovery_rows_;
		detail::MultiwayDiscoveryParentOrderScratch admission_order_scratch_;
		std::vector<MultiwayWorkerBatch> worker_batches_;
		std::vector<std::thread> threads_;
		std::vector<std::unique_ptr<std::condition_variable>> worker_work_cvs_;
		std::vector<std::uint16_t> worker_affinity_groups_;
		std::vector<std::uint64_t> worker_affinity_masks_;
		std::vector<std::uint32_t> worker_affinity_processors_;
		std::mutex pool_mutex_;
		std::condition_variable completion_cv_;
		std::uint64_t notification_timestamp_nanoseconds_ = 0U;
		std::atomic<std::uint64_t> batch_completion_timestamp_nanoseconds_{ 0U };
		bool stop_workers_ = false;
		bool batch_active_ = false;
		std::uint64_t batch_generation_ = 0U;
		std::atomic<std::size_t> completed_workers_{ 0U };
		std::size_t active_batch_count_ = 0U;
		std::uint64_t active_trajectory_count_ = 0U;
		std::size_t active_chunk_count_ = 0U;
		std::atomic<std::size_t> next_dynamic_chunk_{ 0U };
		std::uint64_t active_first_trajectory_id_ = 0U;
		std::uint64_t active_seed_ = 0U;
		std::uint64_t active_batch_number_ = 0U;
		double active_iteration_weight_ = 1.0;
		std::chrono::steady_clock::time_point active_deadline_ =
			std::chrono::steady_clock::time_point::max();
		std::atomic<bool> cancelled_{ false };
		std::exception_ptr worker_error_;
	};

} // namespace texas::solver::multiway
