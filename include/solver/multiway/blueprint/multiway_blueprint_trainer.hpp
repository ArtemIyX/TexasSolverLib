#pragma once

#include "solver/multiway/blueprint/multiway_export.hpp"
#include "solver/multiway/blueprint/multiway_artifact.hpp"
#include "solver/multiway/engine/multiway_traversal.hpp"
#include "solver/multiway/abstraction/multiway_bucket_artifact.hpp"
#include "solver/multiway/engine/multiway_memory.hpp"
#include "solver/multiway/continuation/multiway_continuation_selector.hpp"
#include "games/multiway_rules.hpp"

#include <cstdint>
#include <array>
#include <chrono>
#include <filesystem>
#include <memory>
#include <vector>

namespace texas::solver::multiway
{
	struct MultiwayTrainingCheckpointWriteResult;

	enum class MultiwayTrainingCapacityStage : std::uint8_t
	{
		None = 0U,
		PublicStates = 1U,
		SparseRows = 2U,
		SparseValues = 3U,
		WorkerDeltas = 4U,
		Discovery = 5U,
	};

	[[nodiscard]] MultiwayRootSnapshot make_multiway_initial_blueprint_root(
		const MultiwayGameRules& rules,
		MultiwayPrivateConfig private_ranges,
		const MultiwayActionAbstraction& action_abstraction,
		std::uint64_t action_abstraction_version,
		std::uint64_t leaf_model_version,
		PlayerId first_player = 0);

	struct MultiwayBlueprintIterationSchedule
	{
		bool linear_iteration_weighting = true;
		bool discount_regrets = false;
		double regret_discount_factor = 1.0;
		std::uint64_t discount_interval_batches = 1;
		bool prune_negative_regrets = false;
		std::uint64_t pruning_warmup_batches = 0;
		std::uint64_t pruning_interval_batches = 1;
		// Negative-regret pruning keeps a bounded floor and periodically explores
		// every action to recover actions that were previously skipped.
		double pruning_threshold = 0.0;
		double regret_floor = 0.0;
		std::uint64_t recovery_interval_batches = 20;
		double pruning_exploration_probability = 0.05;
		double pruning_action_probability_threshold = 0.0;
		// Zero disables late-window export. A non-zero value is the first
		// included batch in its separately accumulated weighted window.
		std::uint64_t late_window_start_batch = 0;

		void validate() const;
		[[nodiscard]] double strategy_weight(std::uint64_t one_based_batch) const;
		[[nodiscard]] std::uint64_t identity() const noexcept;
	};

