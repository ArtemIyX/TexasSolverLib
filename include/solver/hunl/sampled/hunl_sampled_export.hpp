#pragma once

/** @file
 * @brief Converts sampled storage rows into root strategy outputs.
 */

#include "solver/hunl/sampled/hunl_sampled_export_types.hpp"
#include "solver/hunl/sampled/hunl_sampled_storage.hpp"
#include "games/hunl.hpp"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace texas::solver::hunl
{
	/** @brief Builds root strategy exports from sampled solver rows. */
	class HUNLSampledStrategyExporter
	{
	public:
		/** @brief Creates a uniform root strategy for the requested action count. */
		[[nodiscard]] static HUNLSampledRootStrategy export_uniform(std::uint8_t action_count);
		/** @brief Exports average probabilities from one sampled storage row and bucket. */
		[[nodiscard]] static HUNLSampledRootStrategy export_average_strategy(
			HUNLSampledConstRowView row,
			std::size_t bucket_index = 0);
		/** @brief Attaches action IDs and target contributions to exported probabilities. */
		static void attach_action_descriptors(
			HUNLSampledRootStrategy& strategy,
			const std::vector<ActionId>& actions,
			const std::vector<int>& target_contributions);
	};
} // namespace texas::solver::hunl
