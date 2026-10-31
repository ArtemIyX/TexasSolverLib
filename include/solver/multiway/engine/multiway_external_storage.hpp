#pragma once

/** @file
 * Append-only external catalogs, row/page stores, transactions, and checkpoints.
 */

#include "solver/multiway/engine/multiway_external_observer.hpp"

#include <cstddef>
#include <cstdint>
#include <array>
#include <filesystem>
#include <memory>
#include <mutex>
#include <vector>

namespace texas::solver::multiway
{
	struct MultiwayPackedPublicNode;
	struct MultiwayPublicStateId;
	struct MultiwayPublicStateDescriptor;
	struct MultiwayActionDescriptor;
	struct MultiwayInfosetId;
	struct MultiwaySparseRowShape;
	class MultiwayExternalRowIndex;
	class MultiwayExternalRowBinding;

	inline constexpr std::uint32_t MULTIWAY_EXTERNAL_NODE_RECORD_SCHEMA = 1U;
	/** Returns the fixed encoded size of one packed public-node record. */
	[[nodiscard]] std::size_t external_public_node_record_size() noexcept;
	/** Returns the encoded record size used by the external public catalog. */
	[[nodiscard]] std::size_t external_public_catalog_record_size() noexcept;

	// Explicit little-endian codec. The on-disk form never depends on C++ padding.
	/** Encodes a public node in the versioned little-endian wire format. */
	void encode_external_public_node(const MultiwayPackedPublicNode& node,
		std::vector<std::uint8_t>& output);
	/** Decodes a packed public node; returns false for invalid or truncated bytes. */
	[[nodiscard]] bool decode_external_public_node(const std::uint8_t* bytes,
		std::size_t size, MultiwayPackedPublicNode& output) noexcept;
	/** Directory, cache, durability, compaction, and observer settings for a store. */
	struct MultiwayExternalStorageConfig
	{
		std::filesystem::path directory;
		std::uint64_t cache_bytes = 0U;
		std::uint64_t maximum_wal_bytes = 0U;
		std::uint64_t segment_target_bytes = 0U;
		std::uint32_t compaction_fan_in = 0U;
		std::uint64_t compaction_scratch_bytes = 0U;
		std::uint64_t disk_reserve_bytes = 0U;
		bool synchronous_durability = true;
		std::uint32_t disk_amplification_limit = 0U;
		std::filesystem::path disk_accounting_root;
		MultiwayExternalPublicationObserver publication_observer = nullptr;
		void* publication_observer_context = nullptr;
		std::uint32_t publication_component = 0U;
		// Set only when a synchronous aggregate training journal owns recovery for
		// every append made through this component store.
		bool aggregate_transaction_covers_append_recovery = false;

		/** Validates storage limits against the record payload size. */
		void validate(std::size_t payload_bytes) const;
	};

	/** Keyed byte payload appended to or retrieved from a record store. */
	struct MultiwayExternalRecord
	{
		std::uint64_t key = 0U;
		std::vector<std::uint8_t> payload;
	};

	/** Cumulative lookup, I/O, cache, compaction, and publication counters. */
	struct MultiwayExternalStorageStats
	{
		std::uint64_t lookups = 0U;
		std::uint64_t cache_hits = 0U;
		std::uint64_t cache_misses = 0U;
		std::uint64_t disk_reads = 0U;
		std::uint64_t cache_evictions = 0U;
		std::uint64_t read_nanoseconds = 0U;
		std::uint64_t page_cache_hits = 0U;
		std::uint64_t page_cache_misses = 0U;
		std::uint64_t page_cache_evictions = 0U;
		std::uint64_t compaction_read_bytes = 0U;
		std::uint64_t compaction_written_bytes = 0U;
		std::uint64_t run_checks = 0U;
		std::uint64_t run_count = 0U;
		std::uint64_t publications = 0U;
		std::uint64_t published_records = 0U;
		std::uint64_t manifest_bytes_written = 0U;
		std::uint64_t commit_nanoseconds = 0U;
		std::uint64_t compactions = 0U;
		std::uint64_t compaction_nanoseconds = 0U;
	};

	/** Detailed timing and byte counts for external-record lookup operations. */
	struct MultiwayExternalRecordLookupProfile
	{
		std::uint64_t index_opens = 0U;
		std::uint64_t index_reads = 0U;
		std::uint64_t run_range_skips = 0U;
		std::uint64_t membership_misses = 0U;
		std::uint64_t data_opens = 0U;
		std::uint64_t data_reads = 0U;
		std::uint64_t requested_bytes = 0U;
		std::uint64_t index_open_nanoseconds = 0U;
		std::uint64_t index_search_nanoseconds = 0U;
		std::uint64_t data_open_nanoseconds = 0U;
		std::uint64_t data_read_nanoseconds = 0U;
	};

	/** On-disk bytes retained by live and obsolete storage segments. */
	struct MultiwayExternalDiskUsage
	{
		std::uint64_t live_segment_bytes = 0U;
		std::uint64_t retained_segment_bytes = 0U;
	};

	/** Aggregate and per-component external storage metrics for training. */
	struct MultiwayExternalStorageTelemetry
	{
		MultiwayExternalStorageStats stats{};
		MultiwayExternalStorageStats catalog{};
		MultiwayExternalStorageStats rows{};
		MultiwayExternalStorageStats pages{};
		std::uint64_t training_generation = 0U;
		std::uint64_t committed_batch = 0U;
		std::uint64_t aggregate_commit_nanoseconds = 0U;
		MultiwayExternalDiskUsage disk_usage{};
	};

