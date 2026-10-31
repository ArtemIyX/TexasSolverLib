#pragma once

/** @file
 * @brief Versioned configuration values for offline multiway blueprints.
 */

#include "games/multiway_rake_types.hpp"
#include "solver/multiway/continuation/multiway_continuation_policy_kind.hpp"

#include <cstdint>

namespace texas::solver::multiway
{
	/** @brief Immutable rules, abstraction sizes and semantic versions for one blueprint.
	 * Chip values are exact integers. Stack bands and action templates have their
	 * own version identities when introduced.
	 */
	struct MultiwayBlueprintConfig
	{
		std::uint8_t player_count = 6; ///< Seats represented by the blueprint.
		int initial_stack_chips = 10'000; ///< Initial stack in chips.
		int small_blind_chips = 50; ///< Small blind in chips.
		int big_blind_chips = 100; ///< Big blind in chips.
		int ante_chips = 0; ///< Per-seat ante in chips.
		games::multiway::MultiwayRakePolicy rake_policy = games::multiway::MultiwayRakePolicy::explicit_zero(); ///< Settlement rake policy.
		std::uint32_t flop_bucket_count = 96; ///< Flop abstraction bucket count.
		std::uint32_t turn_bucket_count = 128; ///< Turn abstraction bucket count.
		std::uint32_t river_bucket_count = 192; ///< River abstraction bucket count.
		std::uint64_t action_abstraction_version = 1; ///< Action abstraction semantic version.
		std::uint64_t bucket_model_version = 1; ///< Bucket model semantic version.
		std::uint64_t terminal_model_version = 1; ///< Terminal model semantic version.
		std::uint64_t rules_profile_version = 1; ///< Rules-profile semantic version.
		std::uint64_t resolver_schema_version = 2; ///< Resolver artifact schema version.
		std::uint64_t code_schema_version = 1; ///< Code-produced artifact schema version.
		std::uint64_t range_semantics_version = 1; ///< Range semantics version.
		std::uint64_t future_bucket_model_version = 1; ///< Future-bucket model version.
		std::uint64_t off_tree_policy_version = 1; ///< Off-tree policy version.
		std::uint64_t continuation_policy_version = 1; ///< Continuation policy version.
		MultiwayContinuationPolicyKind continuation_policy = MultiwayContinuationPolicyKind::Blueprint; ///< Continuation policy.
		std::uint64_t runtime_search_schema_version = 1; ///< Runtime search schema version.

		/** @brief Checks supported player, chip, bucket and version settings. */
		void validate() const;
	};
}
