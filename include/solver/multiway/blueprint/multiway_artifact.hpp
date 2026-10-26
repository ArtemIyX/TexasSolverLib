#pragma once

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
	struct MultiwayBlueprintManifest
	{
		std::uint32_t schema_version = MULTIWAY_BLUEPRINT_MANIFEST_SCHEMA_VERSION;
		MultiwayModelIdentity identity{};
		std::uint64_t snapshot_hash = 0;

		void validate() const;
	};

	enum class MultiwayArtifactSource : std::uint8_t
	{
		Primary,
		KnownGoodFallback,
	};

	struct MultiwayVerifiedBlueprintArtifact
	{
		MultiwayBlueprintSnapshot snapshot{};
		MultiwayBlueprintManifest manifest{};
		MultiwayArtifactSource source = MultiwayArtifactSource::Primary;

		void validate(const MultiwayModelIdentity& expected_identity) const;
	};

	inline constexpr std::uint32_t MULTIWAY_FULL_BLUEPRINT_SCHEMA_VERSION = 2U;
	inline constexpr std::uint32_t MULTIWAY_POLICY_CONTENT_HASH_VERSION = 1U;
	inline constexpr std::uint32_t MULTIWAY_FULL_BLUEPRINT_MAX_ACTIONS_PER_ROW = 64U;

	// Full runtime lookup payload. It is independent of the compact root
	// snapshot so hosts can retain the latter as a compatible fallback.
	struct MultiwayFullBlueprintArtifact
	{
		std::uint32_t schema_version = MULTIWAY_FULL_BLUEPRINT_SCHEMA_VERSION;
		MultiwayModelIdentity identity{};
		MultiwayBlueprintTrainingMetadata training{};
		std::vector<MultiwayBlueprintRow> rows;
		std::uint64_t payload_hash = 0U;

		void validate() const;
		[[nodiscard]] MultiwayMemoryComponent memory_snapshot() const noexcept;
	};

	struct MultiwayFullBlueprintRowView
	{
		MultiwayInfosetId infoset{};
		std::uint32_t bucket = 0U;
		std::uint64_t action_menu_id = 0U;
		const MultiwayQuantizedRootAction* actions = nullptr;
		std::size_t action_count = 0U;

		MultiwayFullBlueprintRowView() = default;
		explicit MultiwayFullBlueprintRowView(const MultiwayBlueprintRow& row) noexcept
			: infoset(row.infoset), bucket(row.bucket), action_menu_id(row.action_menu_id), actions(row.actions.data()), action_count(row.actions.size())
		{
		}
	};

	struct MultiwayFullBlueprintStreamPlan
	{
		std::uint32_t schema_version = MULTIWAY_FULL_BLUEPRINT_SCHEMA_VERSION;
		MultiwayModelIdentity identity{};
		MultiwayBlueprintTrainingMetadata training{};
		std::uint64_t row_count = 0U;
		std::uint64_t action_count = 0U;
		std::uint64_t serialized_bytes = 0U;

		void validate() const;
	};

	struct MultiwayFullBlueprintShardRange
	{
		std::uint64_t first_row = 0U;
		std::uint64_t row_count = 0U;
	};

	struct MultiwayFullBlueprintShardLayout
	{
		std::uint64_t maximum_serialized_bytes = 0U;
		std::uint64_t maximum_rows_per_shard = 0U;
		std::vector<MultiwayFullBlueprintShardRange> ranges;

		void validate(const MultiwayFullBlueprintStreamPlan& plan) const;
	};

	inline constexpr std::uint32_t MULTIWAY_FULL_BLUEPRINT_SHARD_MANIFEST_SCHEMA_VERSION = 1U;
	inline constexpr std::uint64_t MULTIWAY_FULL_BLUEPRINT_MAX_SHARDS = 65'536U;

	struct MultiwayFullBlueprintShardReference
	{
		std::string relative_path;
		MultiwayFullBlueprintShardRange range{};
		std::uint64_t action_count = 0U;
		std::uint64_t serialized_bytes = 0U;
		std::uint64_t payload_hash = 0U;
	};

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

		void validate() const;
	};

	[[nodiscard]] MultiwayFullBlueprintStreamPlan make_multiway_full_blueprint_stream_plan(
		const MultiwayModelIdentity& identity,
		const MultiwayBlueprintTrainingMetadata& training,
		std::uint64_t row_count,
		std::uint64_t action_count);

	[[nodiscard]] MultiwayFullBlueprintShardLayout make_multiway_full_blueprint_shard_layout(
		const MultiwayFullBlueprintStreamPlan& plan,
		std::uint64_t maximum_serialized_bytes);

	class MultiwayFullBlueprintPayloadHasher
	{
	public:
		void begin(
			std::uint32_t schema_version,
			const MultiwayModelIdentity& identity,
			const MultiwayBlueprintTrainingMetadata& training,
			std::uint64_t row_count) noexcept;
		void append_row(const MultiwayFullBlueprintRowView& row) noexcept;
		[[nodiscard]] std::uint64_t finish() const noexcept;

	private:
		std::uint64_t hash_ = 0U;
	};

	class MultiwayFullBlueprintPolicyContentHasher
	{
	public:
		void begin(std::uint64_t row_count) noexcept;
		void append_row(const MultiwayFullBlueprintRowView& row) noexcept;
		[[nodiscard]] std::uint64_t finish() const noexcept;

	private:
		std::uint64_t hash_ = 0U;
	};

	enum class MultiwayFullBlueprintStreamFailurePoint : std::uint8_t
	{
		None = 0U,
		Write,
		Close,
		Publication,
		ManifestPublication
	};

	struct MultiwayFullBlueprintStreamOptions
	{
		bool require_strict_row_order = true;
		MultiwayFullBlueprintStreamFailurePoint failure_point =
			MultiwayFullBlueprintStreamFailurePoint::None;
	};

	struct MultiwayFullBlueprintStreamResult
	{
		std::uint64_t row_count = 0U;
		std::uint64_t action_count = 0U;
		std::uint64_t serialized_bytes = 0U;
		std::uint64_t payload_hash = 0U;
		std::uint64_t policy_content_hash = 0U;
		core::AtomicPublishMode publication_mode = core::AtomicPublishMode::Unknown;
	};

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

	class MultiwayFullBlueprintStreamWriter
	{
	public:
		MultiwayFullBlueprintStreamWriter(
			const std::filesystem::path& destination,
			const MultiwayFullBlueprintStreamPlan& plan,
			MultiwayFullBlueprintStreamOptions options = {});
		~MultiwayFullBlueprintStreamWriter();

		MultiwayFullBlueprintStreamWriter(const MultiwayFullBlueprintStreamWriter&) = delete;
		MultiwayFullBlueprintStreamWriter& operator=(const MultiwayFullBlueprintStreamWriter&) = delete;

		void write_row(const MultiwayFullBlueprintRowView& row);
		[[nodiscard]] MultiwayFullBlueprintStreamResult finish();

	private:
		struct Impl;
		std::unique_ptr<Impl> impl_;
	};

	class MultiwayFullBlueprintArtifacts
	{
	public:
		static void save_atomic(const std::filesystem::path& path, const MultiwayFullBlueprintArtifact& artifact);
		[[nodiscard]] static MultiwayFullBlueprintStreamResult save_atomic_streaming(
			const std::filesystem::path& path,
			const MultiwayFullBlueprintStreamPlan& plan,
			bool (*produce_row)(
				std::uint64_t index,
				MultiwayFullBlueprintRowView& row,
				void* context),
			void* context = nullptr);
		[[nodiscard]] static MultiwayFullBlueprintShardedStreamResult save_atomic_sharded_streaming(
			const std::filesystem::path& manifest_path,
			const MultiwayFullBlueprintStreamPlan& plan,
			std::uint64_t maximum_shard_bytes,
			bool (*produce_row)(std::uint64_t index, MultiwayFullBlueprintRowView& row, void* context),
			void* context = nullptr,
			MultiwayFullBlueprintStreamOptions options = {});
		[[nodiscard]] static MultiwayFullBlueprintShardManifest load_sharded_manifest(
			const std::filesystem::path& manifest_path,
			const MultiwayModelIdentity& expected_identity);
		static void verify_sharded_artifact(
			const std::filesystem::path& manifest_path,
			const MultiwayModelIdentity& expected_identity);
		[[nodiscard]] static MultiwayFullBlueprintArtifact load_verified(
			const std::filesystem::path& path,
			const MultiwayModelIdentity& expected_identity);
		[[nodiscard]] static std::uint64_t payload_hash(const MultiwayFullBlueprintArtifact& artifact) noexcept;
		[[nodiscard]] static std::uint64_t policy_content_hash(const MultiwayFullBlueprintArtifact& artifact);
	};

	class MultiwayBlueprintArtifacts
	{
	public:
		// Writes the compact checkpoint and its manifest. A partially published
		// pair fails validation on load rather than being accepted optimistically.
		static void save_atomic(
			const std::filesystem::path& checkpoint_path,
			const MultiwayBlueprintSnapshot& snapshot);
		[[nodiscard]] static MultiwayVerifiedBlueprintArtifact load_verified(
			const std::filesystem::path& checkpoint_path,
			const MultiwayModelIdentity& expected_identity);
		[[nodiscard]] static MultiwayVerifiedBlueprintArtifact load_with_fallback(
			const std::filesystem::path& primary_checkpoint_path,
			const std::filesystem::path& known_good_checkpoint_path,
			const MultiwayModelIdentity& expected_identity);
		[[nodiscard]] static std::uint64_t snapshot_hash(
			const MultiwayBlueprintSnapshot& snapshot) noexcept;
	};

	// Public audit record. It intentionally has no cards, ranges, raw seeds, or
	// worker state. Probabilities are quantized to preserve a stable log boundary.
	struct MultiwayPublicDecisionPolicy
	{
		MultiwayActionDescriptor action{};
		std::uint16_t probability = 0;
	};

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

		void validate() const;
	};

	[[nodiscard]] MultiwayPublicDecisionLog make_multiway_public_decision_log(
		const MultiwayResolverRequest& request,
		const MultiwayResolverResult& result,
		std::uint64_t decision_index);

	// Sealed, public-only replay envelope. It carries the per-decision seeds from
	// the public replay history while excluding hero cards and opponent ranges.
	struct MultiwayProtectedReplayRecord
	{
		std::uint32_t schema_version = MULTIWAY_PROTECTED_REPLAY_SCHEMA_VERSION;
		MultiwayModelIdentity identity{};
		MultiwayHandHistory public_history{};
		std::vector<std::uint64_t> decision_seeds;
		std::uint64_t integrity_hash = 0;

		[[nodiscard]] static MultiwayProtectedReplayRecord from_history(
			const MultiwayModelIdentity& identity,
			const MultiwayHandHistory& public_history);
		void seal() noexcept;
		void validate() const;
	};

} // namespace texas::solver::multiway
