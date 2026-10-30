#pragma once

/** @file
 * Artifact schemas, validation, streaming, and public audit record interfaces.
 */

#include "games/multiway_replay.hpp"
#include "core/atomic_publish.hpp"
#include "solver/multiway/blueprint/multiway_export.hpp"
#include "solver/multiway/blueprint/multiway_blueprint_store.hpp"
#include "solver/multiway/resolver/multiway_resolver.hpp"

#include <cstdint>
#include <cstddef>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace texas::solver::multiway
{

	struct MultiwayMemoryComponent;

	inline constexpr std::uint32_t MULTIWAY_BLUEPRINT_MANIFEST_SCHEMA_VERSION = 4U;
	inline constexpr std::uint32_t MULTIWAY_PUBLIC_DECISION_LOG_SCHEMA_VERSION = 3U;
	inline constexpr std::uint32_t MULTIWAY_PROTECTED_REPLAY_SCHEMA_VERSION = 1U;

	// Sidecar for a compact root-only checkpoint. It binds the artifact bytes to
	// the exact model identity without adding row storage to deployment exports.
	/** Identity and content hash sidecar for a compact root snapshot. */
	struct MultiwayBlueprintManifest
	{
		std::uint32_t schema_version = MULTIWAY_BLUEPRINT_MANIFEST_SCHEMA_VERSION;
		MultiwayModelIdentity identity{};
		std::uint64_t snapshot_hash = 0;

		/** Checks schema version and identity/hash fields. */
		void validate() const;
	};

	/** Identifies whether a verified snapshot came from the primary or fallback path. */
	enum class MultiwayArtifactSource : std::uint8_t
	{
		Primary,
		KnownGoodFallback,
	};

	/** Compact snapshot and manifest after identity and integrity verification. */
	struct MultiwayVerifiedBlueprintArtifact
	{
		MultiwayBlueprintSnapshot snapshot{};
		MultiwayBlueprintManifest manifest{};
		MultiwayArtifactSource source = MultiwayArtifactSource::Primary;

		/** Checks the snapshot and manifest against the expected model identity. */
		void validate(const MultiwayModelIdentity& expected_identity) const;
	};

	inline constexpr std::uint32_t MULTIWAY_FULL_BLUEPRINT_SCHEMA_VERSION = 2U;
	inline constexpr std::uint32_t MULTIWAY_POLICY_CONTENT_HASH_VERSION = 1U;
	inline constexpr std::uint32_t MULTIWAY_FULL_BLUEPRINT_MAX_ACTIONS_PER_ROW = 64U;

	// Full runtime lookup payload. It is independent of the compact root
	// snapshot so hosts can retain the latter as a compatible fallback.
	/** Owning, full-row runtime blueprint payload. */
	struct MultiwayFullBlueprintArtifact
	{
		std::uint32_t schema_version = MULTIWAY_FULL_BLUEPRINT_SCHEMA_VERSION;
		MultiwayModelIdentity identity{};
		MultiwayBlueprintTrainingMetadata training{};
		std::vector<MultiwayBlueprintRow> rows;
		std::uint64_t payload_hash = 0U;

		/** Checks schema, identity, row order, rows, and payload hash. */
		void validate() const;
		/** Estimates the artifact's contribution to runtime memory usage. */
		[[nodiscard]] MultiwayMemoryComponent memory_snapshot() const noexcept;
	};

	/** Non-owning view of one serialized full-blueprint row. */
	struct MultiwayFullBlueprintRowView
	{
		MultiwayInfosetId infoset{};
		std::uint32_t bucket = 0U;
		std::uint64_t action_menu_id = 0U;
		const MultiwayQuantizedRootAction* actions = nullptr;
		std::size_t action_count = 0U;

		/** Creates an empty view. */
		MultiwayFullBlueprintRowView() = default;
		/** Creates a non-owning view over the supplied row. */
		explicit MultiwayFullBlueprintRowView(const MultiwayBlueprintRow& row) noexcept
			: infoset(row.infoset), bucket(row.bucket), action_menu_id(row.action_menu_id), actions(row.actions.data()), action_count(row.actions.size())
		{
		}
	};

	/** Validated dimensions and identity for a streamed full-blueprint write. */
	struct MultiwayFullBlueprintStreamPlan
	{
		std::uint32_t schema_version = MULTIWAY_FULL_BLUEPRINT_SCHEMA_VERSION;
		MultiwayModelIdentity identity{};
		MultiwayBlueprintTrainingMetadata training{};
		std::uint64_t row_count = 0U;
		std::uint64_t action_count = 0U;
		std::uint64_t serialized_bytes = 0U;

		/** Checks plan schema, identity, and byte/count invariants. */
		void validate() const;
	};

	/** Half-open row interval assigned to one shard. */
	struct MultiwayFullBlueprintShardRange
	{
		std::uint64_t first_row = 0U;
		std::uint64_t row_count = 0U;
	};

	/** Row partition and per-shard serialized-size limit. */
	struct MultiwayFullBlueprintShardLayout
	{
		std::uint64_t maximum_serialized_bytes = 0U;
		std::uint64_t maximum_rows_per_shard = 0U;
		std::vector<MultiwayFullBlueprintShardRange> ranges;

		/** Checks that ranges cover planned rows within the configured bound. */
		void validate(const MultiwayFullBlueprintStreamPlan& plan) const;
	};

	/** Estimated payload and manifest sizes for a sharded export. */
	struct MultiwayFullBlueprintShardedSizeEstimate
	{
		std::uint64_t shard_bytes = 0U;
		std::uint64_t manifest_bytes = 0U;
		std::uint64_t total_bytes = 0U;
	};

	inline constexpr std::uint32_t MULTIWAY_FULL_BLUEPRINT_SHARD_MANIFEST_SCHEMA_VERSION = 1U;
	inline constexpr std::uint64_t MULTIWAY_FULL_BLUEPRINT_MAX_SHARDS = 65'536U;

	/** Relative path and verified range metadata for one shard file. */
	struct MultiwayFullBlueprintShardReference
	{
		std::string relative_path;
		MultiwayFullBlueprintShardRange range{};
		std::uint64_t action_count = 0U;
		std::uint64_t serialized_bytes = 0U;
		std::uint64_t payload_hash = 0U;
	};

	/** Identity, counts, hashes, and shard list for a full-blueprint export. */
	struct MultiwayFullBlueprintShardManifest
	{
		std::uint32_t schema_version = MULTIWAY_FULL_BLUEPRINT_SHARD_MANIFEST_SCHEMA_VERSION;
		MultiwayModelIdentity identity{};
		MultiwayBlueprintTrainingMetadata training{};
		std::uint64_t row_count = 0U;
		std::uint64_t action_count = 0U;
		std::uint64_t maximum_serialized_bytes = 0U;
		std::uint64_t policy_content_hash = 0U;
		std::vector<MultiwayFullBlueprintShardReference> shards;

		/** Checks schema, identity, contiguous ranges, and aggregate counts. */
		void validate() const;
	};

	/** Builds a validated stream plan for the requested row and action counts. */
	[[nodiscard]] MultiwayFullBlueprintStreamPlan make_multiway_full_blueprint_stream_plan(
		const MultiwayModelIdentity& identity,
		const MultiwayBlueprintTrainingMetadata& training,
		std::uint64_t row_count,
		std::uint64_t action_count);

	/** Partitions a stream plan into shards under the serialized-byte limit. */
	[[nodiscard]] MultiwayFullBlueprintShardLayout make_multiway_full_blueprint_shard_layout(
		const MultiwayFullBlueprintStreamPlan& plan,
		std::uint64_t maximum_serialized_bytes);
	/** Estimates total sharded output size, including the manifest. */
	[[nodiscard]] MultiwayFullBlueprintShardedSizeEstimate estimate_multiway_full_blueprint_sharded_size(
		const MultiwayFullBlueprintStreamPlan& plan,
		const MultiwayFullBlueprintShardLayout& layout,
		const std::filesystem::path& manifest_path);

	/** Incremental hasher for the complete encoded full-blueprint payload. */
	class MultiwayFullBlueprintPayloadHasher
	{
	public:
		/** Starts a payload hash with artifact-level header values. */
		void begin(
			std::uint32_t schema_version,
			const MultiwayModelIdentity& identity,
			const MultiwayBlueprintTrainingMetadata& training,
			std::uint64_t row_count) noexcept;
		/** Adds a row to the payload hash in stream order. */
		void append_row(const MultiwayFullBlueprintRowView& row) noexcept;
		/** Returns the current payload hash. */
		[[nodiscard]] std::uint64_t finish() const noexcept;

	private:
		std::uint64_t hash_ = 0U;
	};

	/** Incremental hash of policy rows independent of artifact encoding. */
	class MultiwayFullBlueprintPolicyContentHasher
	{
	public:
		/** Starts a policy-content hash for the expected row count. */
		void begin(std::uint64_t row_count) noexcept;
		/** Adds a row to the policy-content hash in stream order. */
		void append_row(const MultiwayFullBlueprintRowView& row) noexcept;
		/** Returns the current policy-content hash. */
		[[nodiscard]] std::uint64_t finish() const noexcept;

	private:
		std::uint64_t hash_ = 0U;
	};

	/** Optional injected failure stage for streaming-writer reliability checks. */
	enum class MultiwayFullBlueprintStreamFailurePoint : std::uint8_t
	{
		None = 0U,
		Write,
		Close,
		Publication,
		ManifestPublication
	};

	/** Aggregated timing and flush measurements for streaming output. */
	struct MultiwayFullBlueprintWriterProfile
	{
		std::uint64_t sampled_rows = 0U;
		std::uint64_t sampled_validation_nanoseconds = 0U;
		std::uint64_t sampled_payload_hash_nanoseconds = 0U;
		std::uint64_t sampled_policy_hash_nanoseconds = 0U;
		std::uint64_t sampled_encoding_nanoseconds = 0U;
		std::uint64_t flushes = 0U;
	};

	/** Strict-order, failure-injection, and optional profiling settings. */
	struct MultiwayFullBlueprintStreamOptions
	{
		bool require_strict_row_order = true;
		MultiwayFullBlueprintStreamFailurePoint failure_point =
			MultiwayFullBlueprintStreamFailurePoint::None;
		// Optional caller-owned aggregate, shared across shards.
		MultiwayFullBlueprintWriterProfile* profile = nullptr;
	};

	/** Counts, hashes, size, and publication method for one stream. */
	struct MultiwayFullBlueprintStreamResult
	{
		std::uint64_t row_count = 0U;
		std::uint64_t action_count = 0U;
		std::uint64_t serialized_bytes = 0U;
		std::uint64_t payload_hash = 0U;
		std::uint64_t policy_content_hash = 0U;
		core::AtomicPublishMode publication_mode = core::AtomicPublishMode::Unknown;
	};

	/** Current encoded and output-stream byte progress. */
	struct MultiwayFullBlueprintStreamProgress
	{
		std::uint64_t encoded_bytes = 0U;
		// Bytes handed to the output stream, not necessarily durable on disk.
		std::uint64_t stream_write_bytes = 0U;
	};

	/** Counts, hashes, and publication details for a sharded stream. */
	struct MultiwayFullBlueprintShardedStreamResult
	{
		std::uint64_t row_count = 0U;
		std::uint64_t action_count = 0U;
		std::uint64_t serialized_bytes = 0U;
		std::uint64_t policy_content_hash = 0U;
		std::uint64_t shard_count = 0U;
		std::uint64_t manifest_bytes = 0U;
		core::AtomicPublishMode publication_mode = core::AtomicPublishMode::Unknown;
	};

	/** Writes a full blueprint incrementally to one atomically published file. */
	class MultiwayFullBlueprintStreamWriter
	{
	public:
		/** Creates a writer for the supplied plan and destination. */
		MultiwayFullBlueprintStreamWriter(
			const std::filesystem::path& destination,
			const MultiwayFullBlueprintStreamPlan& plan,
			MultiwayFullBlueprintStreamOptions options = {});
		/** Releases writer resources and discards unpublished temporary output. */
		~MultiwayFullBlueprintStreamWriter();

		MultiwayFullBlueprintStreamWriter(const MultiwayFullBlueprintStreamWriter&) = delete;
		MultiwayFullBlueprintStreamWriter& operator=(const MultiwayFullBlueprintStreamWriter&) = delete;

		/** Encodes the next row, enforcing ordering when enabled in the options. */
		void write_row(const MultiwayFullBlueprintRowView& row);
		/** Returns progress without finalizing the stream. */
		[[nodiscard]] MultiwayFullBlueprintStreamProgress progress() const noexcept;
		/** Completes validation, hashing, and atomic publication. */
		[[nodiscard]] MultiwayFullBlueprintStreamResult finish();

	private:
		struct Impl;
		std::unique_ptr<Impl> impl_;
	};

	/** Writes bounded shard files and publishes their manifest after all shards. */
	class MultiwayFullBlueprintShardedStreamWriter
	{
	public:
		/** Creates a writer using the supplied row layout and shard action counts. */
		MultiwayFullBlueprintShardedStreamWriter(
			const std::filesystem::path& manifest_path,
			const MultiwayFullBlueprintStreamPlan& plan,
			MultiwayFullBlueprintShardLayout layout,
			std::vector<std::uint64_t> shard_action_counts,
			MultiwayFullBlueprintStreamOptions options = {});
		/** Releases writer resources and discards unpublished temporary output. */
		~MultiwayFullBlueprintShardedStreamWriter();
		MultiwayFullBlueprintShardedStreamWriter(const MultiwayFullBlueprintShardedStreamWriter&) = delete;
		MultiwayFullBlueprintShardedStreamWriter& operator=(const MultiwayFullBlueprintShardedStreamWriter&) = delete;

		/** Encodes the next row in global row order. */
		void write_row(const MultiwayFullBlueprintRowView& row);
		/** Returns progress without finalizing the stream. */
		[[nodiscard]] MultiwayFullBlueprintStreamProgress progress() const noexcept;
		/** Finalizes shards, hashes, manifest, and atomic publication. */
		[[nodiscard]] MultiwayFullBlueprintShardedStreamResult finish();

	private:
		struct Impl;
		std::unique_ptr<Impl> impl_;
	};

	/** High-level save, load, verification, and hashing for full artifacts. */
	class MultiwayFullBlueprintArtifacts
	{
	public:
		/** Validates and atomically writes an in-memory artifact. */
		static void save_atomic(const std::filesystem::path& path, const MultiwayFullBlueprintArtifact& artifact);
		/** Streams callback-provided rows into a single artifact file. */
		[[nodiscard]] static MultiwayFullBlueprintStreamResult save_atomic_streaming(
			const std::filesystem::path& path,
			const MultiwayFullBlueprintStreamPlan& plan,
			bool (*produce_row)(
				std::uint64_t index,
				MultiwayFullBlueprintRowView& row,
				void* context),
			void* context = nullptr);
		/** Streams callback-provided rows into bounded shards and a manifest. */
		[[nodiscard]] static MultiwayFullBlueprintShardedStreamResult save_atomic_sharded_streaming(
			const std::filesystem::path& manifest_path,
			const MultiwayFullBlueprintStreamPlan& plan,
			std::uint64_t maximum_shard_bytes,
			bool (*produce_row)(std::uint64_t index, MultiwayFullBlueprintRowView& row, void* context),
			void* context = nullptr,
			MultiwayFullBlueprintStreamOptions options = {});
		/** Loads a shard manifest after checking exact model identity. */
		[[nodiscard]] static MultiwayFullBlueprintShardManifest load_sharded_manifest(
			const std::filesystem::path& manifest_path,
			const MultiwayModelIdentity& expected_identity);
		/** Loads a manifest using runtime policy-lookup compatibility rules. */
		[[nodiscard]] static MultiwayFullBlueprintShardManifest load_sharded_manifest_for_policy_lookup(
			const std::filesystem::path& manifest_path,
			const MultiwayModelIdentity& expected_identity);
		/** Loads a manifest with optional tolerance for runtime identity differences. */
		[[nodiscard]] static MultiwayFullBlueprintShardManifest load_sharded_manifest(
			const std::filesystem::path& manifest_path,
			const MultiwayModelIdentity& expected_identity,
			bool allow_runtime_identity_difference);
		/** Reports whether path contains a recognized sharded-artifact manifest. */
		[[nodiscard]] static bool is_sharded_manifest(
			const std::filesystem::path& path);
		/** Verifies shard payloads, ranges, hashes, and model identity. */
		static void verify_sharded_artifact(
			const std::filesystem::path& manifest_path,
			const MultiwayModelIdentity& expected_identity);
		/** Loads and verifies a full artifact for exact model identity. */
		[[nodiscard]] static MultiwayFullBlueprintArtifact load_verified(
			const std::filesystem::path& path,
			const MultiwayModelIdentity& expected_identity);
		/** Loads and verifies a full artifact for runtime policy lookup. */
		[[nodiscard]] static MultiwayFullBlueprintArtifact load_verified_for_policy_lookup(
			const std::filesystem::path& path,
			const MultiwayModelIdentity& expected_identity);
		/** Loads a full artifact with optional runtime identity tolerance. */
		[[nodiscard]] static MultiwayFullBlueprintArtifact load_verified(
			const std::filesystem::path& path,
			const MultiwayModelIdentity& expected_identity,
			bool allow_runtime_identity_difference);
		/** Computes the encoded payload hash. */
		[[nodiscard]] static std::uint64_t payload_hash(const MultiwayFullBlueprintArtifact& artifact) noexcept;
		/** Computes a stable hash over policy rows and action probabilities. */
		[[nodiscard]] static std::uint64_t policy_content_hash(const MultiwayFullBlueprintArtifact& artifact);
	};

	/** High-level save and verified-load operations for compact root snapshots. */
	class MultiwayBlueprintArtifacts
	{
	public:
		// Writes the compact checkpoint and its manifest. A partially published
		// pair fails validation on load rather than being accepted optimistically.
		/** Writes a snapshot and its identity manifest as an atomic artifact pair. */
		static void save_atomic(
			const std::filesystem::path& checkpoint_path,
			const MultiwayBlueprintSnapshot& snapshot);
		/** Loads and verifies a snapshot and manifest against expected identity. */
		[[nodiscard]] static MultiwayVerifiedBlueprintArtifact load_verified(
			const std::filesystem::path& checkpoint_path,
			const MultiwayModelIdentity& expected_identity);
		/** Tries the primary artifact pair, then the known-good pair on failure. */
		[[nodiscard]] static MultiwayVerifiedBlueprintArtifact load_with_fallback(
			const std::filesystem::path& primary_checkpoint_path,
			const std::filesystem::path& known_good_checkpoint_path,
			const MultiwayModelIdentity& expected_identity);
		/** Computes the stable content hash for a compact snapshot. */
		[[nodiscard]] static std::uint64_t snapshot_hash(
			const MultiwayBlueprintSnapshot& snapshot) noexcept;
	};

	// Public audit record. It intentionally has no cards, ranges, raw seeds, or
	// worker state. Probabilities are quantized to preserve a stable log boundary.
	/** One public action and quantized policy probability in an audit record. */
	struct MultiwayPublicDecisionPolicy
	{
		MultiwayActionDescriptor action{};
		std::uint16_t probability = 0;
	};

	/** Public-only resolver decision suitable for an audit log. */
	struct MultiwayPublicDecisionLog
	{
		std::uint32_t schema_version = MULTIWAY_PUBLIC_DECISION_LOG_SCHEMA_VERSION;
		MultiwayModelIdentity identity{};
		std::uint64_t public_state_id = 0;
		std::uint64_t decision_index = 0;
		PlayerId acting_seat = -1;
		MultiwayActionDescriptor sampled_action{};
		MultiwayResolverStatus resolver_status = MultiwayResolverStatus::InvalidRequest;
		MultiwayPolicyProvenance policy_provenance = MultiwayPolicyProvenance::None;
		MultiwayResolverEngine search_engine = MultiwayResolverEngine::NoRuntimeSearch;
		std::uint64_t search_engine_version = MULTIWAY_NO_RUNTIME_SEARCH_ENGINE_VERSION;
		bool used_fallback = false;
		std::vector<MultiwayPublicDecisionPolicy> policy;

		/** Checks schema, public decision identity, and policy consistency. */
		void validate() const;
	};

	/** Builds an auditable public decision record from a resolver request/result. */
	[[nodiscard]] MultiwayPublicDecisionLog make_multiway_public_decision_log(
		const MultiwayResolverRequest& request,
		const MultiwayResolverResult& result,
		std::uint64_t decision_index);

	// Sealed, public-only replay envelope. It carries the per-decision seeds from
	// the public replay history while excluding hero cards and opponent ranges.
	/** Integrity-sealed public replay history with deterministic decision seeds. */
	struct MultiwayProtectedReplayRecord
	{
		std::uint32_t schema_version = MULTIWAY_PROTECTED_REPLAY_SCHEMA_VERSION;
		MultiwayModelIdentity identity{};
		MultiwayHandHistory public_history{};
		std::vector<std::uint64_t> decision_seeds;
		std::uint64_t integrity_hash = 0;

		/** Builds a record and derives per-decision seeds from the public history. */
		[[nodiscard]] static MultiwayProtectedReplayRecord from_history(
			const MultiwayModelIdentity& identity,
			const MultiwayHandHistory& public_history);
		/** Recomputes the integrity hash after public history or seeds change. */
		void seal() noexcept;
		/** Checks schema, history, seed count, and integrity hash. */
		void validate() const;
	};

} // namespace texas::solver::multiway
