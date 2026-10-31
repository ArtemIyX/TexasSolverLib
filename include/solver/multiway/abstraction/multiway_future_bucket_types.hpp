#pragma once

/** @file
 * @brief Lightweight future-bucket feature values.
 */

#include <array>
#include <cstdint>

#include "core/street_types.hpp"

namespace texas::solver::multiway
{
	// Stable offline-only features contain no opponent private cards or runtime policy state.
	/** @brief Versioned feature vector for offline future-state clustering. */
	struct MultiwayFutureBucketFeatures
	{
		std::array<double, 10U> values{};
		std::uint64_t feature_version = 1U;
	};

	/** @brief Feature, clustering and bucket-count settings for an offline model. */
	struct MultiwayFutureBucketProfile
	{
		std::uint64_t feature_version = 1U; ///< Feature extraction semantics version.
		std::uint64_t clustering_version = 1U; ///< Clustering algorithm version.
		std::uint64_t seed = 1U; ///< Deterministic clustering seed.
		std::uint32_t lloyd_iterations = 4U; ///< Number of clustering refinement iterations.
		std::uint32_t flop_bucket_count = 96U; ///< Flop bucket count.
		std::uint32_t turn_bucket_count = 128U; ///< Turn bucket count.
		std::uint32_t river_bucket_count = 192U; ///< River bucket count.

		/** @brief Validates profile versions, iterations and bucket counts. */
		void validate() const;
		/** @brief Returns the configured bucket count for a street. */
		[[nodiscard]] std::uint32_t bucket_count(core::Street street) const;
	};
}
