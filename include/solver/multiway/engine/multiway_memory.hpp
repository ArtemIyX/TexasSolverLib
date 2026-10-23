#pragma once

#include "solver/multiway/engine/multiway_solver.hpp"

#include <array>
#include <cstddef>
#include <cstdint>

namespace texas::solver::multiway
{

	class MultiwayBucketRegistry;
	class MultiwayRootBatchRunner;
	class MultiwayFixedContinuationSelector;
	class MultiwayContinuationCache;

	enum class MultiwayMemoryPhase : std::uint8_t
	{
		ProcessStart = 0U,
		BeforeBucketMap = 1U,
		AfterBucketMapMetadata = 2U,
		AfterBucketAssignmentValidation = 3U,
		BeforeMemoryPreflight = 4U,
		AfterCoordinatorConstruction = 5U,
		AfterWorkerPoolConstruction = 6U,
		AfterColdBatch = 7U,
		AfterSteadyBatch = 8U,
		BeforeCheckpoint = 9U,
		AfterCheckpoint = 10U,
		BeforeExport = 11U,
		AfterExport = 12U,
	};

	inline constexpr std::size_t MULTIWAY_MEMORY_PHASE_COUNT = 13U;
	inline constexpr std::uint32_t MULTIWAY_PROCESS_MEMORY_SCHEMA_VERSION = 1U;
	inline constexpr std::uint32_t MULTIWAY_MEMORY_SUBSYSTEM_SCHEMA_VERSION = 1U;
	inline constexpr std::size_t MULTIWAY_MEMORY_MAX_WORKERS = 16U;

	struct MultiwayMemoryComponent
	{
		std::uint64_t live_element_bytes = 0U;
		std::uint64_t retained_capacity_bytes = 0U;
		std::uint64_t allocation_count = 0U;
		bool allocation_count_known = false;
		bool arithmetic_saturated = false;
		bool grouping_only = false;

		void add_buffer(
			std::size_t live_elements,
			std::size_t retained_elements,
			std::size_t element_size) noexcept;
		void accumulate(const MultiwayMemoryComponent& other) noexcept;
		void mark_grouping_only() noexcept { grouping_only = true; }
		[[nodiscard]] bool valid() const noexcept;
	};

	[[nodiscard]] std::uint64_t multiway_memory_saturating_add(
		std::uint64_t left, std::uint64_t right) noexcept;
	[[nodiscard]] std::uint64_t multiway_memory_saturating_capacity_bytes(
		std::size_t element_count, std::size_t element_size) noexcept;

	struct MultiwayBucketRegistryMemorySnapshot
	{
		MultiwayMemoryComponent owned_metadata{};
		MultiwayMemoryComponent mapping_virtual{};
		MultiwayMemoryComponent total{};

		[[nodiscard]] bool valid() const noexcept;
	};

	struct MultiwayPublicCatalogMemorySnapshot
	{
		MultiwayMemoryComponent nodes{};
		MultiwayMemoryComponent indexes{};
		MultiwayMemoryComponent menus{};
		MultiwayMemoryComponent nested_payloads{};
		MultiwayMemoryComponent scratch{};
		MultiwayMemoryComponent total{};

		[[nodiscard]] bool valid() const noexcept;
	};

	struct MultiwaySparseStorageMemorySnapshot
	{
		MultiwayMemoryComponent row_metadata{};
		MultiwayMemoryComponent regret_cells{};
		MultiwayMemoryComponent strategy_cells{};
		MultiwayMemoryComponent total{};

		[[nodiscard]] bool valid() const noexcept;
	};

	struct MultiwayCompactStorageMemorySnapshot
	{
		MultiwayMemoryComponent row_metadata{};
		MultiwayMemoryComponent page_metadata{};
		MultiwayMemoryComponent regret_cells{};
		MultiwayMemoryComponent strategy_cells{};
		MultiwayMemoryComponent total{};

		[[nodiscard]] bool valid() const noexcept;
	};

