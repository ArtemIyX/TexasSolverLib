#pragma once

/** @file
 * Offline multiway blueprint training configuration, status, and session APIs.
 */

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
	struct MultiwayPB12CoverageAdmission;
	struct MultiwayTrainingCheckpointWriteResult;
	struct MultiwayExternalCheckpointManifest;
	struct MultiwayExternalStorageTelemetry;

	/** Subsystem where configured capacity prevented another training batch. */
	enum class MultiwayTrainingCapacityStage : std::uint8_t
	{
		None = 0U,
		PublicStates = 1U,
		SparseRows = 2U,
		SparseValues = 3U,
		WorkerDeltas = 4U,
		Discovery = 5U,
	};

	/** Builds an initial root snapshot from game rules, ranges, and abstractions. */
	[[nodiscard]] MultiwayRootSnapshot make_multiway_initial_blueprint_root(
		const MultiwayGameRules& rules,
		MultiwayPrivateConfig private_ranges,
		const MultiwayActionAbstraction& action_abstraction,
		std::uint64_t action_abstraction_version,
		std::uint64_t leaf_model_version,
		PlayerId first_player = 0);

	/** Batch-level CFR weighting, discounting, pruning, and export-window policy. */
#pragma pack(push, 8)
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

		/** Checks schedule parameters and their supported ranges. */
		void validate() const;
		/** Returns the strategy accumulation weight for a one-based batch index. */
		[[nodiscard]] double strategy_weight(std::uint64_t one_based_batch) const;
		/** Returns a stable identity hash for all schedule settings. */
		[[nodiscard]] std::uint64_t identity() const noexcept;
	};
#pragma pack(pop)

	/** Batch-boundary training counters, capacity, worker, memory, and timing telemetry. */
#pragma pack(push, 8)
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
#pragma pack(pop)

	// Called after each clean batch, outside the traversal and merge kernels.
	/** Callback invoked after each completed batch, outside traversal and merge. */
	using MultiwayBlueprintBatchProgressCallback =
		void (*)(const MultiwayBlueprintTrainingStatus&, void*);

	// Public-only coverage summary for a sparse full-blueprint export. Zero
	// counts remain meaningful: they distinguish an empty sparse table from an
	// unavailable report without retaining private cards or ranges.
	/** Public-state coverage totals accumulated during blueprint training. */
#pragma pack(push, 8)
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
#pragma pack(pop)

	/** Complete validated state required to resume blueprint training. */
#pragma pack(push, 8)
	struct MultiwayBlueprintTrainingCheckpoint
	{
		MultiwayModelIdentity identity{};
		MultiwayBlueprintTrainingMetadata training{};
		MultiwayBlueprintCoverageManifest coverage{};
		MultiwayCoordinatorCheckpoint coordinator{};
		std::vector<double> late_window_baseline;
		MultiwayCheckpointTelemetry telemetry{};

		/** Checks identity, counters, coordinator state, and late-window values. */
		void validate() const;
	};
#pragma pack(pop)

	/** Precomputed counts and byte sizes for a full-blueprint export. */
#pragma pack(push, 8)
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

		/** Checks identity, row/action counts, and serialized-size fields. */
		void validate() const;
	};
#pragma pack(pop)

	/** Hashes, sizes, scratch use, and publication mode from a full export. */
#pragma pack(push, 8)
	struct MultiwayFullBlueprintExportResult
	{
		MultiwayFullBlueprintExportPlan plan{};
		std::uint64_t payload_hash = 0U;
		std::uint64_t policy_content_hash = 0U;
		std::uint64_t peak_scratch_bytes = 0U;
		std::uint64_t serialized_bytes = 0U;
		std::uint64_t shard_count = 0U;
		std::uint64_t manifest_bytes = 0U;
		bool sharded = false;
		core::AtomicPublishMode publication_mode = core::AtomicPublishMode::Unknown;
	};
#pragma pack(pop)

	// Production composition boundary. Artifact inputs are non-owning and must
	// outlive a training session; their model identity is validated on creation.
	/** Complete rules, model, scheduling, capacity, and memory configuration. */