	/** Published catalog, row, and page generations for one committed batch. */
	struct MultiwayExternalGenerationManifest
	{
		std::uint64_t generation = 0U;
		std::uint64_t committed_batch_number = 0U;
		std::uint64_t merge_fingerprint = 0U;
		std::uint64_t catalog_generation = 0U;
		std::uint64_t public_state_count = 0U;
		std::uint64_t row_generation = 0U;
		std::uint64_t row_count = 0U;
		std::uint64_t page_generation = 0U;
		std::uint64_t page_count = 0U;
		std::uint64_t logical_value_count = 0U;
		std::uint64_t content_hash = 0U;
	};

	inline constexpr std::uint32_t MULTIWAY_EXTERNAL_CHECKPOINT_SCHEMA = 6U;
	/** Resume metadata and pinned external segment references for training. */
	struct MultiwayExternalCheckpointManifest
	{
		struct SegmentReference
		{
			std::uint64_t generation = 0U;
			std::uint64_t record_count = 0U;
			std::uint64_t content_hash = 0U;
		};
		std::uint32_t backend_schema = MULTIWAY_EXTERNAL_NODE_RECORD_SCHEMA;
		std::uint64_t model_identity_hash = 0U;
		std::uint64_t workflow_identity_hash = 0U;
		std::uint64_t storage_identity_hash = 0U;
		std::uint64_t schedule_hash = 0U;
		std::uint64_t completed_batches = 0U;
		std::uint64_t completed_trajectories = 0U;
		std::uint64_t pruned_negative_regrets = 0U;
		std::uint64_t late_window_start_batch = 0U;
		std::uint64_t terminal_visits = 0U;
		std::uint64_t leaf_visits = 0U;
		std::uint64_t missing_lookup_requests = 0U;
		std::uint64_t preflop_rows = 0U;
		std::uint64_t flop_rows = 0U;
		std::uint64_t turn_rows = 0U;
		std::uint64_t river_rows = 0U;
		std::array<std::uint64_t, 4U> street_visits{};
		std::uint8_t linear_iteration_weighting = 0U;
		std::uint8_t discounting_enabled = 0U;
		std::uint8_t negative_regret_pruning_enabled = 0U;
		std::vector<double> late_window_baseline;
		std::uint64_t wal_generation = 0U;
		std::uint64_t wal_committed_offset = 0U;
		MultiwayExternalGenerationManifest generation{};
		std::array<std::uint32_t, 3U> component_payload_bytes{};
		std::array<std::uint64_t, 3U> component_last_batch_numbers{};
		std::array<std::vector<SegmentReference>, 3U> segments;
	};

	/** Optional publication failure injection for checkpoint writing. */
	struct MultiwayExternalCheckpointWriteOptions
	{
		bool fail_before_publication = false;
	};

	/** Optional transaction failure controls used by restore verification. */
	struct MultiwayExternalCheckpointRestoreOptions
	{
		std::uint32_t fail_after_component_publications = 0U;
		bool fail_before_aggregate_publication = false;
		bool leave_pending_journal_for_recovery_test = false;
	};

	// Captures the currently published aggregate generation without exporting
	// catalog, row, or cell data into process memory.
	/** Captures the published generation and identities without loading row data. */
	[[nodiscard]] MultiwayExternalCheckpointManifest make_multiway_external_checkpoint_manifest(
		const std::filesystem::path& external_storage_directory,
		std::uint64_t model_identity_hash,
		std::uint64_t workflow_identity_hash,
		std::uint64_t storage_identity_hash,
		std::uint64_t schedule_hash,
		std::uint64_t completed_trajectories);

	/** Atomically writes and pins an external checkpoint manifest. */
	void save_multiway_external_checkpoint_manifest(
		const std::filesystem::path& path,
		const std::filesystem::path& external_storage_directory,
		const MultiwayExternalCheckpointManifest& manifest,
		bool synchronous_durability,
		MultiwayExternalCheckpointWriteOptions options = {},
		std::uint64_t disk_reserve_bytes = 0U);
	/** Loads a manifest matching expected model, workflow, storage, and schedule IDs. */
	[[nodiscard]] MultiwayExternalCheckpointManifest load_multiway_external_checkpoint_manifest(
		const std::filesystem::path& path,
		const std::filesystem::path& external_storage_directory,
		std::uint64_t expected_model_identity_hash,
		std::uint64_t expected_workflow_identity_hash,
		std::uint64_t expected_storage_identity_hash,
		std::uint64_t expected_schedule_hash);
	/** Releases segment pins held by a saved external checkpoint. */
	void release_multiway_external_checkpoint_manifest_pins(
		const std::filesystem::path& checkpoint_path,
		const std::filesystem::path& external_storage_directory,
		std::uint64_t storage_identity_hash,
		bool synchronous_durability);
	/** Restores a published generation after verifying the checkpoint identities. */
	[[nodiscard]] MultiwayExternalCheckpointManifest restore_multiway_external_checkpoint(
		const std::filesystem::path& checkpoint_path,
		const std::filesystem::path& external_storage_directory,
		std::uint64_t expected_model_identity_hash,
		std::uint64_t expected_workflow_identity_hash,
		std::uint64_t expected_storage_identity_hash,
		std::uint64_t expected_schedule_hash,
		bool synchronous_durability,
		MultiwayExternalCheckpointRestoreOptions options = {});

	/** Atomically publishes the aggregate generation manifest. */
	void publish_multiway_external_generation(const std::filesystem::path& directory,
		const MultiwayExternalGenerationManifest& manifest, bool synchronous_durability);
	/** Loads the aggregate generation manifest when a committed one exists. */
	[[nodiscard]] bool load_multiway_external_generation(const std::filesystem::path& directory,
		MultiwayExternalGenerationManifest& manifest);