	struct MultiwayWorkerMemorySnapshot
	{
		MultiwayMemoryComponent regret_delta_stream{};
		MultiwayMemoryComponent continuation_delta_stream{};
		MultiwayMemoryComponent discovery_public_states{};
		MultiwayMemoryComponent discovery_rows{};
		MultiwayMemoryComponent action_menu_scratch{};
		MultiwayMemoryComponent total{};

		[[nodiscard]] bool valid() const noexcept;
	};

	struct MultiwayCoordinatorMemorySnapshot
	{
		MultiwayMemoryComponent merge_scratch{};
		MultiwayMemoryComponent admission_scratch{};
		MultiwayMemoryComponent total{};

		[[nodiscard]] bool valid() const noexcept;
	};

	struct MultiwayRootBatchMemorySnapshot
	{
		std::array<MultiwayWorkerMemorySnapshot, MULTIWAY_MEMORY_MAX_WORKERS> workers{};
		std::uint32_t worker_count = 0U;
		MultiwayMemoryComponent stream_views{};
		MultiwayMemoryComponent discovery_merge_scratch{};
		MultiwayMemoryComponent discovery_outputs{};
		MultiwayMemoryComponent worker_batches{};
		MultiwayMemoryComponent thread_scratch{};
		MultiwayMemoryComponent total{};

		[[nodiscard]] bool valid() const noexcept;
	};

	struct MultiwayMemorySubsystemSnapshot
	{
		std::uint32_t schema_version = MULTIWAY_MEMORY_SUBSYSTEM_SCHEMA_VERSION;
		MultiwayMemoryPhase phase_id = MultiwayMemoryPhase::ProcessStart;
		MultiwayMemoryComponent bucket_registry_owned{};
		MultiwayMemoryComponent bucket_mapping_virtual{};
		MultiwayMemoryComponent public_catalog_nodes{};
		MultiwayMemoryComponent public_catalog_indexes{};
		MultiwayMemoryComponent public_catalog_menus{};
		MultiwayMemoryComponent public_catalog_nested_payloads{};
		MultiwayMemoryComponent public_catalog_scratch{};
		MultiwayMemoryComponent compact_row_metadata{};
		MultiwayMemoryComponent compact_page_metadata{};
		MultiwayMemoryComponent compact_regret_cells{};
		MultiwayMemoryComponent compact_strategy_cells{};
		std::array<MultiwayWorkerMemorySnapshot, MULTIWAY_MEMORY_MAX_WORKERS> workers{};
		std::uint32_t worker_count = 0U;
		MultiwayMemoryComponent worker_regret_delta_stream{};
		MultiwayMemoryComponent worker_continuation_delta_stream{};
		MultiwayMemoryComponent discovery_public_state_stream{};
		MultiwayMemoryComponent discovery_row_stream{};
		MultiwayMemoryComponent worker_action_menu_scratch{};
		MultiwayMemoryComponent discovery_merge_scratch{};
		MultiwayMemoryComponent batch_runner_scratch{};
		MultiwayMemoryComponent coordinator_merge_scratch{};
		MultiwayMemoryComponent coordinator_admission_scratch{};
		MultiwayMemoryComponent checkpoint_retained_buffers{};
		MultiwayMemoryComponent export_retained_buffers{};
		MultiwayMemoryComponent continuation_cache{};
		MultiwayMemoryComponent continuation_scratch{};
		std::uint64_t live_bytes = 0U;
		std::uint64_t retained_capacity_bytes = 0U;
		bool arithmetic_saturated = false;

		void recompute_totals() noexcept;
		[[nodiscard]] bool valid() const noexcept;
	};

	struct MultiwayMemorySubsystemSnapshotCollection
	{
		std::array<MultiwayMemorySubsystemSnapshot, MULTIWAY_MEMORY_PHASE_COUNT> snapshots{};
		std::uint8_t count = 0U;
		bool enabled = false;
		bool completed = false;

		[[nodiscard]] bool record(
			MultiwayMemoryPhase phase,
			MultiwayMemorySubsystemSnapshot snapshot) noexcept;
		[[nodiscard]] bool valid() const noexcept;
	};