#pragma pack(push, 8)
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

		/** Validates the configuration and compatibility of its component settings. */
		void validate() const;
		/** Derives the model identity represented by this configuration. */
		[[nodiscard]] MultiwayModelIdentity identity() const;
	};
#pragma pack(pop)

	// Coordinator-owned offline session. Publishing creates an immutable compact
	// root artifact and never exposes dense sparse-row storage to callers.
	/** Runs offline training and exports policies or resumable checkpoints. */
	class MultiwayBlueprintTrainer
	{
	public:
		/** Binds a trainer to its batch runner, coordinator, schedule, and inputs. */
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

		/** Runs the requested batches and invokes progress after each clean batch. */
		void run_batches(
			std::uint64_t batch_count,
			std::uint64_t trajectories_per_batch,
			std::uint64_t seed,
			MultiwayBlueprintBatchProgressCallback progress_callback = nullptr,
			void* progress_context = nullptr);

		/** Returns the number of completed trajectories. */
		[[nodiscard]] std::uint64_t trajectories() const noexcept { return status_.trajectories; }
		/** Returns the number of completed batches. */
		[[nodiscard]] std::uint64_t batches() const noexcept { return status_.batches; }
		/** Returns the current batch-boundary status and telemetry. */
		[[nodiscard]] const MultiwayBlueprintTrainingStatus& status() const noexcept { return status_; }
		/** Returns the public-only coverage totals. */
		[[nodiscard]] MultiwayBlueprintCoverageManifest coverage_manifest() const noexcept;
		/** Builds an owning full-policy artifact from accumulated rows. */
		[[nodiscard]] MultiwayFullBlueprintArtifact export_full_policy() const;
		/** Streams a full policy artifact to path and returns export telemetry. */
		[[nodiscard]] MultiwayFullBlueprintExportResult save_full_policy_atomic(
			const std::filesystem::path& path) const;
		/** Saves a process-resume checkpoint to path. */
		[[nodiscard]] MultiwayTrainingCheckpointWriteResult save_checkpoint_atomic(
			const std::filesystem::path& path) const;
		/** Saves an externally managed checkpoint and returns its manifest. */
		[[nodiscard]] MultiwayExternalCheckpointManifest save_external_checkpoint_atomic(
			const std::filesystem::path& path,
			std::uint64_t workflow_identity_hash,
			std::uint64_t storage_identity_hash) const;
		/** Creates an in-memory checkpoint of the current trainer state. */
		[[nodiscard]] MultiwayBlueprintTrainingCheckpoint checkpoint() const;
		/** Returns telemetry for configured external checkpoint storage. */
		[[nodiscard]] MultiwayExternalStorageTelemetry external_storage_telemetry(bool include_disk_usage = true) const;
		/** Installs an observer for external artifact publication events. */
		void set_external_publication_observer(MultiwayExternalPublicationObserver observer, void* context);
		/** Exports a compact root snapshot in the selected policy mode. */
		[[nodiscard]] MultiwayBlueprintSnapshot publish(
			MultiwayBlueprintPolicyKind policy_kind = MultiwayBlueprintPolicyKind::WeightedAverage) const;
		/** Initializes resume state from a compatible root-policy snapshot. */
		void resume_from_root_policy(const MultiwayBlueprintSnapshot& snapshot);
		/** Restores all training accumulators from a compatible checkpoint. */
		void resume_from_checkpoint(const MultiwayBlueprintTrainingCheckpoint& checkpoint);
		/** Restores state from a checkpoint verified against workflow and storage IDs. */
		void resume_from_external_checkpoint(
			const MultiwayExternalCheckpointManifest& checkpoint,
			std::uint64_t workflow_identity_hash,
			std::uint64_t storage_identity_hash);
		/** Adds a process-memory sample and associated temporary buffer estimates. */
		void record_process_memory_snapshot(
			MultiwayMemoryPhase phase,
			MultiwayMemoryComponent checkpoint_buffers = {},
			MultiwayMemoryComponent export_buffers = {});

	private:
		struct ExportRunContext;

		void prepare_full_policy_export_plan(
			ExportRunContext& context,
			const std::filesystem::path& path) const;
		void enforce_full_policy_export_gates(
			ExportRunContext& context,
			const std::filesystem::path& path) const;
		void setup_full_policy_export_writer(
			ExportRunContext& context,
			const std::filesystem::path& path) const;
		void write_full_policy_export_rows(ExportRunContext& context) const;
		void write_external_full_policy_export_rows(ExportRunContext& context) const;
		void write_memory_full_policy_export_rows(ExportRunContext& context) const;
		[[nodiscard]] MultiwayMemoryComponent export_buffer_component(
			const ExportRunContext& context) const noexcept;
		[[nodiscard]] MultiwayFullBlueprintExportResult finish_full_policy_export(
			ExportRunContext& context) const;

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
	/** Owns the production training composition and presents a session API. */
	class MultiwayBlueprintTrainingSession
	{
	public:
		/** Builds the trainer composition for the supplied config and root state. */
		MultiwayBlueprintTrainingSession(
			MultiwayBlueprintTrainingConfig config,
			MultiwayRootSnapshot root,
			const MultiwayBucketRegistry& buckets,
			const MultiwayLeafEvaluator* leaf_evaluator = nullptr);

		/** Runs the requested number of batches and reports completed-batch progress. */
		void run_batches(
			std::uint64_t batch_count,
			MultiwayBlueprintBatchProgressCallback progress_callback = nullptr,
			void* progress_context = nullptr);
		// Seeds the captured PB-12 public rows through coordinator admission.
		/** Admits captured PB-12 public rows into the coordinator. */
		[[nodiscard]] MultiwayPB12CoverageAdmission admit_pb12_targeted_coverage();
		/** Resumes policy state from a compatible root snapshot. */
		void resume_from_root_policy(const MultiwayBlueprintSnapshot& snapshot);
		/** Resumes complete training state from a compatible checkpoint. */
		void resume_from_checkpoint(const MultiwayBlueprintTrainingCheckpoint& checkpoint);
		/** Resumes an external checkpoint after workflow/storage identity checks. */
		void resume_from_external_checkpoint(
			const MultiwayExternalCheckpointManifest& checkpoint,
			std::uint64_t workflow_identity_hash,
			std::uint64_t storage_identity_hash);
		/** Exports a compact root policy in the selected accumulation mode. */
		[[nodiscard]] MultiwayBlueprintSnapshot export_policy(
			MultiwayBlueprintPolicyKind policy_kind = MultiwayBlueprintPolicyKind::WeightedAverage) const;
		/** Returns the immutable configuration used to build this session. */
		[[nodiscard]] const MultiwayBlueprintTrainingConfig& config() const noexcept { return config_; }
		/** Returns current batch-boundary counters and telemetry. */
		[[nodiscard]] const MultiwayBlueprintTrainingStatus& status() const noexcept;
		/** Returns public-only coverage totals. */
		[[nodiscard]] MultiwayBlueprintCoverageManifest coverage_manifest() const noexcept;
		/** Builds an owning full-policy artifact from accumulated rows. */
		[[nodiscard]] MultiwayFullBlueprintArtifact export_full_policy() const;
		/** Saves a full policy artifact and returns export telemetry. */
		[[nodiscard]] MultiwayFullBlueprintExportResult save_full_policy_atomic(
			const std::filesystem::path& path) const;
		/** Saves a process-resume checkpoint to path. */
		[[nodiscard]] MultiwayTrainingCheckpointWriteResult save_checkpoint_atomic(
			const std::filesystem::path& path) const;
		/** Saves an externally managed checkpoint and returns its manifest. */
		[[nodiscard]] MultiwayExternalCheckpointManifest save_external_checkpoint_atomic(
			const std::filesystem::path& path,
			std::uint64_t workflow_identity_hash,
			std::uint64_t storage_identity_hash) const;
		/** Creates an in-memory checkpoint of the current trainer state. */
		[[nodiscard]] MultiwayBlueprintTrainingCheckpoint checkpoint() const;
		/** Returns telemetry for configured external checkpoint storage. */
		[[nodiscard]] MultiwayExternalStorageTelemetry external_storage_telemetry(bool include_disk_usage = true) const;
		/** Installs an observer for external artifact publication events. */
		void set_external_publication_observer(MultiwayExternalPublicationObserver observer, void* context);
		/** Records a process-memory sample at the specified training phase. */
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