	// Coordinates root/catalog, root/rows, and root/pages CURRENT manifests
	// through one durable commit point. Construct this before opening child stores
	// on restart. Callers hold the exclusive coordinator batch boundary and
	// recreate child stores after abort because their in-memory views may be stale.
	/** Provides one durable commit point across external storage components. */
	class MultiwayExternalBatchTransaction
	{
	public:
		/** Opens the aggregate transaction and recovers any pending batch. */
		MultiwayExternalBatchTransaction(std::filesystem::path directory,
			bool synchronous_durability);
		/** Starts a component batch under the requested merge fingerprint. */
		void begin_batch(std::uint64_t batch_number, std::uint64_t merge_fingerprint);
		void set_merge_fingerprint(std::uint64_t merge_fingerprint);
		/** Commits all component generations through the aggregate manifest. */
		void commit_batch(const MultiwayExternalGenerationManifest& manifest);
		/** Rolls back the open batch to the previously published generation. */
		void abort_batch();
		/** Returns the currently committed aggregate generation. */
		[[nodiscard]] const MultiwayExternalGenerationManifest& current_manifest() const noexcept { return current_; }
		/** Reports whether a component batch is currently open. */
		[[nodiscard]] bool batch_open() const noexcept { return batch_open_; }

	private:
		void recover_pending_batch();
		void rollback_pending_batch(std::uint32_t previous_manifest_flags);
		void clear_pending_batch();
		std::filesystem::path directory_;
		bool synchronous_durability_ = true;
		MultiwayExternalGenerationManifest current_{};
		std::uint64_t batch_number_ = 0U;
		std::uint64_t merge_fingerprint_ = 0U;
		std::uint32_t previous_manifest_flags_ = 0U;
		std::array<std::uint64_t, 3U> previous_component_generations_{};
		std::array<std::uint64_t, 3U> previous_component_batch_numbers_{};
		bool batch_open_ = false;
	};

	// Fixed-width, append-only sorted runs. Callers serialize typed public-state,
	// row, or page records explicitly and keep this API at coordinator boundaries.
	/** Bounded-cache, append-only sorted record store with batch publication. */
	class MultiwayExternalRecordStore
	{
	public:
		/** Callback for visiting borrowed payloads in sorted key order. */
		using OrderedRecordVisitor = bool (*)(void*, std::uint64_t,
			const std::uint8_t*, std::size_t);

		/** Opens or creates a store with fixed-size record payloads. */
		MultiwayExternalRecordStore(MultiwayExternalStorageConfig config, std::size_t payload_bytes);
		~MultiwayExternalRecordStore();
		MultiwayExternalRecordStore(const MultiwayExternalRecordStore&) = delete;
		MultiwayExternalRecordStore& operator=(const MultiwayExternalRecordStore&) = delete;

		/** Appends a sorted record batch and publishes its run. */
		void append_batch(std::uint64_t batch_number, std::uint64_t merge_fingerprint,
			const std::vector<MultiwayExternalRecord>& records);
		// Append another bounded sorted run for the same outer training batch.
		// The aggregate training manifest is the atomic publication boundary.
		void append_batch_continuation(std::uint64_t batch_number, std::uint64_t merge_fingerprint,
			const std::vector<MultiwayExternalRecord>& records);
		/** Finds a key and copies its payload into an owning vector. */
		[[nodiscard]] bool lookup(std::uint64_t key, std::vector<std::uint8_t>& payload) const;
		[[nodiscard]] bool lookup(std::uint64_t key, std::uint8_t* payload,
			std::size_t payload_size) const;
		[[nodiscard]] bool lookup_profiled(std::uint64_t key, std::vector<std::uint8_t>& payload,
			MultiwayExternalRecordLookupProfile& profile) const;
		// Caller budgets this fixed allocation and holds a stable storage lease.
		static constexpr std::size_t export_filter_bytes = 1024U * 1024U;
		void begin_export_filter() const;
		void end_export_filter() const noexcept;
		// Visits unique keys in ascending order. Payload storage is borrowed and
		// valid only for the duration of the callback. Run fan-in is bounded by
		// the configured compaction fan-in; larger sets fail closed.
		/** Visits unique records in key order using borrowed payload memory. */
		[[nodiscard]] bool visit_ordered_records(OrderedRecordVisitor visitor,
			void* context) const;
		// Visits matches for sorted unique keys using buffered index reads.
		[[nodiscard]] bool visit_matching_records(const std::vector<std::uint64_t>& sorted_keys,
			OrderedRecordVisitor visitor, void* context,
			MultiwayExternalRecordLookupProfile* profile = nullptr) const;
		// During a multi-store transaction, retain old input runs until the
		// aggregate manifest commits. Call compact(false) afterward to reclaim them.
		void compact(bool retain_input_runs = false);
		void compact_batch_runs(std::uint64_t base_generation);
		void reclaim_orphans();
		[[nodiscard]] std::uint64_t generation() const;
		[[nodiscard]] std::uint64_t record_count() const;
		[[nodiscard]] std::uint64_t last_batch_number() const;
		[[nodiscard]] std::uint64_t content_hash() const;
		[[nodiscard]] std::size_t cache_capacity_records() const noexcept;
		[[nodiscard]] std::size_t cache_bytes() const noexcept;
		[[nodiscard]] std::size_t payload_bytes() const noexcept { return payload_bytes_; }
		[[nodiscard]] MultiwayExternalStorageStats stats() const;
		[[nodiscard]] MultiwayExternalDiskUsage disk_usage() const;
		[[nodiscard]] std::uint64_t maximum_batch_bytes() const noexcept { return config_.maximum_wal_bytes; }
		[[nodiscard]] std::uint64_t segment_target_bytes() const noexcept { return config_.segment_target_bytes; }
		void set_publication_observer(MultiwayExternalPublicationObserver observer, void* context,
			std::uint32_t component);

