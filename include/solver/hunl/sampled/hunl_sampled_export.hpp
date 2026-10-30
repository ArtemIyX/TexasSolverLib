#pragma once

/** @file
 * @brief Converts sampled storage rows into root strategy outputs.
 */

#include "solver/hunl/sampled/hunl_sampled_storage.hpp"
#include "games/hunl.hpp"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace texas::solver::hunl
{

	/** @brief One exported root action with its probability and menu metadata. */
	struct HUNLSampledActionProbability
	{
		std::uint32_t action_index = 0;	  ///< Position in the root action row.
		ActionId action_id = ACTION_FOLD; ///< HUNL action identifier.
		int target_contribution = 0;	  ///< Target contribution for contribution-based actions.
		std::uint64_t action_menu_id = 0; ///< Identifier for the action menu used to produce the entry.
		double probability = 0.0;		  ///< Average action probability.
	};

	/** @brief Ordered action-probability entries for a sampled root strategy. */
	struct HUNLSampledRootStrategy
	{
		std::vector<HUNLSampledActionProbability> actions; ///< Root actions in menu order.
	};

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
