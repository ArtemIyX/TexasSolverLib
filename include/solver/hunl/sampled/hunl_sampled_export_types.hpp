#pragma once

/** @file
 * @brief Value types for sampled HUNL root strategy exports.
 */

#include "games/hunl_action_types.hpp"

#include <cstdint>
#include <vector>

namespace texas::solver::hunl
{
	/** @brief One exported root action with its probability and menu metadata. */
	struct HUNLSampledActionProbability
	{
		std::uint32_t action_index = 0;						 ///< Position in the root action row.
		core::ActionId action_id = games::hunl::ACTION_FOLD; ///< HUNL action identifier.
		int target_contribution = 0;						 ///< Target contribution for contribution-based actions.
		std::uint64_t action_menu_id = 0;					 ///< Action menu identifier for this entry.
		double probability = 0.0;							 ///< Average action probability.
	};

	/** @brief Ordered action-probability entries for a sampled root strategy. */
	struct HUNLSampledRootStrategy
	{
		std::vector<HUNLSampledActionProbability> actions; ///< Root actions in menu order.
	};
} // namespace texas::solver::hunl