	private:
		struct Run
		{
			std::uint64_t generation = 0U;
			std::uint64_t record_count = 0U;
			std::uint64_t content_hash = 0U;
			std::filesystem::path data_path;
			std::filesystem::path index_path;
			std::uint64_t minimum_key = 0U;
			std::uint64_t maximum_key = 0U;
		};
		struct CacheEntry
		{
			std::uint64_t key = 0U;
			bool valid = false;
			std::vector<std::uint8_t> payload;
		};

		void load_manifest();
		void append_batch_impl(std::uint64_t batch_number, std::uint64_t merge_fingerprint,
			const std::vector<MultiwayExternalRecord>& records, bool allow_same_batch);
		void compact_impl(bool retain_input_runs, std::uint64_t base_generation,
			bool batch_suffix_only, bool cleanup_orphans_before);
		void cleanup_orphans();
		void recover_wal();
		void publish_manifest(const std::vector<Run>& runs, std::uint64_t next_generation, std::uint64_t last_batch_number);
		[[nodiscard]] bool lookup_unlocked(std::uint64_t key, std::uint8_t* payload,
			std::size_t payload_size) const;
		[[nodiscard]] bool lookup_run(const Run& run, std::uint64_t key,
			std::uint8_t* payload, std::size_t payload_size) const;
		[[nodiscard]] bool lookup_run_profiled(const Run& run, std::uint64_t key,
			std::uint8_t* payload, std::size_t payload_size, MultiwayExternalRecordLookupProfile& profile) const;
		void load_run_key_range(Run& run) const;
		[[nodiscard]] std::uint64_t run_hash(const Run& run) const;
		void check_disk_reserve(std::uint64_t additional_bytes) const;
		void write_wal(std::uint64_t batch_number, std::uint64_t merge_fingerprint,
			const std::vector<MultiwayExternalRecord>& records, std::size_t first, std::size_t count) const;
		void flush_file(const std::filesystem::path& path) const;
		void flush_directory() const;

		MultiwayExternalStorageConfig config_;
		std::size_t payload_bytes_ = 0U;
		std::size_t cache_capacity_ = 0U;
		std::uint64_t generation_ = 0U;
		std::uint64_t record_count_ = 0U;
		std::uint64_t last_batch_number_ = 0U;
		std::vector<Run> runs_;
		mutable std::vector<CacheEntry> cache_;
		mutable std::unique_ptr<std::uint64_t[]> export_filter_;
		mutable MultiwayExternalStorageStats stats_{};
		bool failed_ = false;
		mutable std::mutex mutex_;
	};

	// Durable, sorted public-node catalog. It stores only packed fixed-width node
	// records and reconstructs ancestry iteratively into caller-owned bounded space.
	/** Externalized public-state catalog with ancestry reconstruction. */
	class MultiwayExternalPublicCatalog
	{
	public:
		/** Opens the catalog with a maximum reconstruction ancestry depth. */
		MultiwayExternalPublicCatalog(MultiwayExternalStorageConfig config,
			std::size_t maximum_ancestry_depth);
		// This overload stores an empty menu. Use the descriptor overload when
		// nodes have legal actions.
		void append_batch(std::uint64_t batch_number, std::uint64_t merge_fingerprint,
			const std::vector<MultiwayPackedPublicNode>& nodes);
		void append_batch(std::uint64_t batch_number, std::uint64_t merge_fingerprint,
			const std::vector<MultiwayPackedPublicNode>& nodes,
			const std::vector<MultiwayPublicStateDescriptor>& descriptors);
		/** Appends newly discovered public descriptors selected by state flags. */
		void append_discovered_batch(std::uint64_t batch_number,
			std::uint64_t merge_fingerprint,
			const std::vector<MultiwayPublicStateDescriptor>& states,
			const std::vector<std::uint8_t>& new_state_flags);
		// Batch-boundary lookup for coordinator admission. The caller supplies
		// reusable buffers; outputs are resized to the sorted candidate batch.
		void classify_batch(const std::vector<MultiwayPublicStateDescriptor>& states,
			std::vector<std::uint8_t>& new_state_flags,
			std::vector<MultiwayPackedPublicNode>& existing_nodes) const;
		void classify_batch(const std::vector<MultiwayPublicStateDescriptor>& states,
			std::vector<std::uint8_t>& new_state_flags) const;
		/** Looks up one packed node by stable public-state id. */
		[[nodiscard]] bool lookup(MultiwayPublicStateId id, MultiwayPackedPublicNode& output) const;
		/** Copies a state's action menu into caller-owned bounded storage. */
		[[nodiscard]] bool lookup_menu(MultiwayPublicStateId id,
			MultiwayActionDescriptor* output, std::size_t output_capacity,
			std::size_t& action_count) const;
		[[nodiscard]] bool contains(MultiwayPublicStateId id) const;
		/** Reconstructs the parent-first packed ancestry chain for one state. */
		[[nodiscard]] bool reconstruct_chain(MultiwayPublicStateId id,
			std::vector<MultiwayPackedPublicNode>& parent_first_output) const;
		/** Reconstructs a dynamic public descriptor from packed records. */
		[[nodiscard]] bool reconstruct(MultiwayPublicStateId id,
			MultiwayPublicStateDescriptor& output) const;
		void compact(bool retain_input_runs = false) { store_.compact(retain_input_runs); }
		void compact_batch_runs(std::uint64_t base_generation) { store_.compact_batch_runs(base_generation); }
		void reclaim_orphans() { store_.reclaim_orphans(); }
		[[nodiscard]] std::uint64_t record_count() const noexcept { return store_.record_count(); }
		[[nodiscard]] std::uint64_t generation() const { return store_.generation(); }
		[[nodiscard]] MultiwayExternalStorageStats stats() const { return store_.stats(); }
		[[nodiscard]] MultiwayExternalDiskUsage disk_usage() const { return store_.disk_usage(); }
		[[nodiscard]] std::uint64_t content_hash() const { return store_.content_hash(); }
		void set_publication_observer(MultiwayExternalPublicationObserver observer, void* context)
		{
			store_.set_publication_observer(observer, context, 0U);
		}

