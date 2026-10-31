#pragma once

/** @file
 * @brief Stable component and combined fingerprints for multiway models.
 */

#include <cstdint>

namespace texas::solver::multiway
{
	// Fingerprints are stable FNV-1a identities, not security hashes.
	/** @brief Combined identity of rules, abstractions, artifacts and runtime schemas. */
	struct MultiwayModelIdentity
	{
		std::uint64_t rules_hash = 0;
		std::uint64_t rules_schema_hash = 0;
		std::uint64_t action_abstraction_hash = 0;
		std::uint64_t bucket_model_hash = 0;
		std::uint64_t terminal_model_hash = 0;
		std::uint64_t resolver_schema_hash = 0;
		std::uint64_t code_schema_hash = 0;
		std::uint64_t range_semantics_hash = 0;
		std::uint64_t future_bucket_model_hash = 0;
		std::uint64_t off_tree_policy_hash = 0;
		std::uint64_t continuation_policy_hash = 0;
		std::uint64_t runtime_search_schema_hash = 0;
		std::uint64_t combined_hash = 0;

		/** @brief Checks component and combined fingerprints for consistency. */
		void validate() const;

		/** @brief Compares every component and the combined fingerprint.
		 * @param other Identity to compare.
		 */
		constexpr bool operator==(const MultiwayModelIdentity& other) const noexcept
		{
			return rules_hash == other.rules_hash && rules_schema_hash == other.rules_schema_hash && action_abstraction_hash == other.action_abstraction_hash && bucket_model_hash == other.bucket_model_hash && terminal_model_hash == other.terminal_model_hash && resolver_schema_hash == other.resolver_schema_hash && code_schema_hash == other.code_schema_hash && range_semantics_hash == other.range_semantics_hash && future_bucket_model_hash == other.future_bucket_model_hash && off_tree_policy_hash == other.off_tree_policy_hash && continuation_policy_hash == other.continuation_policy_hash && runtime_search_schema_hash == other.runtime_search_schema_hash && combined_hash == other.combined_hash;
		}
		/** @brief Reports whether any identity component differs. */
		constexpr bool operator!=(const MultiwayModelIdentity& other) const noexcept
		{
			return !(*this == other);
		}
	};
}
