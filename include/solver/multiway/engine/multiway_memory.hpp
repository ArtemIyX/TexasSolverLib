#pragma once

/** @file
 * Memory accounting, preflight estimates, process snapshots, and export gates.
 */

#include "core/atomic_publish.hpp"
#include "solver/multiway/engine/multiway_solver.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>

namespace texas::solver::multiway
{

	class MultiwayBucketRegistry;
	class MultiwayRootBatchRunner;
	class MultiwayFixedContinuationSelector;
	class MultiwayContinuationCache;

	/** Training/export lifecycle boundary associated with memory telemetry. */
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
	inline constexpr std::uint32_t MULTIWAY_PROCESS_MEMORY_SCHEMA_VERSION = 2U;
	inline constexpr std::uint32_t MULTIWAY_MEMORY_SUBSYSTEM_SCHEMA_VERSION = 1U;
	inline constexpr std::uint32_t MULTIWAY_MEMORY_ESTIMATE_SCHEMA_VERSION = 3U;
	inline constexpr std::uint32_t MULTIWAY_EXPORT_TELEMETRY_SCHEMA_VERSION = 1U;
	inline constexpr std::size_t MULTIWAY_MEMORY_MAX_WORKERS = 16U;
	inline constexpr std::uint64_t MULTIWAY_DEFAULT_PROCESS_WORKING_SET_LIMIT_BYTES =
		48ULL * 1024ULL * 1024ULL * 1024ULL;
	inline constexpr std::uint64_t MULTIWAY_COMPAT_PROCESS_WORKING_SET_LIMIT_BYTES =
		64ULL * 1024ULL * 1024ULL * 1024ULL;
	inline constexpr std::uint64_t MULTIWAY_DEFAULT_SYSTEM_MEMORY_RESERVE_BYTES =
		1536ULL * 1024ULL * 1024ULL;

	/** Live and retained-capacity bytes attributed to one subsystem component. */
	struct MultiwayMemoryComponent
	{
		std::uint64_t live_element_bytes = 0U;
		std::uint64_t retained_capacity_bytes = 0U;
		std::uint64_t allocation_count = 0U;
		bool allocation_count_known = false;
		bool arithmetic_saturated = false;
		bool grouping_only = false;

		/** Adds live and retained buffer capacity to this component. */
		void add_buffer(
			std::size_t live_elements,
			std::size_t retained_elements,
			std::size_t element_size) noexcept;
		/** Adds another component's counts and byte totals. */
		void accumulate(const MultiwayMemoryComponent& other) noexcept;
		void mark_grouping_only() noexcept { grouping_only = true; }
		/** Checks accounting flags and byte totals for internal consistency. */
		[[nodiscard]] bool valid() const noexcept;
	};

	/** Adds byte counts, saturating at the uint64 maximum on overflow. */
	[[nodiscard]] std::uint64_t multiway_memory_saturating_add(
		std::uint64_t left, std::uint64_t right) noexcept;
	/** Computes allocation capacity bytes with saturation on overflow. */
	[[nodiscard]] std::uint64_t multiway_memory_saturating_capacity_bytes(
		std::size_t element_count, std::size_t element_size) noexcept;

	/** Memory breakdown for bucket metadata and mapped artifacts. */
	struct MultiwayBucketRegistryMemorySnapshot
	{
		MultiwayMemoryComponent owned_metadata{};
		MultiwayMemoryComponent mapping_virtual{};
		MultiwayMemoryComponent total{};

		[[nodiscard]] bool valid() const noexcept;
	};

	/** Memory breakdown for public catalog records, indexes, menus, and scratch. */
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

	/** Memory breakdown for floating-point sparse rows and accumulator cells. */
	struct MultiwaySparseStorageMemorySnapshot
	{
		MultiwayMemoryComponent row_metadata{};
		MultiwayMemoryComponent regret_cells{};
		MultiwayMemoryComponent strategy_cells{};
		MultiwayMemoryComponent total{};

		[[nodiscard]] bool valid() const noexcept;
	};

	/** Memory breakdown for compact row/page metadata and fixed-point cells. */
	struct MultiwayCompactStorageMemorySnapshot
	{
		MultiwayMemoryComponent row_metadata{};
		MultiwayMemoryComponent page_metadata{};
		MultiwayMemoryComponent regret_cells{};
		MultiwayMemoryComponent strategy_cells{};
		MultiwayMemoryComponent total{};

		[[nodiscard]] bool valid() const noexcept;
	};

	/** Memory components retained by one traversal worker. */
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

	/** Memory components retained by the solver coordinator. */
	struct MultiwayCoordinatorMemorySnapshot
	{
		MultiwayMemoryComponent merge_scratch{};
		MultiwayMemoryComponent admission_scratch{};
		MultiwayMemoryComponent total{};

