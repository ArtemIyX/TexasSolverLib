#pragma once

/** @file
 * @brief Multiway baseline bucket features, artifact serialization and inspection.
 */

#include "solver/multiway/abstraction/multiway_bucket_model.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <vector>

namespace texas::solver::multiway
{
	struct MultiwayProcessMemorySnapshotCollection;

	/** @brief Current multiway bucket artifact schema version. */
	inline constexpr std::uint32_t MULTIWAY_BUCKET_ARTIFACT_SCHEMA_VERSION = 5U;
	/** @brief Previous supported artifact schema version. */
	inline constexpr std::uint32_t MULTIWAY_BUCKET_ARTIFACT_SCHEMA_V4 = 4U;

	// Versioned deterministic baseline used until trained clustering artifacts are
	// introduced. The counts are part of the model identity through blueprint config.
	/** @brief Deterministic bucket counts and feature version for baseline artifacts. */
	struct MultiwayBucketBaselineProfile
	{
		std::uint32_t schema_version = MULTIWAY_BUCKET_ARTIFACT_SCHEMA_VERSION;
		std::uint64_t feature_version = 1U;
		std::uint32_t flop_bucket_count = 96U;
		std::uint32_t turn_bucket_count = 128U;
		std::uint32_t river_bucket_count = 192U;

		/** @brief Returns the standard profile for current schema. */
		[[nodiscard]] static MultiwayBucketBaselineProfile standard() noexcept;
		/** @brief Validates schema and per-street bucket counts. */
		void validate() const;
		/** @brief Returns bucket count for street. */
		[[nodiscard]] std::uint32_t bucket_count(core::Street street) const;
	};

	// Compact, allocation-free features calculated for a live hole pair on a
	// canonical sorted board. They are stable artifact inputs, not a learned model.
	/** @brief Compact card and board features used to assign a baseline bucket. */
	struct MultiwayBucketFeatures
	{
		std::uint16_t board_rank_mask = 0U;
		std::uint8_t board_suit_mask = 0U;
		std::uint8_t board_pair_count = 0U;
		std::uint8_t board_size = 0U;
		std::uint8_t hole_high_rank = 0U;
		std::uint8_t hole_low_rank = 0U;
		std::uint8_t hole_suited = 0U;
		std::uint8_t hole_pairs_board = 0U;
		std::uint8_t hole_suit_matches_board = 0U;
	};

	/** @brief Street and canonical board requested from a bucket artifact. */
	struct MultiwayBucketBoardRequest
	{
		core::Street street = core::Street::Preflop;
		std::vector<std::uint8_t> canonical_board;
	};

	/** @brief Checks a vector board's street length, card validity and canonical order. */
	[[nodiscard]] bool is_multiway_canonical_board(
		core::Street street,
		const std::vector<std::uint8_t>& board) noexcept;
	/** @brief Checks a non-owning board view for canonical street shape. */
	[[nodiscard]] bool is_multiway_canonical_board(
		core::Street street,
		MultiwayBoardView board) noexcept;
	/** @brief Extracts deterministic features for a hole pair on a canonical board. */
	[[nodiscard]] MultiwayBucketFeatures make_multiway_bucket_features(
		core::Street street,
		const std::vector<std::uint8_t>& canonical_board,
		const std::array<std::uint8_t, 2>& hole);
	/** @brief Assigns a baseline bucket from compact features and profile. */
	[[nodiscard]] std::uint32_t assign_multiway_baseline_bucket(
		const MultiwayBucketFeatures& features,
		const MultiwayBucketBaselineProfile& profile,
		core::Street street);

	/** @brief Builds a bucket table from an owned canonical board vector. */
	[[nodiscard]] MultiwayBucketTable build_multiway_baseline_bucket_table(
		const MultiwayModelIdentity& identity,
		core::Street street,
		std::vector<std::uint8_t> canonical_board,
		const MultiwayBucketBaselineProfile& profile = MultiwayBucketBaselineProfile::standard());
	// Fixed-board variant for catalog generation. The caller supplies a validated
	// sorted board of the street's exact length.
	/** @brief Builds a bucket table from fixed-capacity canonical board storage. */
	[[nodiscard]] MultiwayBucketTable build_multiway_baseline_bucket_table_fixed_board(
		const MultiwayModelIdentity& identity,
		core::Street street,
		const std::array<std::uint8_t, 5U>& canonical_board,
		const MultiwayBucketBaselineProfile& profile = MultiwayBucketBaselineProfile::standard());
	/** @brief Builds a registry for a requested set of canonical boards. */
	[[nodiscard]] MultiwayBucketRegistry build_multiway_baseline_bucket_registry(
		const MultiwayModelIdentity& identity,
		const std::vector<MultiwayBucketBoardRequest>& boards,
		const MultiwayBucketBaselineProfile& profile = MultiwayBucketBaselineProfile::standard());