	private:
		void classify_batch_impl(const std::vector<MultiwayPublicStateDescriptor>& states,
			std::vector<std::uint8_t>& new_state_flags,
			std::vector<MultiwayPackedPublicNode>* existing_nodes) const;
		void append_discovered_batch_impl(std::uint64_t batch_number,
			std::uint64_t merge_fingerprint,
			const std::vector<MultiwayPublicStateDescriptor>& states,
			const std::vector<std::uint8_t>* new_state_flags,
			const std::vector<MultiwayPackedPublicNode>* packed_nodes);
		MultiwayExternalRecordStore store_;
		std::size_t maximum_ancestry_depth_ = 0U;
	};

	/** Bucket/action dimensions and page range for one external row. */
	struct MultiwayExternalRowMetadata
	{
		std::uint32_t bucket_count = 0U;
		std::uint8_t action_count = 0U;
		std::uint64_t first_page_id = 0U;
		std::uint32_t page_count = 0U;
	};

	/** Page id and local dimensions for one bucket range. */
	struct MultiwayExternalRowPage
	{
		std::uint64_t page_id = 0U;
		std::uint32_t page_index = 0U;
		std::uint32_t bucket_count = 0U;
		std::uint8_t action_count = 0U;
	};

	/** Stable non-owning row handle bound to its owning external row index. */
	class MultiwayExternalRowBinding
	{
	public:
		MultiwayExternalRowBinding() = default;
		/** Returns the public-state id associated with the binding. */
		[[nodiscard]] MultiwayPublicStateId public_state() const noexcept;
		[[nodiscard]] std::int32_t seat() const noexcept { return seat_; }
		[[nodiscard]] std::uint32_t bucket_count() const noexcept { return row_.bucket_count; }
		[[nodiscard]] std::uint8_t action_count() const noexcept { return row_.action_count; }
		[[nodiscard]] std::uint64_t first_page_id() const noexcept { return row_.first_page_id; }
		[[nodiscard]] std::uint32_t page_count() const noexcept { return row_.page_count; }

	private:
		friend class MultiwayExternalRowIndex;
		friend class MultiwayExternalCompactStorage;
		const MultiwayExternalRowIndex* owner_ = nullptr;
		std::uint64_t state_id_ = 0U;
		std::int32_t seat_ = -1;
		MultiwayExternalRowMetadata row_{};
	};

	// Stores six seat rows in one fixed-width state record so the external index
	// remains keyed by the full 64-bit public-state ID without truncation.
	/** Sorted external index for infoset row shapes and bucket pages. */
	class MultiwayExternalRowIndex
	{
	public:
		/** Callback for visiting row bindings in stable infoset order. */
		using OrderedRowVisitor = bool (*)(void*, const MultiwayExternalRowBinding&);

		MultiwayExternalRowIndex(MultiwayExternalStorageConfig config,
			std::uint32_t page_width);
		void append_batch(std::uint64_t batch_number, std::uint64_t merge_fingerprint,
			const std::vector<MultiwaySparseRowShape>& rows, std::uint64_t& next_page_id);
		[[nodiscard]] bool lookup(MultiwayPublicStateId state, std::int32_t seat,
			MultiwayExternalRowMetadata& output) const;
		void lookup_rows(const std::vector<MultiwaySparseRowShape>& rows,
			std::vector<MultiwayExternalRowMetadata>& output) const;
		[[nodiscard]] bool visit_ordered_rows(OrderedRowVisitor visitor,
			void* context) const;
		[[nodiscard]] bool locate_bucket(MultiwayPublicStateId state, std::int32_t seat,
			std::uint32_t bucket, MultiwayExternalRowPage& output) const;
		[[nodiscard]] bool locate_bucket(const MultiwayExternalRowBinding& row,
			std::uint32_t bucket, MultiwayExternalRowPage& output) const;
		[[nodiscard]] MultiwayExternalStorageStats stats() const { return store_.stats(); }
		[[nodiscard]] MultiwayExternalDiskUsage disk_usage() const { return store_.disk_usage(); }
		void compact(bool retain_input_runs = false) { store_.compact(retain_input_runs); }
		void compact_batch_runs(std::uint64_t base_generation) { store_.compact_batch_runs(base_generation); }
		void reclaim_orphans() { store_.reclaim_orphans(); }
		[[nodiscard]] std::uint32_t page_width() const noexcept { return page_width_; }
		[[nodiscard]] std::uint64_t next_page_id() const noexcept { return next_page_id_; }
		[[nodiscard]] std::uint64_t generation() const { return store_.generation(); }
		[[nodiscard]] std::uint64_t content_hash() const { return store_.content_hash(); }
		void set_publication_observer(MultiwayExternalPublicationObserver observer, void* context)
		{
			store_.set_publication_observer(observer, context, 1U);
		}

	private:
		MultiwayExternalRecordStore store_;
		std::uint32_t page_width_ = 0U;
		std::uint64_t next_page_id_ = 1U;
	};