	struct MultiwayBlueprintTrainingStatus
	{
		std::uint64_t batches = 0;
		std::uint64_t trajectories = 0;
		std::uint64_t discarded_trajectories = 0;
		std::uint64_t merged_stream_fingerprint = 0;
		std::uint64_t pruned_negative_regrets = 0;
		std::uint64_t visited_public_descriptors = 0;
		std::uint64_t admitted_rows = 0;
		std::uint64_t preflop_rows = 0;
		std::uint64_t flop_rows = 0;
		std::uint64_t turn_rows = 0;
		std::uint64_t river_rows = 0;
		std::uint64_t admitted_action_cells = 0;
		std::uint64_t terminal_visits = 0;
		std::uint64_t leaf_visits = 0;
		std::array<std::uint64_t, 4> street_visits{};
		std::uint64_t missing_lookup_requests = 0;
		std::uint64_t late_window_start_batch = 0;
		bool late_window_active = false;
		MultiwaySchedulerMode scheduler_mode = MultiwaySchedulerMode::Fixed;
		std::uint32_t dynamic_chunk_size = MULTIWAY_DYNAMIC_SCHEDULER_CHUNK_SIZE;
		std::uint32_t scheduler_policy_version = MULTIWAY_SCHEDULER_POLICY_VERSION;
		std::uint32_t static_block_scheduler_version = MULTIWAY_STATIC_BLOCK_SCHEDULER_VERSION;
		std::uint64_t schedule_fingerprint = 0U;
		std::uint32_t partition_version = MULTIWAY_PARTITION_VERSION;
		std::uint32_t merge_order_version = MULTIWAY_MERGE_ORDER_VERSION;
		// Batch-boundary telemetry. These fields are diagnostic and are not part
		// of the strategy identity or deterministic merge state.
		std::uint64_t configured_max_public_states = 0;
		std::uint64_t configured_max_sparse_rows = 0;
		std::uint64_t configured_max_sparse_values = 0;
		std::uint64_t configured_worker_delta_capacity = 0;
		std::uint64_t configured_discovery_capacity = 0;
		std::uint64_t configured_max_regret_delta_entries_per_worker = 0;
		std::uint64_t configured_max_continuation_delta_entries_per_worker = 0;
		std::uint64_t configured_max_discovery_public_states_per_worker = 0;
		std::uint64_t configured_max_discovery_rows_per_worker = 0;
		std::uint64_t peak_public_states = 0;
		std::uint64_t peak_sparse_rows = 0;
		std::uint64_t peak_sparse_values = 0;
		std::uint64_t peak_worker_delta_entries = 0;
		std::uint64_t total_peak_regret_delta_entries = 0;
		std::uint64_t total_peak_continuation_delta_entries = 0;
		std::uint64_t total_peak_delta_entries = 0;
		std::uint64_t generated_update_count = 0;
		std::uint64_t generated_discovery_public_state_count = 0;
		std::uint64_t generated_discovery_row_count = 0;
		std::uint64_t coordinator_admission_nanoseconds = 0;
		MultiwayAdmissionDiagnostics admission_diagnostics{};
		std::uint64_t trainer_only_nanoseconds = 0;
		std::uint64_t process_cpu_nanoseconds = 0;
		std::uint64_t peak_working_set_bytes = 0;
		std::uint64_t cumulative_worker_delta_entries = 0;
		std::uint64_t peak_compact_storage_bytes = 0;
		std::uint64_t current_process_rss_bytes = 0;
		// Legacy batch-boundary sample retained for progress diagnostics.
		std::uint64_t peak_process_rss_bytes = 0;
		std::uint64_t elapsed_wall_nanoseconds = 0;
		std::uint64_t checkpoint_wall_nanoseconds = 0;
		std::uint64_t export_wall_nanoseconds = 0;
		std::uint64_t worker_active_nanoseconds = 0;
		std::uint64_t worker_wake_latency_nanoseconds = 0;
		std::uint64_t worker_cv_wait_nanoseconds = 0;
		std::uint64_t worker_scheduler_claim_nanoseconds = 0;
		std::uint64_t worker_completion_nanoseconds = 0;
		std::uint64_t worker_tail_idle_nanoseconds = 0;
		std::uint64_t minimum_worker_active_nanoseconds = 0;
		std::uint64_t maximum_worker_active_nanoseconds = 0;
		std::uint64_t coordinator_wait_nanoseconds = 0;
		std::uint64_t barrier_wait_nanoseconds = 0;
		std::uint64_t coordinator_lock_wait_nanoseconds = 0;
		std::uint64_t delta_sort_nanoseconds = 0;
		std::uint64_t merge_nanoseconds = 0;
		std::uint64_t minimum_worker_trajectories = 0;
		std::uint64_t maximum_worker_trajectories = 0;
		std::uint32_t requested_worker_count = 0;
		std::uint32_t effective_worker_count = 0;
		std::uint64_t trajectories_per_batch = 0;
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
		std::uint64_t memory_preflight_estimate_bytes = 0;
		MultiwayMemoryLedger memory_ledger{};
		MultiwayProcessMemorySnapshotCollection process_memory_snapshots{};
		MultiwayMemorySubsystemSnapshotCollection memory_subsystem_snapshots{};
		MultiwayWorkingSetGateConfig working_set_gate_config{};
		MultiwayWorkingSetGateResult working_set_gate{};
		MultiwaySystemMemoryGuardConfig system_memory_guard_config{};
		MultiwaySystemMemoryGuardResult system_memory_guard{};
		MultiwayExportTelemetry export_telemetry{};
		bool process_rss_available = false;
		std::vector<std::uint64_t> admitted_rows_per_batch;
		MultiwayTrainingCapacityStage capacity_exhaustion_stage =
			MultiwayTrainingCapacityStage::None;
		bool failed = false;
		bool failed_batch_available = false;
		MultiwayRootBatchResult failed_batch{};
	};

	// Called after each clean batch, outside the traversal and merge kernels.
	using MultiwayBlueprintBatchProgressCallback =
		void (*)(const MultiwayBlueprintTrainingStatus&, void*);

	// Public-only coverage summary for a sparse full-blueprint export. Zero
	// counts remain meaningful: they distinguish an empty sparse table from an
	// unavailable report without retaining private cards or ranges.
	struct MultiwayBlueprintCoverageManifest
	{
		std::uint64_t visited_public_descriptors = 0;
		std::uint64_t admitted_rows = 0;
		std::uint64_t admitted_action_cells = 0;
		std::uint64_t terminal_visits = 0;
		std::uint64_t leaf_visits = 0;
		std::uint64_t missing_lookup_requests = 0;
		std::uint64_t preflop_rows = 0;
		std::uint64_t flop_rows = 0;
		std::uint64_t turn_rows = 0;
		std::uint64_t river_rows = 0;
		std::array<std::uint64_t, 4> street_visits{};
	};