	[[nodiscard]] MultiwayMemorySubsystemSnapshot capture_multiway_memory_subsystems(
		const MultiwayBucketRegistry* buckets,
		const MultiwaySolverCoordinator* coordinator,
		const MultiwayRootBatchRunner* batch_runner,
		const MultiwayFixedContinuationSelector* continuation_selector = nullptr,
		const MultiwayContinuationCache* continuation_cache = nullptr,
		MultiwayMemoryComponent checkpoint_buffers = {},
		MultiwayMemoryComponent export_buffers = {}) noexcept;

	struct MultiwayProcessMemorySnapshot
	{
		MultiwayMemoryPhase phase_id = MultiwayMemoryPhase::ProcessStart;
		std::uint64_t current_working_set_bytes = 0U;
		std::uint64_t peak_working_set_bytes = 0U;
		std::uint64_t private_committed_bytes = 0U;
		std::uint64_t peak_private_committed_bytes = 0U;
		std::uint64_t virtual_address_bytes = 0U;
		std::uint64_t anonymous_resident_bytes = 0U;
		std::uint64_t file_resident_bytes = 0U;
		bool current_working_set_available = false;
		bool peak_working_set_available = false;
		bool private_committed_available = false;
		bool peak_private_committed_available = false;
		bool virtual_address_available = false;
		bool anonymous_resident_available = false;
		bool file_resident_available = false;

		[[nodiscard]] bool valid() const noexcept;
	};

	struct MultiwayProcessMemorySnapshotCollection
	{
		std::array<MultiwayProcessMemorySnapshot, MULTIWAY_MEMORY_PHASE_COUNT> snapshots{};
		std::uint8_t count = 0U;
		bool enabled = false;
		bool completed = false;

		[[nodiscard]] bool record(
			MultiwayMemoryPhase phase,
			MultiwayProcessMemorySnapshot snapshot) noexcept;
		[[nodiscard]] bool record_current(MultiwayMemoryPhase phase) noexcept;
		[[nodiscard]] bool valid() const noexcept;
	};

	[[nodiscard]] const char* multiway_memory_phase_name(MultiwayMemoryPhase phase) noexcept;
	[[nodiscard]] MultiwayProcessMemorySnapshot capture_multiway_process_memory_snapshot() noexcept;

	struct MultiwayMemoryBudget
	{
		std::uint64_t warning_bytes = 48ULL * 1024ULL * 1024ULL * 1024ULL;
		std::uint64_t operating_bytes = 56ULL * 1024ULL * 1024ULL * 1024ULL;
		std::uint64_t reject_bytes = 60ULL * 1024ULL * 1024ULL * 1024ULL;

		constexpr MultiwayMemoryBudget() noexcept = default;
		constexpr MultiwayMemoryBudget(
			std::uint64_t warning,
			std::uint64_t reject,
			std::uint64_t operating = 0U) noexcept
			: warning_bytes(warning), operating_bytes(operating == 0U ? warning + (reject - warning) / 2U : operating), reject_bytes(reject) {}

		void validate() const;
	};

	struct MultiwayMemoryInputs
	{
		// RSS measured after immutable artifacts are loaded and before solver
		// construction. It is part of the resident admission contract.
		std::uint64_t resident_baseline_bytes = 0U;
		std::uint64_t bucket_registry_bytes = 0U;
		std::uint64_t mapped_bucket_artifact_bytes = 0U;
		std::uint64_t blueprint_index_bytes = 0U;
		std::uint64_t range_row_count = 0U;
		std::uint64_t range_entry_count = 0U;
		std::uint64_t future_bucket_cache_bytes = 0U;
		std::uint64_t off_tree_menu_entries = 0U;
		std::uint64_t continuation_scratch_bytes_per_worker = 0U;
		std::uint64_t continuation_cache_bytes = 0U;
		std::uint64_t export_action_capacity = 0U;
		std::uint64_t checkpoint_peak_bytes = 0U;
		std::uint64_t export_peak_bytes = 0U;
		std::uint64_t allocator_overhead_bytes = 0U;
	};