	// Verifies an artifact contains every requested canonical board exactly once.
	// It is an offline load/build check and is never used by traversal.
	/** @brief Requires each requested canonical board to appear exactly once. */
	void validate_multiway_bucket_coverage(
		const MultiwayBucketRegistry& registry,
		const std::vector<MultiwayBucketBoardRequest>& required_boards);
	/** @brief Requires registry identity and every table's bucket count to match the requested profile. */
	void validate_multiway_bucket_registry_profile(
		const MultiwayBucketRegistry& registry,
		const MultiwayModelIdentity& expected_identity,
		const MultiwayBucketBaselineProfile& expected_profile);

	/** @brief Serializes a registry into its versioned binary representation. */
	[[nodiscard]] std::vector<std::uint8_t> serialize_multiway_bucket_registry(
		const MultiwayBucketRegistry& registry);
	/** @brief Deserializes a versioned registry from bytes. */
	[[nodiscard]] MultiwayBucketRegistry deserialize_multiway_bucket_registry(
		const std::vector<std::uint8_t>& bytes);
	// Streaming load avoids materializing a second serialized copy alongside the
	// immutable registry required by traversal.
	/** @brief Loads a registry from disk using streaming deserialization. */
	[[nodiscard]] MultiwayBucketRegistry load_multiway_bucket_registry(
		const std::filesystem::path& path,
		MultiwayProcessMemorySnapshotCollection* process_memory_snapshots = nullptr);

	/** @brief Hash computation requested during offline bucket artifact inspection. */
	enum class MultiwayBucketInspectionHashMode : std::uint8_t
	{
		Parallel,
		Legacy,
		Both
	};

	/** @brief Metadata and payload hashes observed while inspecting an artifact. */
	struct MultiwayBucketArtifactInspection
	{
		std::uint64_t flop_tables = 0U;
		std::uint64_t turn_tables = 0U;
		std::uint64_t river_tables = 0U;
		std::uint32_t flop_bucket_count = 0U;
		std::uint32_t turn_bucket_count = 0U;
		std::uint32_t river_bucket_count = 0U;
		std::uint64_t live_assignments = 0U;
		std::uint64_t payload_hash = 0U;
		std::uint64_t parallel_payload_hash = 0U;
		std::uint32_t effective_threads = 1U;
		MultiwayBucketInspectionHashMode hash_mode = MultiwayBucketInspectionHashMode::Legacy;
		MultiwayModelIdentity identity{};
	};

	/** @brief Thread and hash settings for artifact inspection. */
	struct MultiwayBucketInspectionOptions
	{
		std::uint32_t requested_threads = 0U;
		MultiwayBucketInspectionHashMode hash_mode = MultiwayBucketInspectionHashMode::Parallel;
	};

	/** @brief Default number of tables between progress callbacks. */
	inline constexpr std::uint64_t MULTIWAY_BUCKET_INSPECTION_DEFAULT_PROGRESS_INTERVAL = 100000U;
	// Parallel inspection emits 0/total before a serial fallback or a second,
	// legacy stream-hash pass.
	/** @brief Callback for completed and total tables during artifact inspection. */
	using MultiwayBucketInspectionProgressCallback = std::function<void(
		std::uint64_t completed_tables, std::uint64_t total_tables)>;

	/** @brief Inspects a bucket artifact using default hash options. */
	[[nodiscard]] MultiwayBucketArtifactInspection inspect_multiway_bucket_artifact(
		const std::filesystem::path& path,
		const MultiwayModelIdentity& expected_identity,
		MultiwayBucketInspectionProgressCallback progress_callback = {},
		std::uint64_t progress_interval = MULTIWAY_BUCKET_INSPECTION_DEFAULT_PROGRESS_INTERVAL);
	/** @brief Inspects a bucket artifact with explicit hash and thread settings. */
	[[nodiscard]] MultiwayBucketArtifactInspection inspect_multiway_bucket_artifact(
		const std::filesystem::path& path, const MultiwayModelIdentity& expected_identity,
		const MultiwayBucketInspectionOptions& options,
		MultiwayBucketInspectionProgressCallback progress_callback = {},
		std::uint64_t progress_interval = MULTIWAY_BUCKET_INSPECTION_DEFAULT_PROGRESS_INTERVAL);

} // namespace texas::solver::multiway