	/** Per-operation timing and lookup counters for a compact page request. */
	struct MultiwayExternalPageProfile
	{
		std::uint64_t locate_nanoseconds = 0U;
		std::uint64_t cache_find_nanoseconds = 0U;
		std::uint64_t victim_nanoseconds = 0U;
		std::uint64_t store_lookup_nanoseconds = 0U;
		std::uint64_t decode_nanoseconds = 0U;
		std::uint64_t zero_nanoseconds = 0U;
		std::uint64_t decoded_pages = 0U;
		std::uint64_t zero_pages = 0U;
		MultiwayExternalRecordLookupProfile record_lookup{};
	};

	/** Bounded cache of fixed-point pages backed by an external record store. */
	class MultiwayExternalCompactPages
	{
	public:
		/** Pinned mutable cell view; valid only until the page is unpinned. */
		struct PageView
		{
			// Cell pointers remain valid only until the matching unpin. Action stride
			// is fixed to the configured page width, including the final short page.
			std::uint64_t page_id = 0U;
			std::int32_t* regrets = nullptr;
			std::uint64_t* strategy_masses = nullptr;
			std::uint32_t bucket_count = 0U;
			std::uint8_t action_count = 0U;
			std::uint32_t action_stride = 0U;
		};

		/** Opens a fixed-width page store with a bounded decoded-page cache. */
		MultiwayExternalCompactPages(MultiwayExternalStorageConfig config,
			std::uint32_t page_width, std::uint64_t cache_bytes,
			std::uint64_t maximum_page_id = 0U);
		void begin_batch(std::uint64_t batch_number, std::uint64_t merge_fingerprint);
		[[nodiscard]] std::uint64_t next_batch_number() const;
		// Returns an empty view for allocated but untouched pages.
		/** Pins an existing page, returning an empty view for untouched pages. */
		[[nodiscard]] PageView pin_existing(std::uint64_t page_id);
		[[nodiscard]] PageView pin(std::uint64_t page_id, MultiwayPublicStateId state,
			std::int32_t seat, std::uint32_t page_index, std::uint32_t bucket_count,
			std::uint8_t action_count);
		/** Pins a page and records lookup-stage timings into profile. */
		[[nodiscard]] PageView pin_profiled(std::uint64_t page_id, MultiwayPublicStateId state,
			std::int32_t seat, std::uint32_t page_index, std::uint32_t bucket_count,
			std::uint8_t action_count, MultiwayExternalPageProfile* profile);
		void unpin(std::uint64_t page_id);
		void mark_dirty(std::uint64_t page_id);
		[[nodiscard]] bool batch_full() const;
		void commit_batch();
		void set_merge_fingerprint(std::uint64_t merge_fingerprint);
		void abort_batch();
		void compact(bool retain_input_runs = false);
		void compact_batch_runs(std::uint64_t base_generation);
		void reclaim_orphans();
		void begin_export_filter() const { store_->begin_export_filter(); }
		void end_export_filter() const noexcept { store_->end_export_filter(); }
		[[nodiscard]] std::uint64_t generation() const { return store_->generation(); }
		[[nodiscard]] std::uint64_t content_hash() const { return store_->content_hash(); }
		[[nodiscard]] std::size_t page_capacity() const noexcept { return pages_.size(); }
		[[nodiscard]] std::size_t memory_bytes() const noexcept;
		[[nodiscard]] MultiwayExternalStorageStats stats() const;
		[[nodiscard]] MultiwayExternalDiskUsage disk_usage() const { return store_->disk_usage(); }
		void set_publication_observer(MultiwayExternalPublicationObserver observer, void* context)
		{
			store_->set_publication_observer(observer, context, 2U);
		}

	private:
		struct Page
		{
			std::uint64_t page_id = 0U;
			std::uint64_t state_id = 0U;
			std::int32_t seat = -1;
			std::uint32_t page_index = 0U;
			std::uint32_t bucket_count = 0U;
			std::uint8_t action_count = 0U;
			std::uint32_t pin_count = 0U;
			bool valid = false;
			bool dirty = false;
			std::vector<std::int32_t> regrets;
			std::vector<std::uint64_t> strategy_masses;
			std::vector<std::int32_t> before_regrets;
			std::vector<std::uint64_t> before_strategy_masses;
		};
		[[nodiscard]] Page* find(std::uint64_t page_id) noexcept;
		void validate_page_id(std::uint64_t page_id) const;
		void cache_page(Page& page, std::size_t slot);
		void forget_cached_page(Page& page, std::size_t slot) noexcept;
		[[nodiscard]] std::size_t page_payload_size() const noexcept;
		void decode_page(const std::uint8_t* bytes, Page& page) const;
		void encode_page(const Page& page, std::vector<std::uint8_t>& bytes) const;
		std::unique_ptr<MultiwayExternalRecordStore> store_;
		std::uint32_t page_width_ = 0U;
		std::vector<Page> pages_;
		std::vector<std::uint32_t> page_lookup_slots_;
		std::vector<std::uint8_t> lookup_scratch_;
		std::vector<std::uint8_t> encode_scratch_;
		std::uint64_t store_cache_budget_bytes_ = 0U;
		std::size_t next_victim_ = 0U;
		std::uint64_t batch_number_ = 0U;
		std::uint64_t merge_fingerprint_ = 0U;
		std::uint64_t page_cache_hits_ = 0U;
		std::uint64_t page_cache_misses_ = 0U;
		std::uint64_t page_cache_evictions_ = 0U;
		std::size_t dirty_count_ = 0U;
		bool batch_open_ = false;
		mutable std::mutex mutex_;
	};

