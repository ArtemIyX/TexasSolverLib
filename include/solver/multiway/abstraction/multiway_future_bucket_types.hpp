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
}
