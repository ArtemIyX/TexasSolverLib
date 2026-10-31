#pragma once

/** @file
 * @brief Offline future-bucket features, profiles and artifacts for multiway play.
 */

#include "solver/multiway/abstraction/multiway_bucket_artifact.hpp"
#include "solver/multiway/abstraction/multiway_future_bucket_types.hpp"

#include <array>
#include <cstdint>
#include <filesystem>

namespace texas::solver::multiway
{

	/** @brief Schema version for future-bucket artifacts. */
	inline constexpr std::uint32_t MULTIWAY_FUTURE_BUCKET_ARTIFACT_SCHEMA_VERSION = 1U;

	/** @brief Feature, clustering and bucket-count settings for an offline model. */
	struct MultiwayFutureBucketProfile
	{
		std::uint64_t feature_version = 1U;
		std::uint64_t clustering_version = 1U;
		std::uint64_t seed = 1U;
		std::uint32_t lloyd_iterations = 4U;
		std::uint32_t flop_bucket_count = 96U;
		std::uint32_t turn_bucket_count = 128U;
		std::uint32_t river_bucket_count = 192U;

		/** @brief Validates profile versions, iterations and bucket counts. */
		void validate() const;
		/** @brief Returns the configured bucket count for a street. */
		[[nodiscard]] std::uint32_t bucket_count(core::Street street) const;
	};

	/** @brief Extracts a stable feature vector for one compact private hand and board. */
	[[nodiscard]] MultiwayFutureBucketFeatures make_multiway_future_bucket_features(
		core::Street street,
		const std::vector<std::uint8_t>& canonical_board,
		const std::array<std::uint8_t, 2>& compact_hole,
		std::uint64_t feature_version = 1U);

	// Immutable consumer with explicit producer provenance. Cluster construction
	// is offline; live callers only use registry lookup.
	/** @brief Immutable future-bucket profile and board registry. */
	class MultiwayFutureBucketArtifact
	{
	public:
		/** @brief Binds the producer profile to its bucket registry. */
		MultiwayFutureBucketArtifact(MultiwayFutureBucketProfile profile, MultiwayBucketRegistry registry);

		/** @brief Returns the offline producer profile. */
		[[nodiscard]] const MultiwayFutureBucketProfile& profile() const noexcept { return profile_; }
		/** @brief Returns the immutable board registry. */
		[[nodiscard]] const MultiwayBucketRegistry& registry() const noexcept { return registry_; }
		/** @brief Looks up a bucket by street, board and compact private hand. */
		[[nodiscard]] std::uint32_t lookup(
			core::Street street, const std::vector<std::uint8_t>& board,
			const std::array<std::uint8_t, 2>& hole) const;

	private:
		MultiwayFutureBucketProfile profile_{};
		MultiwayBucketRegistry registry_;
	};

	/** @brief Builds an offline artifact for requested boards and profile. */
	[[nodiscard]] MultiwayFutureBucketArtifact build_multiway_future_bucket_artifact(
		const MultiwayModelIdentity& identity,
		const std::vector<MultiwayBucketBoardRequest>& boards,
		const MultiwayFutureBucketProfile& profile = {});
	/** @brief Serializes an artifact to versioned bytes. */
	[[nodiscard]] std::vector<std::uint8_t> serialize_multiway_future_bucket_artifact(
		const MultiwayFutureBucketArtifact& artifact);
	/** @brief Deserializes an artifact from bytes. */
	[[nodiscard]] MultiwayFutureBucketArtifact deserialize_multiway_future_bucket_artifact(
		const std::vector<std::uint8_t>& bytes);
	/** @brief Atomically writes a complete future-bucket artifact. */
	void save_multiway_future_bucket_artifact_atomic(
		const std::filesystem::path& path,
		const MultiwayFutureBucketArtifact& artifact);
	/** @brief Loads an artifact and checks its model identity. */
	[[nodiscard]] MultiwayFutureBucketArtifact load_multiway_future_bucket_artifact(
		const std::filesystem::path& path,
		const MultiwayModelIdentity& expected_identity);

} // namespace texas::solver::multiway
