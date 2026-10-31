#pragma once

/** @file
 * @brief Lightweight future-bucket feature values.
 */

#include <array>
#include <cstdint>

namespace texas::solver::multiway
{
	// Stable offline-only features contain no opponent private cards or runtime policy state.
	/** @brief Versioned feature vector for offline future-state clustering. */
	struct MultiwayFutureBucketFeatures
	{
		std::array<double, 10U> values{};
		std::uint64_t feature_version = 1U;
	};
}