		[[nodiscard]] bool valid() const noexcept;
	};

	/** Per-worker and shared memory used by a root batch runner. */
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

	/** Schema-versioned memory accounting for one training lifecycle phase. */
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

		/** Recomputes aggregate byte totals and saturation state. */
		void recompute_totals() noexcept;
		[[nodiscard]] bool valid() const noexcept;
	};

	/** Bounded ordered collection of subsystem snapshots by lifecycle phase. */
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

	/** Captures accounting from live solver components and retained buffers. */
	[[nodiscard]] MultiwayMemorySubsystemSnapshot capture_multiway_memory_subsystems(
		const MultiwayBucketRegistry* buckets,
		const MultiwaySolverCoordinator* coordinator,
		const MultiwayRootBatchRunner* batch_runner,
		const MultiwayFixedContinuationSelector* continuation_selector = nullptr,
		const MultiwayContinuationCache* continuation_cache = nullptr,
		MultiwayMemoryComponent checkpoint_buffers = {},
		MultiwayMemoryComponent export_buffers = {}) noexcept;

	/** Process working-set, commit, virtual-memory, and page-fault sample. */
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
		std::uint64_t minor_page_faults = 0U;
		std::uint64_t major_page_faults = 0U;
		std::uint64_t total_page_faults = 0U;
		bool current_working_set_available = false;
		bool peak_working_set_available = false;
		bool private_committed_available = false;
		bool peak_private_committed_available = false;
		bool virtual_address_available = false;
		bool anonymous_resident_available = false;
		bool file_resident_available = false;
		bool minor_page_faults_available = false;
		bool major_page_faults_available = false;
		bool total_page_faults_available = false;

		[[nodiscard]] bool valid() const noexcept;
	};

	/** Bounded process-memory samples collected at named lifecycle phases. */
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

	/** Returns a stable name for a memory lifecycle phase. */
	[[nodiscard]] const char* multiway_memory_phase_name(MultiwayMemoryPhase phase) noexcept;
	/** Captures process memory fields available on the current platform. */
	[[nodiscard]] MultiwayProcessMemorySnapshot capture_multiway_process_memory_snapshot() noexcept;

	/** Export row/action counts and serialized-size inputs for estimates. */
	struct MultiwayExportPlanMemoryValues
	{
		std::uint32_t schema_version = MULTIWAY_EXPORT_TELEMETRY_SCHEMA_VERSION;
		std::uint64_t shape_count = 0U;
		std::uint64_t row_count = 0U;
		std::uint64_t action_count = 0U;
		std::uint64_t serialized_bytes = 0U;
		std::uint64_t order_index_bytes = 0U;
		bool compact_direct_order = false;
		std::uint64_t sharding_metadata_bytes = 0U;

		[[nodiscard]] bool valid() const noexcept;
	};

	/** Estimated temporary allocations needed by an export operation. */
	struct MultiwayExportScratchEstimate
	{
		std::uint32_t schema_version = MULTIWAY_EXPORT_TELEMETRY_SCHEMA_VERSION;
		std::uint64_t order_index_bytes = 0U;
		std::uint64_t sharding_metadata_bytes = 0U;
		std::uint64_t strategy_scratch_bytes = 0U;
		std::uint64_t action_scratch_bytes = 0U;
		std::uint64_t encoder_buffer_bytes = 0U;
		std::uint64_t exporter_headroom_bytes = 0U;
		std::uint64_t total_bytes = 0U;
		bool arithmetic_saturated = false;

		[[nodiscard]] bool valid() const noexcept;
	};

	inline constexpr std::uint64_t MULTIWAY_DEFAULT_EXPORTER_HEADROOM_BYTES = 64ULL * 1024ULL * 1024ULL;
	inline constexpr std::uint64_t MULTIWAY_DEFAULT_EXPORT_ENCODER_BUFFER_BYTES = 1ULL * 1024ULL * 1024ULL;
	inline constexpr std::uint64_t MULTIWAY_DEFAULT_CHECKPOINT_WRITER_BUFFER_BYTES = 64ULL * 1024ULL;

	/** Projected process working set while an export is in progress. */
	struct MultiwayExportProcessEstimate
	{
		std::uint32_t schema_version = MULTIWAY_EXPORT_TELEMETRY_SCHEMA_VERSION;
		std::uint64_t current_working_set_bytes = 0U;
		std::uint64_t order_index_bytes = 0U;
		std::uint64_t sharding_metadata_bytes = 0U;
		std::uint64_t strategy_scratch_bytes = 0U;
		std::uint64_t action_scratch_bytes = 0U;
		std::uint64_t encoder_buffer_bytes = 0U;
		std::uint64_t exporter_headroom_bytes = 0U;
		std::uint64_t mapped_residency_allowance_bytes = 0U;
		std::uint64_t estimated_process_export_peak_bytes = 0U;
		bool current_working_set_available = false;
		bool arithmetic_saturated = false;

		[[nodiscard]] bool valid() const noexcept;
	};

	/** Result state shared by process and system export admission checks. */
	enum class MultiwayExportGateStatus : std::uint8_t
	{
		Disabled = 0U,
		Admitted = 1U,
		Rejected = 2U,
		Unavailable = 3U,
	};

	enum class MultiwayExportProcessRejectionReason : std::uint8_t
	{
		None = 0U,
		CurrentWorkingSetUnavailable = 1U,
		EstimateOverflow = 2U,
		ProcessLimitExceeded = 3U,
	};

	/** Hard process limit and scratch allowances for export admission. */
	struct MultiwayExportProcessGateConfig
	{
		std::uint64_t hard_limit_bytes = MULTIWAY_DEFAULT_PROCESS_WORKING_SET_LIMIT_BYTES;
		std::uint64_t exporter_headroom_bytes = MULTIWAY_DEFAULT_EXPORTER_HEADROOM_BYTES;
		std::uint64_t encoder_buffer_bytes = MULTIWAY_DEFAULT_EXPORT_ENCODER_BUFFER_BYTES;
		std::uint64_t mapped_residency_allowance_bytes = 0U;

		void validate() const;
	};

	/** Process gate decision, rejection cause, and computed estimate. */
	struct MultiwayExportProcessGateResult
	{
		std::uint32_t schema_version = MULTIWAY_EXPORT_TELEMETRY_SCHEMA_VERSION;
		MultiwayExportGateStatus status = MultiwayExportGateStatus::Disabled;
		MultiwayExportProcessRejectionReason rejection_reason = MultiwayExportProcessRejectionReason::None;
		std::uint64_t hard_limit_bytes = 0U;
		std::uint64_t compared_process_working_set_bytes = 0U;
		MultiwayExportProcessEstimate estimate{};
		const char* message = "disabled";

		[[nodiscard]] bool blocked() const noexcept
		{
			return status == MultiwayExportGateStatus::Rejected || status == MultiwayExportGateStatus::Unavailable;
		}
	};

	/** Physical-memory totals and availability from the current system. */
	struct MultiwaySystemMemorySnapshot
	{
		std::uint32_t schema_version = MULTIWAY_EXPORT_TELEMETRY_SCHEMA_VERSION;
		std::uint64_t total_physical_bytes = 0U;
		std::uint64_t available_physical_bytes = 0U;
		std::uint64_t used_physical_bytes = 0U;
		bool total_physical_available = false;
		bool available_physical_available = false;

		[[nodiscard]] bool valid() const noexcept;
	};

	enum class MultiwaySystemMemoryGuardStatus : std::uint8_t
	{
		Disabled = 0U,
		Admitted = 1U,
		Rejected = 2U,
		Unavailable = 3U,
	};

	/** Minimum available physical memory required to start or continue work. */
	struct MultiwaySystemMemoryGuardConfig
	{
		bool enabled = false;
		std::uint64_t minimum_available_physical_bytes = MULTIWAY_DEFAULT_SYSTEM_MEMORY_RESERVE_BYTES;

		void validate() const;
	};

	/** System-memory guard decision and measured available-memory values. */
	struct MultiwaySystemMemoryGuardResult
	{
		MultiwaySystemMemoryGuardStatus status = MultiwaySystemMemoryGuardStatus::Disabled;
		std::uint64_t total_physical_bytes = 0U;
		std::uint64_t available_physical_bytes = 0U;
		std::uint64_t used_physical_bytes = 0U;
		std::uint64_t maximum_used_physical_bytes = 0U;
		std::uint64_t minimum_available_physical_bytes = 0U;
		const char* rejected_category = "none";
		const char* message = "disabled";

		[[nodiscard]] bool blocked() const noexcept
		{
			return status == MultiwaySystemMemoryGuardStatus::Rejected || status == MultiwaySystemMemoryGuardStatus::Unavailable;
		}
	};

	enum class MultiwayExportSystemRejectionReason : std::uint8_t
	{
		None = 0U,
		AvailablePhysicalMemoryUnavailable = 1U,
		AllowanceOverflow = 2U,
		AvailablePhysicalMemoryInsufficient = 3U,
	};

	/** System reserve required in addition to serialized and scratch bytes. */
	struct MultiwayExportSystemGateConfig
	{
		bool enabled = false;
		std::uint64_t system_reserve_bytes = 0U;

		void validate() const;
	};

	/** System-memory gate decision, measured availability, and required allowance. */
	struct MultiwayExportSystemGateResult
	{
		std::uint32_t schema_version = MULTIWAY_EXPORT_TELEMETRY_SCHEMA_VERSION;
		MultiwayExportGateStatus status = MultiwayExportGateStatus::Disabled;
		MultiwayExportSystemRejectionReason rejection_reason = MultiwayExportSystemRejectionReason::None;
		std::uint64_t available_physical_bytes = 0U;
		std::uint64_t serialized_output_bytes = 0U;
		std::uint64_t exporter_scratch_bytes = 0U;
		std::uint64_t system_reserve_bytes = 0U;
		std::uint64_t required_available_bytes = 0U;
		const char* message = "disabled";

		[[nodiscard]] bool blocked() const noexcept
		{
			return status == MultiwayExportGateStatus::Rejected || status == MultiwayExportGateStatus::Unavailable;
		}
	};

	enum class MultiwayExportDiskGateStatus : std::uint8_t
	{
		NotChecked = 0U,
		Admitted = 1U,
		Rejected = 2U,
		Unavailable = 3U,
	};

	enum class MultiwayExportDiskRejectionReason : std::uint8_t
	{
		None = 0U,
		FreeSpaceUnavailable = 1U,
		RequiredSpaceOverflow = 2U,
		InsufficientFreeSpace = 3U,
	};

	/** Disk-space gate result and required temporary/output bytes. */
	struct MultiwayExportDiskGateResult
	{
		MultiwayExportDiskGateStatus status = MultiwayExportDiskGateStatus::NotChecked;
		MultiwayExportDiskRejectionReason rejection_reason = MultiwayExportDiskRejectionReason::None;
		std::uint64_t serialized_output_bytes = 0U;
		std::uint64_t temporary_overlap_bytes = 0U;
		std::uint64_t reserve_bytes = 0U;
		std::uint64_t required_free_bytes = 0U;
		std::uint64_t available_free_bytes = 0U;

		[[nodiscard]] bool blocked() const noexcept
		{
			return status == MultiwayExportDiskGateStatus::Rejected || status == MultiwayExportDiskGateStatus::Unavailable;
		}
	};

	/** Checks disk space for output plus configured reserve. */
	[[nodiscard]] MultiwayExportDiskGateResult evaluate_multiway_export_disk_gate(
		std::uint64_t serialized_output_bytes, std::uint64_t reserve_bytes,
		bool available_free_bytes_available, std::uint64_t available_free_bytes) noexcept;
	/** Checks disk space including temporary output overlap. */
	[[nodiscard]] MultiwayExportDiskGateResult evaluate_multiway_export_disk_gate(
		std::uint64_t serialized_output_bytes, std::uint64_t temporary_overlap_bytes,
		std::uint64_t reserve_bytes, bool available_free_bytes_available,
		std::uint64_t available_free_bytes) noexcept;

	/** Before/after working-set and private-commit deltas for an export. */
	struct MultiwayExportObservedMemoryDelta
	{
		std::uint32_t schema_version = MULTIWAY_EXPORT_TELEMETRY_SCHEMA_VERSION;
		std::uint64_t before_working_set_bytes = 0U;
		std::uint64_t after_working_set_bytes = 0U;
		std::uint64_t working_set_delta_bytes = 0U;
		std::uint64_t before_private_committed_bytes = 0U;
		std::uint64_t after_private_committed_bytes = 0U;
		std::uint64_t private_committed_delta_bytes = 0U;
		bool before_working_set_available = false;
		bool after_working_set_available = false;
		bool before_private_committed_available = false;
		bool after_private_committed_available = false;
		bool working_set_delta_decreased = false;
		bool private_committed_delta_decreased = false;

		[[nodiscard]] bool valid() const noexcept;
	};

	/** Combined process and system memory gate settings for artifact export. */
	struct MultiwayExportMemoryGateConfig
	{
		MultiwayExportProcessGateConfig process{};
		MultiwayExportSystemGateConfig system{};

		void validate() const;
	};

	/** Estimates temporary memory needed from a serialized export plan. */
	[[nodiscard]] MultiwayExportScratchEstimate make_multiway_export_scratch_estimate(
		const MultiwayExportPlanMemoryValues& plan,
		MultiwayExportProcessGateConfig config) noexcept;
	/** Combines a process snapshot and plan into a projected export peak. */
	[[nodiscard]] MultiwayExportProcessEstimate make_multiway_export_process_estimate(
		const MultiwayExportPlanMemoryValues& plan,
		const MultiwayProcessMemorySnapshot& snapshot,
		MultiwayExportProcessGateConfig config) noexcept;
	/** Admits or rejects export using the process working-set limit. */
	[[nodiscard]] MultiwayExportProcessGateResult evaluate_multiway_export_process_gate(
		const MultiwayExportPlanMemoryValues& plan,
		const MultiwayProcessMemorySnapshot& snapshot,
		MultiwayExportProcessGateConfig config) noexcept;
	/** Captures physical memory totals exposed by the current platform. */
	[[nodiscard]] MultiwaySystemMemorySnapshot capture_multiway_system_memory_snapshot() noexcept;
	/** Applies a minimum-available-memory guard to a system snapshot. */
	[[nodiscard]] MultiwaySystemMemoryGuardResult evaluate_multiway_system_memory_guard(
		MultiwaySystemMemorySnapshot snapshot,
		MultiwaySystemMemoryGuardConfig config) noexcept;
	/** Admits or rejects export using system availability and reserve settings. */
	[[nodiscard]] MultiwayExportSystemGateResult evaluate_multiway_export_system_gate(
		const MultiwayExportPlanMemoryValues& plan,
		const MultiwayExportScratchEstimate& scratch,
		const MultiwaySystemMemorySnapshot& snapshot,
		MultiwayExportSystemGateConfig config) noexcept;
	/** Computes observed process memory deltas around an export operation. */
	[[nodiscard]] MultiwayExportObservedMemoryDelta observe_multiway_export_memory_delta(
		const MultiwayProcessMemorySnapshot& before,
		const MultiwayProcessMemorySnapshot& after) noexcept;
	/** Returns the display name of a process-gate rejection reason. */
	[[nodiscard]] const char* multiway_export_process_rejection_reason_name(
		MultiwayExportProcessRejectionReason reason) noexcept;
	/** Returns the display name of a system-gate rejection reason. */
	[[nodiscard]] const char* multiway_export_system_rejection_reason_name(
		MultiwayExportSystemRejectionReason reason) noexcept;

	/** Lookup and page-cache counters from one external export component. */
	struct MultiwayExportStorageCounters
	{
		std::uint64_t lookups = 0U;
		std::uint64_t cache_hits = 0U;
		std::uint64_t cache_misses = 0U;
		std::uint64_t run_probes = 0U;
		std::uint64_t read_nanoseconds = 0U;
		std::uint64_t page_hits = 0U;
		std::uint64_t page_misses = 0U;
	};

	// Fixed-size, opt-in diagnostics. Times are inclusive unless named otherwise.
	struct MultiwayExternalExportProfile
	{
		bool enabled = false;
		std::uint64_t shapes = 0U;
		std::uint64_t pages = 0U;
		std::uint64_t rows = 0U;
		std::uint64_t actions = 0U;
		std::uint64_t encoded_bytes = 0U;
		std::uint64_t stream_write_bytes = 0U;
		std::uint64_t ordered_visit_nanoseconds = 0U;
		std::uint64_t visitor_nanoseconds = 0U;
		std::uint64_t menu_nanoseconds = 0U;
		std::uint64_t pin_nanoseconds = 0U;
		std::uint64_t unpin_nanoseconds = 0U;
		std::uint64_t page_locate_nanoseconds = 0U;
		std::uint64_t page_cache_find_nanoseconds = 0U;
		std::uint64_t page_victim_nanoseconds = 0U;
		std::uint64_t page_store_lookup_nanoseconds = 0U;
		std::uint64_t page_decode_nanoseconds = 0U;
		std::uint64_t page_zero_nanoseconds = 0U;
		std::uint64_t page_decoded_count = 0U;
		std::uint64_t page_zero_count = 0U;
		std::uint64_t page_index_opens = 0U;
		std::uint64_t page_index_reads = 0U;
		std::uint64_t page_run_range_skips = 0U;
		std::uint64_t page_membership_misses = 0U;
		std::uint64_t page_data_opens = 0U;
		std::uint64_t page_data_reads = 0U;
		std::uint64_t page_requested_read_bytes = 0U;
		std::uint64_t page_index_open_nanoseconds = 0U;
		std::uint64_t page_index_search_nanoseconds = 0U;
		std::uint64_t page_data_open_nanoseconds = 0U;
		std::uint64_t page_data_read_nanoseconds = 0U;
		std::uint64_t finish_nanoseconds = 0U;
		std::uint64_t sampled_rows = 0U;
		std::uint64_t sampled_average_nanoseconds = 0U;
		std::uint64_t sampled_quantize_nanoseconds = 0U;
		std::uint64_t sampled_writer_nanoseconds = 0U;
		std::uint64_t writer_sampled_rows = 0U;
		std::uint64_t writer_sampled_validation_nanoseconds = 0U;
		std::uint64_t writer_sampled_payload_hash_nanoseconds = 0U;
		std::uint64_t writer_sampled_policy_hash_nanoseconds = 0U;
		std::uint64_t writer_sampled_encoding_nanoseconds = 0U;
		std::uint64_t writer_flushes = 0U;
		MultiwayExportStorageCounters catalog{};
		MultiwayExportStorageCounters row_index{};
		MultiwayExportStorageCounters page_store{};
	};

	// Invoked after a page is unpinned. The view is transient; do not re-enter storage.
	using MultiwayExternalExportProgressObserver = void (*)(
		const MultiwayExternalExportProfile&, std::uint64_t elapsed_nanoseconds,
		void* context) noexcept;

	/** End-to-end export plan, gates, sizes, hashes, timing, and outcome. */
	struct MultiwayExportTelemetry
	{
		std::uint32_t schema_version = MULTIWAY_EXPORT_TELEMETRY_SCHEMA_VERSION;
		bool attempted = false;
		bool succeeded = false;
		bool sharded_output = false;
		std::uint64_t shard_count = 0U;
		std::uint64_t manifest_bytes = 0U;
		MultiwayExportPlanMemoryValues plan{};
		MultiwayExportScratchEstimate scratch{};
		MultiwayExportProcessEstimate process_estimate{};
		MultiwayExportProcessGateResult process_gate{};
		MultiwaySystemMemorySnapshot system_before{};
		MultiwaySystemMemorySnapshot system_after{};
		MultiwayExportSystemGateResult system_gate{};
		MultiwayExportDiskGateResult disk_gate{};
		MultiwayExportObservedMemoryDelta observed_delta{};
		std::uint64_t result_bytes = 0U;
		std::uint64_t payload_hash = 0U;
		std::uint64_t policy_content_hash = 0U;
		std::uint64_t duration_nanoseconds = 0U;
		MultiwayExternalExportProfile external_profile{};
		core::AtomicPublishMode publication_mode = core::AtomicPublishMode::Unknown;

		/** Checks schema version, gate outcomes, estimates, and result values. */
		void validate() const;
	};

	/** Warning, operating, and hard rejection byte thresholds for preflight. */
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

		/** Checks ordering and consistency of the three thresholds. */
		void validate() const;
	};

	/** Measured baselines and optional capacity inputs to memory preflight. */
	struct MultiwayMemoryInputs
	{
		// Capacity bounds for the packed public catalog. Zero menu/action bounds
		// select the conservative per-state defaults used by preflight.
		std::uint64_t public_catalog_menu_capacity = 0U;
		std::uint64_t public_catalog_action_capacity = 0U;
		std::uint64_t public_catalog_admission_scratch_bytes = 0U;
		std::uint64_t compact_reallocation_peak_bytes = 0U;
		std::uint64_t external_storage_cache_bytes = 0U;
		std::uint64_t external_storage_scratch_bytes = 0U;
		std::uint64_t external_storage_disk_reserve_bytes = 0U;
		std::uint64_t file_cache_allowance_bytes = 0U;
		std::uint64_t disk_output_bytes = 0U;
		// Task A snapshots taken after immutable artifacts are loaded. These
		// baselines already include resident pages of the loaded mapping.
		std::uint64_t existing_working_set_bytes = 0U;
		std::uint64_t existing_private_committed_bytes = 0U;
		// Full mapping size is a virtual-address bound. The resident allowance is
		// only the additional mapping residency not represented by the baseline.
		std::uint64_t mapped_virtual_bytes = 0U;
		std::uint64_t mapped_resident_allowance_bytes = 0U;
		// Task B retained capacity that is known to be an actual additional
		// allocation. False keeps the configured-capacity growth estimate.
		std::uint64_t retained_capacity_bytes = 0U;
		bool retained_capacity_known = false;

		// Legacy fields remain accepted with their original serialized meanings.
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
		// Scratch only. Serialized output bytes belong to disk_output_bytes.
		std::uint64_t checkpoint_peak_bytes = 0U;
		std::uint64_t export_peak_bytes = 0U;
		std::uint64_t allocator_overhead_bytes = 0U;
	};

	struct MultiwayCatalogCapacityEstimate
	{
		std::uint64_t node_bytes = 0U;
		std::uint64_t index_bytes = 0U;
		std::uint64_t menu_header_bytes = 0U;
		std::uint64_t menu_action_bytes = 0U;
		std::uint64_t admission_scratch_bytes = 0U;
		std::uint64_t reallocation_peak_bytes = 0U;
		std::uint64_t total_bytes = 0U;
	};

	[[nodiscard]] MultiwayCatalogCapacityEstimate estimate_multiway_catalog_capacity(
		std::uint64_t state_capacity,
		std::uint64_t menu_capacity,
		std::uint64_t action_capacity) noexcept;

	/** Named memory allocations and peak estimates retained in reports. */
	struct MultiwayMemoryLedger
	{
		std::uint32_t schema_version = MULTIWAY_MEMORY_ESTIMATE_SCHEMA_VERSION;
		std::uint64_t existing_working_set_bytes = 0U;
		std::uint64_t existing_private_committed_bytes = 0U;
		std::uint64_t additional_private_capacity_bytes = 0U;
		std::uint64_t compact_reallocation_peak_bytes = 0U;
		std::uint64_t external_storage_cache_bytes = 0U;
		std::uint64_t external_storage_scratch_bytes = 0U;
		std::uint64_t external_storage_disk_reserve_bytes = 0U;
		std::uint64_t public_catalog_reallocation_peak_bytes = 0U;
		std::uint64_t file_cache_allowance_bytes = 0U;
		std::uint64_t disk_output_bytes = 0U;
		std::uint64_t mapped_virtual_bytes = 0U;
		std::uint64_t mapped_resident_allowance_bytes = 0U;
		std::uint64_t checkpoint_transient_bytes = 0U;
		std::uint64_t export_transient_bytes = 0U;
		std::uint64_t export_scratch_bytes = 0U;
		std::uint64_t export_output_bytes = 0U;
		std::uint64_t export_process_peak_bytes = 0U;
		std::uint64_t export_system_cache_allowance_bytes = 0U;
		std::uint64_t export_observed_before_working_set_bytes = 0U;
		std::uint64_t export_observed_after_working_set_bytes = 0U;
		std::uint64_t estimated_private_peak_bytes = 0U;
		std::uint64_t estimated_working_set_peak_bytes = 0U;
		std::uint64_t configured_capacity_growth_bytes = 0U;
		std::uint64_t actual_retained_capacity_bytes = 0U;

		// Legacy fields retain their original report names and meanings.
		std::uint64_t resident_baseline_bytes = 0U;
		std::uint64_t bucket_registry_bytes = 0U;
		std::uint64_t mapped_bucket_artifact_bytes = 0U;
		std::uint64_t public_catalog_bytes = 0U;
		std::uint64_t row_metadata_bytes = 0U;
		std::uint64_t page_metadata_bytes = 0U;
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

	/** Estimated solver memory across resident, retained, and transient classes. */
	struct MultiwayMemoryEstimate
	{
		MultiwayMemoryLedger ledger{};
		std::uint64_t existing_working_set_bytes = 0U;
		std::uint64_t existing_private_committed_bytes = 0U;
		std::uint64_t additional_private_capacity_bytes = 0U;
		std::uint64_t compact_reallocation_peak_bytes = 0U;
		std::uint64_t external_storage_cache_bytes = 0U;
		std::uint64_t external_storage_scratch_bytes = 0U;
		std::uint64_t external_storage_disk_reserve_bytes = 0U;
		std::uint64_t public_catalog_reallocation_peak_bytes = 0U;
		std::uint64_t file_cache_allowance_bytes = 0U;
		std::uint64_t disk_output_bytes = 0U;
		std::uint64_t mapped_virtual_bytes = 0U;
		std::uint64_t mapped_resident_allowance_bytes = 0U;
		std::uint64_t checkpoint_transient_bytes = 0U;
		std::uint64_t export_transient_bytes = 0U;
		std::uint64_t allocator_overhead_bytes = 0U;
		std::uint64_t estimated_private_peak_bytes = 0U;
		std::uint64_t estimated_working_set_peak_bytes = 0U;
		std::uint64_t configured_capacity_growth_bytes = 0U;
		std::uint64_t actual_retained_capacity_bytes = 0U;
		std::uint64_t public_state_bytes = 0;
		std::uint64_t sparse_row_bytes = 0;
		std::uint64_t compact_page_bytes = 0U;
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

	/** Preflight admission result and capacity stage reached. */
	struct MultiwayMemoryPreflight
	{
		MultiwayMemoryStatus status = MultiwayMemoryStatus::Ok;
		MultiwayMemoryEstimate estimate{};
		MultiwayMemoryAdmissionStage highest_admitted_stage = MultiwayMemoryAdmissionStage::None;
		bool optional_continuation_cache_admitted = false;
		bool degraded = false;
		const char* rejected_category = "none";
		const char* message = "ok";
	};

	enum class MultiwayWorkingSetGatePhase : std::uint8_t
	{
		None = 0U,
		Preflight = 1U,
		Runtime = 2U,
	};

	enum class MultiwayWorkingSetGateStatus : std::uint8_t
	{
		Disabled = 0U,
		Admitted = 1U,
		Rejected = 2U,
		Unavailable = 3U,
	};

	/** Process working-set and private-commit limits for training gates. */
	struct MultiwayWorkingSetGateConfig
	{
		std::uint64_t hard_limit_bytes = MULTIWAY_DEFAULT_PROCESS_WORKING_SET_LIMIT_BYTES;
		std::uint64_t resident_allowance_bytes = 0U;
		std::uint64_t private_commit_limit_bytes = 0U;

		/** Checks that configured byte limits are internally consistent. */
		void validate() const;
	};

	/** Working-set gate outcome and observed or estimated memory values. */
	struct MultiwayWorkingSetGateResult
	{
		MultiwayWorkingSetGateStatus status = MultiwayWorkingSetGateStatus::Disabled;
		MultiwayWorkingSetGatePhase phase = MultiwayWorkingSetGatePhase::None;
		MultiwayMemoryAdmissionStage admission_stage = MultiwayMemoryAdmissionStage::None;
		std::uint64_t hard_limit_bytes = 0U;
		std::uint64_t resident_allowance_bytes = 0U;
		std::uint64_t estimated_working_set_bytes = 0U;
		std::uint64_t compared_working_set_bytes = 0U;
		std::uint64_t observed_working_set_bytes = 0U;
		std::uint64_t observed_private_commit_bytes = 0U;
		const char* rejected_category = "none";
		const char* message = "disabled";

		[[nodiscard]] bool rejected() const noexcept
		{
			return status == MultiwayWorkingSetGateStatus::Rejected;
		}

		[[nodiscard]] bool blocked() const noexcept
		{
			return status == MultiwayWorkingSetGateStatus::Rejected || status == MultiwayWorkingSetGateStatus::Unavailable;
		}
	};

	using MultiwayProcessMemoryObserver = MultiwayProcessMemorySnapshot (*)(const void*) noexcept;
	using MultiwaySystemMemoryObserver = MultiwaySystemMemorySnapshot (*)(const void*) noexcept;
	using MultiwayExportDiskSpaceObserver = bool (*)(const void*,
		const std::filesystem::path&, std::uint64_t&) noexcept;

	/** Sampling callbacks and gate settings used by training/export monitors. */
	struct MultiwayWorkingSetMonitor
	{
		std::uint64_t single_file_blueprint_limit_bytes = 56ULL * 1024ULL * 1024ULL * 1024ULL;
		std::uint64_t shard_max_serialized_bytes = 1024ULL * 1024ULL * 1024ULL;
		MultiwayWorkingSetGateConfig gate{};
		MultiwayProcessMemoryObserver observer = nullptr;
		const void* observer_context = nullptr;
		MultiwayExportMemoryGateConfig export_gate{};
		MultiwayProcessMemoryObserver export_process_observer = nullptr;
		MultiwaySystemMemoryObserver export_system_observer = nullptr;
		MultiwayExportDiskSpaceObserver export_disk_space_observer = nullptr;
		MultiwaySystemMemoryGuardConfig system_memory_guard{};
		MultiwaySystemMemoryObserver system_memory_observer = nullptr;
		const void* export_observer_context = nullptr;
		const void* system_memory_observer_context = nullptr;
		bool profile_external_export = false;
		bool external_export_membership_filter = true;
		MultiwayExternalExportProgressObserver export_progress_observer = nullptr;
		void* export_progress_context = nullptr;
		std::uint64_t export_progress_page_interval = 4096U;

		/** Checks observer and gate settings before attaching the monitor. */
		void validate() const;
	};

	/** Evaluates estimated memory against preflight limits. */
	[[nodiscard]] MultiwayWorkingSetGateResult evaluate_multiway_working_set_preflight(
		const MultiwayMemoryPreflight& preflight,
		MultiwayWorkingSetGateConfig config) noexcept;
	/** Evaluates a live process sample against runtime limits. */
	[[nodiscard]] MultiwayWorkingSetGateResult evaluate_multiway_working_set_runtime(
		MultiwayProcessMemorySnapshot snapshot,
		MultiwayWorkingSetGateConfig config) noexcept;

	// Returns the current process resident set when available. Zero means the
	// platform does not expose the metric.
	/** Returns current resident memory bytes, or zero when unavailable. */
	[[nodiscard]] std::uint64_t observed_multiway_process_memory_bytes() noexcept;
	/** Returns peak working-set bytes, or zero when unavailable. */
	[[nodiscard]] std::uint64_t observed_multiway_process_peak_working_set_bytes() noexcept;
	/** Returns process CPU time used by training, or zero when unavailable. */
	[[nodiscard]] std::uint64_t observed_multiway_training_process_cpu_nanoseconds() noexcept;

	/** Estimates memory use for solver limits with default capacity inputs. */
	[[nodiscard]] MultiwayMemoryPreflight preflight_multiway_memory(
		const MultiwaySolverLimits& limits,
		MultiwayMemoryBudget budget = {});

	/** Estimates memory use using explicit artifact, cache, and baseline inputs. */
	[[nodiscard]] MultiwayMemoryPreflight preflight_multiway_memory(
		const MultiwaySolverLimits& limits,
		MultiwayMemoryBudget budget,
		const MultiwayMemoryInputs& inputs);

} // namespace texas::solver::multiway
