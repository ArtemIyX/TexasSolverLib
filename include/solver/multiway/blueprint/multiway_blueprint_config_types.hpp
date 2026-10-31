#pragma once

/** @file
 * @brief Versioned configuration values for offline multiway blueprints.
 */

#include "games/multiway_rake_types.hpp"
#include "solver/multiway/continuation/multiway_continuation_policy_kind.hpp"

#include <cstdint>

namespace texas::solver::multiway
{
	/** @brief Rules, abstraction sizes, and semantic versions defining a blueprint. */
	struct MultiwayBlueprintConfig
	{
		std::uint8_t player_count = 6;
		int initial_stack_chips = 10'000;
		int small_blind_chips = 50;
		int big_blind_chips = 100;
		int ante_chips = 0;
		games::multiway::MultiwayRakePolicy rake_policy = games::multiway::MultiwayRakePolicy::explicit_zero();
		std::uint32_t flop_bucket_count = 96;
		std::uint32_t turn_bucket_count = 128;
		std::uint32_t river_bucket_count = 192;
		std::uint64_t action_abstraction_version = 1;
		std::uint64_t bucket_model_version = 1;
		std::uint64_t terminal_model_version = 1;
		std::uint64_t rules_profile_version = 1;
		std::uint64_t resolver_schema_version = 2;
		std::uint64_t code_schema_version = 1;
		std::uint64_t range_semantics_version = 1;
		std::uint64_t future_bucket_model_version = 1;
		std::uint64_t off_tree_policy_version = 1;
		std::uint64_t continuation_policy_version = 1;
		MultiwayContinuationPolicyKind continuation_policy = MultiwayContinuationPolicyKind::Blueprint;
		std::uint64_t runtime_search_schema_version = 1;

		/** @brief Checks supported player, chip, bucket and version settings. */
		void validate() const;
	};
}