	struct MultiwayBlueprintTrainingCheckpoint
	{
		MultiwayModelIdentity identity{};
		MultiwayBlueprintTrainingMetadata training{};
		MultiwayBlueprintCoverageManifest coverage{};
		MultiwayCoordinatorCheckpoint coordinator{};
		std::vector<double> late_window_baseline;
		MultiwayCheckpointTelemetry telemetry{};

		void validate() const;
	};

	struct MultiwayFullBlueprintExportPlan
	{
		MultiwayModelIdentity identity{};
		MultiwayBlueprintTrainingMetadata training{};
		std::uint64_t shape_count = 0U;
		std::uint64_t row_count = 0U;
		std::uint64_t action_count = 0U;
		std::uint64_t serialized_bytes = 0U;
		std::uint64_t order_index_bytes = 0U;
		bool compact_direct_order = false;

		void validate() const;
	};

	struct MultiwayFullBlueprintExportResult
	{
		MultiwayFullBlueprintExportPlan plan{};
		std::uint64_t payload_hash = 0U;
		std::uint64_t policy_content_hash = 0U;
		std::uint64_t peak_scratch_bytes = 0U;
		core::AtomicPublishMode publication_mode = core::AtomicPublishMode::Unknown;
	};

	// Production composition boundary. Artifact inputs are non-owning and must
	// outlive a training session; their model identity is validated on creation.
	struct MultiwayBlueprintTrainingConfig
	{
		MultiwayGameRules rules = MultiwayGameRules::standard_6max();
		MultiwayBlueprintConfig blueprint{};
		MultiwayBucketBaselineProfile bucket_profile = MultiwayBucketBaselineProfile::standard();
		MultiwayActionAbstractionConfig action_abstraction{};
		MultiwayCFRConfig cfr{ 6, true };
		MultiwaySolverLimits limits{ 1, 1, 1024, 1024, 8192, 8192, 8192 };
		MultiwayBlueprintIterationSchedule schedule{};
		std::uint64_t deterministic_seed = 1;
		// The production default is a complete preflop-to-river traversal. Tests
		// and constrained DEV profiles may explicitly select shallower limits.
		std::uint32_t max_decision_depth = MULTIWAY_MAX_DECISION_DEPTH;
		std::uint32_t max_public_chance_depth = MULTIWAY_MAX_PUBLIC_CHANCE_DEPTH;
		// Diagnostic execution metadata. Zero uses the effective runtime value.
		std::uint32_t requested_worker_count = 0U;
		MultiwaySchedulerMode scheduler_mode = MultiwaySchedulerMode::Fixed;
		std::uint32_t dynamic_chunk_size = MULTIWAY_DYNAMIC_SCHEDULER_CHUNK_SIZE;
		MultiwayWorkerPlacementConfig placement{};
		bool pin_workers = false;
		std::uint64_t memory_preflight_estimate_bytes = 0U;
		MultiwayMemoryLedger memory_ledger{};
		MultiwayProcessMemorySnapshotCollection process_memory_snapshots{};
		MultiwayMemorySubsystemSnapshotCollection memory_subsystem_snapshots{};
		MultiwayWorkingSetMonitor working_set_monitor{};
		bool immutable_batch_epochs = true;

		void validate() const;
		[[nodiscard]] MultiwayModelIdentity identity() const;
	};

	// Coordinator-owned offline session. Publishing creates an immutable compact
	// root artifact and never exposes dense sparse-row storage to callers.
	class MultiwayBlueprintTrainer
	{
	public:
		MultiwayBlueprintTrainer(
			MultiwayModelIdentity identity,
			MultiwayRootBatchRunner& batch_runner,
			MultiwaySolverCoordinator& coordinator,
			MultiwayBlueprintIterationSchedule schedule = {},
			std::uint64_t deterministic_seed = 1,
			std::uint32_t requested_worker_count = 0U,
			std::uint64_t memory_preflight_estimate_bytes = 0U,
			MultiwayMemoryLedger memory_ledger = {},
			MultiwayProcessMemorySnapshotCollection process_memory_snapshots = {},
			MultiwayMemorySubsystemSnapshotCollection memory_subsystem_snapshots = {},
			const MultiwayBucketRegistry* buckets = nullptr,
			const MultiwayFixedContinuationSelector* continuation_selector = nullptr,
			MultiwayWorkingSetMonitor working_set_monitor = {});

		void run_batches(
			std::uint64_t batch_count,
			std::uint64_t trajectories_per_batch,
			std::uint64_t seed,
			MultiwayBlueprintBatchProgressCallback progress_callback = nullptr,
			void* progress_context = nullptr);