	// Fixed-point row/page adapter. The caller owns the aggregate transaction and
	// must recreate this object after rollback so its component views are current.
	/** Combines external row metadata and pinned compact accumulator pages. */
	class MultiwayExternalCompactStorage
	{
	public:
		/** Move-only pinned page and bucket index used for accumulator access. */
		struct PinnedBucket
		{
			PinnedBucket() = default;
			PinnedBucket(const PinnedBucket&) = delete;
			PinnedBucket& operator=(const PinnedBucket&) = delete;
			PinnedBucket(PinnedBucket&& other) noexcept;
			PinnedBucket& operator=(PinnedBucket&&) = delete;

			const MultiwayExternalCompactStorage* owner = nullptr;
			MultiwayExternalCompactPages::PageView page{};
			std::uint32_t local_bucket = 0U;
			bool writable = false;
		};

		/** Opens the external row index and bounded compact-page cache. */
		MultiwayExternalCompactStorage(MultiwayExternalStorageConfig row_config,
			MultiwayExternalStorageConfig page_config, std::uint32_t page_width,
			std::uint64_t page_cache_bytes, std::uint64_t maximum_page_id = 0U);
		void admit_rows(std::uint64_t batch_number, std::uint64_t merge_fingerprint,
			const std::vector<MultiwaySparseRowShape>& rows);
		void begin_batch(std::uint64_t batch_number, std::uint64_t merge_fingerprint);
		void flush_page_batch_and_continue();
		void flush_page_batch_if_full();
		void set_merge_fingerprint(std::uint64_t merge_fingerprint);
		[[nodiscard]] bool lookup_row(MultiwayInfosetId infoset,
			MultiwayExternalRowBinding& output) const;
		void lookup_rows(const std::vector<MultiwaySparseRowShape>& rows,
			std::vector<MultiwayExternalRowMetadata>& output) const;
		[[nodiscard]] bool visit_ordered_rows(
			MultiwayExternalRowIndex::OrderedRowVisitor visitor, void* context) const;
		[[nodiscard]] PinnedBucket pin_bucket(const MultiwayExternalRowBinding& row,
			std::uint32_t bucket, bool writable) const;
		[[nodiscard]] PinnedBucket pin_bucket_profiled(const MultiwayExternalRowBinding& row,
			std::uint32_t bucket, bool writable, MultiwayExternalPageProfile* profile) const;
		void apply_delta(PinnedBucket& bucket, std::uint8_t action,
			double regret, double strategy_sum);
		void regret_matched_strategy_into(const PinnedBucket& bucket,
			double* output, std::size_t output_size) const;
		void average_strategy_into(const PinnedBucket& bucket,
			double* output, std::size_t output_size) const;
		[[nodiscard]] bool action_below_regret(const PinnedBucket& bucket,
			std::uint8_t action, double threshold) const noexcept;
		void unpin_bucket(PinnedBucket& bucket) const;
		void apply_delta(const MultiwayExternalRowBinding& row, std::uint32_t bucket,
			std::uint8_t action, double regret, double strategy_sum);
		void scale_regrets(double factor);
		[[nodiscard]] std::size_t scale_and_prune_regrets(
			double factor, double threshold, double regret_floor);
		[[nodiscard]] std::size_t prune_negative_regrets(
			double threshold = 0.0, double regret_floor = 0.0);
		void regret_matched_strategy_into(const MultiwayExternalRowBinding& row,
			std::uint32_t bucket, double* output, std::size_t output_size) const;
		void average_strategy_into(const MultiwayExternalRowBinding& row,
			std::uint32_t bucket, double* output, std::size_t output_size) const;
		[[nodiscard]] bool action_below_regret(const MultiwayExternalRowBinding& row,
			std::uint32_t bucket, std::uint8_t action, double threshold) const;
		/** Publishes all dirty pages in the active batch. */
		void commit_batch();
		/** Restores page contents and discards unpublished batch changes. */
		void abort_batch();
		void compact(bool retain_input_runs = false);
		void compact_batch_runs(std::uint64_t base_row_generation,
			std::uint64_t base_page_generation);
		void reclaim_orphans();
		[[nodiscard]] std::uint64_t next_page_id() const noexcept { return next_page_id_; }
		[[nodiscard]] std::uint64_t row_generation() const { return rows_.generation(); }
		[[nodiscard]] std::uint64_t page_generation() const { return pages_.generation(); }
		[[nodiscard]] std::uint64_t content_hash() const;
		[[nodiscard]] MultiwayExternalStorageStats stats() const;
		[[nodiscard]] MultiwayExternalStorageStats row_index_stats() const { return rows_.stats(); }
		[[nodiscard]] MultiwayExternalStorageStats page_store_stats() const { return pages_.stats(); }
		void set_publication_observer(MultiwayExternalPublicationObserver observer, void* context)
		{
			rows_.set_publication_observer(observer, context);
			pages_.set_publication_observer(observer, context);
		}
		void begin_export_filter() const { pages_.begin_export_filter(); }
		void end_export_filter() const noexcept { pages_.end_export_filter(); }
		[[nodiscard]] MultiwayExternalDiskUsage disk_usage() const;

	private:
		MultiwayExternalRowIndex rows_;
		mutable MultiwayExternalCompactPages pages_;
		std::uint64_t next_page_id_ = 1U;
		std::uint32_t page_width_ = 0U;
		std::uint64_t page_batch_number_ = 0U;
		std::uint64_t page_batch_fingerprint_ = 0U;
	};