	struct MultiwayMemoryLedger
	{
		std::uint64_t resident_baseline_bytes = 0U;
		std::uint64_t bucket_registry_bytes = 0U;
		std::uint64_t mapped_bucket_artifact_bytes = 0U;
		std::uint64_t public_catalog_bytes = 0U;
		std::uint64_t row_metadata_bytes = 0U;
		std::uint64_t regret_page_bytes = 0U;
		std::uint64_t worker_stream_bytes = 0U;
		std::uint64_t discovery_stream_bytes = 0U;
		std::uint64_t worker_allocation_count = 0U;
		std::uint64_t worker_allocation_bytes = 0U;
		std::uint64_t coordinator_allocation_count = 0U;
		std::uint64_t coordinator_allocation_bytes = 0U;
		std::uint64_t merge_scratch_bytes = 0U;
		std::uint64_t checkpoint_peak_bytes = 0U;
		std::uint64_t export_peak_bytes = 0U;
		std::uint64_t allocator_overhead_bytes = 0U;
		std::uint64_t live_bytes = 0U;
		std::uint64_t reserved_bytes = 0U;
		std::uint64_t committed_bytes = 0U;
	};

	struct MultiwayMemoryEstimate
	{
		MultiwayMemoryLedger ledger{};
		std::uint64_t public_state_bytes = 0;
		std::uint64_t sparse_row_bytes = 0;
		std::uint64_t sparse_value_bytes = 0;
		std::uint64_t blueprint_index_bytes = 0U;
		std::uint64_t range_row_bytes = 0U;
		std::uint64_t future_bucket_cache_bytes = 0U;
		std::uint64_t off_tree_menu_bytes = 0U;
		std::uint64_t continuation_scratch_bytes = 0U;
		std::uint64_t worker_delta_bytes = 0;
		std::uint64_t discovery_stream_bytes = 0U;
		std::uint64_t merge_scratch_bytes = 0U;
		std::uint64_t export_bytes = 0U;
		std::uint64_t continuation_cache_bytes = 0U;
		std::uint64_t mandatory_bytes = 0U;
		std::uint64_t admitted_bytes = 0U;
		std::uint64_t total_bytes = 0;
		std::uint64_t current_rss_bytes = 0U;
		std::uint64_t peak_rss_bytes = 0U;
	};

	enum class MultiwayMemoryStatus : std::uint8_t
	{
		Ok,
		Warning,
		Rejected,
	};

	enum class MultiwayMemoryAdmissionStage : std::uint8_t
	{
		None = 0U,
		Root = 1U,
		SparseRows = 2U,
		WorkerDeltas = 3U,
		OptionalContinuationCache = 4U,
	};

	struct MultiwayMemoryPreflight
	{
		MultiwayMemoryStatus status = MultiwayMemoryStatus::Ok;
		MultiwayMemoryEstimate estimate{};
		MultiwayMemoryAdmissionStage highest_admitted_stage = MultiwayMemoryAdmissionStage::None;
		bool optional_continuation_cache_admitted = false;
		bool degraded = false;
		const char* message = "ok";
	};

	// Returns the current process resident set when available. Zero means the
	// platform does not expose the metric.
	[[nodiscard]] std::uint64_t observed_multiway_process_memory_bytes() noexcept;
	[[nodiscard]] std::uint64_t observed_multiway_process_peak_working_set_bytes() noexcept;
	[[nodiscard]] std::uint64_t observed_multiway_training_process_cpu_nanoseconds() noexcept;

	[[nodiscard]] MultiwayMemoryPreflight preflight_multiway_memory(
		const MultiwaySolverLimits& limits,
		MultiwayMemoryBudget budget = {});

	[[nodiscard]] MultiwayMemoryPreflight preflight_multiway_memory(
		const MultiwaySolverLimits& limits,
		MultiwayMemoryBudget budget,
		const MultiwayMemoryInputs& inputs);

} // namespace texas::solver::multiway