		[[nodiscard]] std::uint64_t trajectories() const noexcept { return status_.trajectories; }
		[[nodiscard]] std::uint64_t batches() const noexcept { return status_.batches; }
		[[nodiscard]] const MultiwayBlueprintTrainingStatus& status() const noexcept { return status_; }
		[[nodiscard]] MultiwayBlueprintCoverageManifest coverage_manifest() const noexcept;
		[[nodiscard]] MultiwayFullBlueprintArtifact export_full_policy() const;
		[[nodiscard]] MultiwayFullBlueprintExportResult save_full_policy_atomic(
			const std::filesystem::path& path) const;
		[[nodiscard]] MultiwayTrainingCheckpointWriteResult save_checkpoint_atomic(
			const std::filesystem::path& path) const;
		[[nodiscard]] MultiwayBlueprintTrainingCheckpoint checkpoint() const;
		[[nodiscard]] MultiwayBlueprintSnapshot publish(
			MultiwayBlueprintPolicyKind policy_kind = MultiwayBlueprintPolicyKind::WeightedAverage) const;
		void resume_from_root_policy(const MultiwayBlueprintSnapshot& snapshot);
		void resume_from_checkpoint(const MultiwayBlueprintTrainingCheckpoint& checkpoint);
		void record_process_memory_snapshot(
			MultiwayMemoryPhase phase,
			MultiwayMemoryComponent checkpoint_buffers = {},
			MultiwayMemoryComponent export_buffers = {});

	private:
		void record_export_memory_snapshot(
			MultiwayMemoryPhase phase,
			MultiwayProcessMemorySnapshot snapshot,
			MultiwayMemoryComponent export_buffers) const;

		MultiwayModelIdentity identity_{};
		MultiwayRootBatchRunner* batch_runner_ = nullptr;
		MultiwaySolverCoordinator* coordinator_ = nullptr;
		MultiwayBlueprintIterationSchedule schedule_{};
		std::uint64_t deterministic_seed_ = 1;
		std::chrono::steady_clock::time_point started_at_{};
		std::uint64_t process_cpu_started_nanoseconds_ = 0U;
		const MultiwayBucketRegistry* buckets_ = nullptr;
		const MultiwayFixedContinuationSelector* continuation_selector_ = nullptr;
		MultiwayWorkingSetMonitor working_set_monitor_{};
		mutable MultiwayBlueprintTrainingStatus status_{};
		std::vector<double> late_window_baseline_;
	};

	// Owns the production assembly while retaining only non-owning bucket/leaf
	// inputs. It is intentionally a library surface, not a process or service.
	class MultiwayBlueprintTrainingSession
	{
	public:
		MultiwayBlueprintTrainingSession(
			MultiwayBlueprintTrainingConfig config,
			MultiwayRootSnapshot root,
			const MultiwayBucketRegistry& buckets,
			const MultiwayLeafEvaluator* leaf_evaluator = nullptr);

		void run_batches(
			std::uint64_t batch_count,
			MultiwayBlueprintBatchProgressCallback progress_callback = nullptr,
			void* progress_context = nullptr);
		void resume_from_root_policy(const MultiwayBlueprintSnapshot& snapshot);
		void resume_from_checkpoint(const MultiwayBlueprintTrainingCheckpoint& checkpoint);
		[[nodiscard]] MultiwayBlueprintSnapshot export_policy(
			MultiwayBlueprintPolicyKind policy_kind = MultiwayBlueprintPolicyKind::WeightedAverage) const;
		[[nodiscard]] const MultiwayBlueprintTrainingConfig& config() const noexcept { return config_; }
		[[nodiscard]] const MultiwayBlueprintTrainingStatus& status() const noexcept;
		[[nodiscard]] MultiwayBlueprintCoverageManifest coverage_manifest() const noexcept;
		[[nodiscard]] MultiwayFullBlueprintArtifact export_full_policy() const;
		[[nodiscard]] MultiwayFullBlueprintExportResult save_full_policy_atomic(
			const std::filesystem::path& path) const;
		[[nodiscard]] MultiwayTrainingCheckpointWriteResult save_checkpoint_atomic(
			const std::filesystem::path& path) const;
		[[nodiscard]] MultiwayBlueprintTrainingCheckpoint checkpoint() const;
		void record_process_memory_snapshot(MultiwayMemoryPhase phase);

	private:
		MultiwayBlueprintTrainingConfig config_{};
		MultiwayRootSnapshot root_{};
		const MultiwayBucketRegistry* buckets_ = nullptr;
		MultiwayLeafEvaluator leaf_evaluator_{};
		std::unique_ptr<MultiwayActionAbstraction> action_abstraction_;
		std::unique_ptr<MultiwayFixedContinuationSelector> continuation_selector_;
		std::unique_ptr<MultiwaySolverCoordinator> coordinator_;
		std::unique_ptr<MultiwayRootBatchRunner> batch_runner_;
		std::unique_ptr<MultiwayBlueprintTrainer> trainer_;
		mutable MultiwayMemoryComponent checkpoint_buffers_{};
		mutable MultiwayMemoryComponent export_buffers_{};
	};

} // namespace texas::solver::multiway