	// Owns all external training components and publishes them through one
	// aggregate manifest. Constructing this object recovers the transaction before
	// opening the component stores.
	/** Coordinates external catalog, row, and page stores for training. */
	class MultiwayExternalTrainingStorage
	{
	public:
		/** Opens components after recovering aggregate batch state. */
		MultiwayExternalTrainingStorage(std::filesystem::path directory,
			MultiwayExternalStorageConfig catalog_config,
			MultiwayExternalStorageConfig row_config,
			MultiwayExternalStorageConfig page_config,
			std::uint32_t page_width, std::uint64_t row_page_cache_bytes,
			std::size_t maximum_ancestry_depth, bool synchronous_durability,
			std::uint64_t maximum_page_id = 0U);
		void initialize_root(const MultiwayPublicStateDescriptor& root);
		[[nodiscard]] std::uint64_t next_batch_number() const;
		void begin_batch(std::uint64_t batch_number);
		void append_catalog(std::uint64_t component_fingerprint,
			const std::vector<MultiwayPackedPublicNode>& nodes,
			const std::vector<MultiwayPublicStateDescriptor>& descriptors);
		void append_discovered_catalog(std::uint64_t component_fingerprint,
			const std::vector<MultiwayPublicStateDescriptor>& states,
			const std::vector<std::uint8_t>& new_state_flags);
		void admit_rows(std::uint64_t component_fingerprint,
			const std::vector<MultiwaySparseRowShape>& rows);
		void begin_page_batch();
		void compact_components();
		/** Commits component runs through one aggregate generation manifest. */
		void commit_batch(std::uint64_t merge_fingerprint, std::uint64_t row_count,
			std::uint64_t logical_value_count);
		void abort_batch();
		[[nodiscard]] MultiwayExternalPublicCatalog& catalog() noexcept { return *catalog_; }
		[[nodiscard]] const MultiwayExternalPublicCatalog& catalog() const noexcept { return *catalog_; }
		[[nodiscard]] MultiwayExternalCompactStorage& compact_storage() noexcept { return *compact_; }
		[[nodiscard]] const MultiwayExternalCompactStorage& compact_storage() const noexcept { return *compact_; }
		[[nodiscard]] const MultiwayExternalGenerationManifest& current_manifest() const noexcept
		{
			return transaction_.current_manifest();
		}
		[[nodiscard]] std::uint64_t content_hash() const;
		[[nodiscard]] MultiwayExternalStorageTelemetry telemetry(bool include_disk_usage = true) const;
		void set_publication_observer(MultiwayExternalPublicationObserver observer, void* context);

	private:
		void open_components();
		std::filesystem::path directory_;
		MultiwayExternalStorageConfig catalog_config_;
		MultiwayExternalStorageConfig row_config_;
		MultiwayExternalStorageConfig page_config_;
		std::uint32_t page_width_ = 0U;
		std::uint64_t row_page_cache_bytes_ = 0U;
		std::uint64_t maximum_page_id_ = 0U;
		std::size_t maximum_ancestry_depth_ = 0U;
		std::uint64_t batch_number_ = 0U;
		std::array<std::uint64_t, 3U> batch_base_generations_{};
		bool page_batch_open_ = false;
		std::uint64_t aggregate_commit_nanoseconds_ = 0U;
		bool synchronous_durability_ = true;
		MultiwayExternalBatchTransaction transaction_;
		std::unique_ptr<MultiwayExternalPublicCatalog> catalog_;
		std::unique_ptr<MultiwayExternalCompactStorage> compact_;
	};

	// Coordinator-owned bounded page cache. Pinned page views contain raw cell
	// bytes so action loops never perform file or index operations.
	/** Reusable byte-page cache for coordinator-boundary record access. */
	class MultiwayExternalPageCache
	{
	public:
		/** Mutable byte view valid only while its cache page remains pinned. */
		struct PageView
		{
			std::uint64_t key = 0U;
			std::uint8_t* bytes = nullptr;
			std::size_t size = 0U;
		};

		/** Creates a bounded cache over the supplied record store. */
		MultiwayExternalPageCache(MultiwayExternalRecordStore& store, std::uint64_t cache_bytes);
		/** Pins a page by key, loading it when it is not cached. */
		[[nodiscard]] PageView pin(std::uint64_t key);
		/** Releases one pin for a cached page. */
		void unpin(std::uint64_t key);
		/** Marks a pinned page as modified for batch publication. */
		void mark_dirty(std::uint64_t key);
		/** Opens a write batch with its deterministic merge fingerprint. */
		void begin_batch(std::uint64_t batch_number, std::uint64_t merge_fingerprint);
		/** Flushes dirty pages and commits the active batch. */
		void commit_batch();
		/** Restores pages to their pre-batch state. */
		void abort_batch() noexcept;
		[[nodiscard]] std::size_t page_capacity() const noexcept { return pages_.size(); }
		[[nodiscard]] std::size_t dirty_page_count() const noexcept;
		[[nodiscard]] std::size_t memory_bytes() const noexcept;

	private:
		struct Page
		{
			std::uint64_t key = 0U;
			std::uint32_t pin_count = 0U;
			bool valid = false;
			bool dirty = false;
			std::vector<std::uint8_t> bytes;
			std::vector<std::uint8_t> before_batch;
		};
		[[nodiscard]] Page* find(std::uint64_t key) noexcept;
		MultiwayExternalRecordStore* store_ = nullptr;
		std::size_t payload_bytes_ = 0U;
		std::vector<Page> pages_;
		std::vector<std::uint8_t> lookup_scratch_;
		std::size_t next_victim_ = 0U;
		std::uint64_t batch_number_ = 0U;
		std::uint64_t merge_fingerprint_ = 0U;
		bool batch_open_ = false;
	};

} // namespace texas::solver::multiway
