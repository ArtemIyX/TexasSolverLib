#pragma once

/** @file
 * @brief Stable fingerprints for multiway rules, models and policy semantics.
 */

#include "solver/multiway/blueprint/multiway_blueprint_config.hpp"
#include "solver/multiway/abstraction/multiway_model_identity_types.hpp"

#include <cstdint>

namespace texas::solver::multiway
{

	/** @brief Invokes function for each component except combined_hash. */
	template <typename Function>
	void visit_multiway_model_identity_components(
		MultiwayModelIdentity& identity,
		Function&& function)
	{
		function(identity.rules_hash);
		function(identity.rules_schema_hash);
		function(identity.action_abstraction_hash);
		function(identity.bucket_model_hash);
		function(identity.terminal_model_hash);
		function(identity.resolver_schema_hash);
		function(identity.code_schema_hash);
		function(identity.range_semantics_hash);
		function(identity.future_bucket_model_hash);
		function(identity.off_tree_policy_hash);
		function(identity.continuation_policy_hash);
		function(identity.runtime_search_schema_hash);
	}

	/** @brief Const overload of component visitation. */
	template <typename Function>
	void visit_multiway_model_identity_components(
		const MultiwayModelIdentity& identity,
		Function&& function)
	{
		function(identity.rules_hash);
		function(identity.rules_schema_hash);
		function(identity.action_abstraction_hash);
		function(identity.bucket_model_hash);
		function(identity.terminal_model_hash);
		function(identity.resolver_schema_hash);
		function(identity.code_schema_hash);
		function(identity.range_semantics_hash);
		function(identity.future_bucket_model_hash);
		function(identity.off_tree_policy_hash);
		function(identity.continuation_policy_hash);
		function(identity.runtime_search_schema_hash);
	}

	/** @brief Invokes function for each component and combined_hash. */
	template <typename Function>
	void visit_multiway_model_identity_fields(MultiwayModelIdentity& identity, Function&& function)
	{
		visit_multiway_model_identity_components(identity, function);
		function(identity.combined_hash);
	}

	/** @brief Const overload of full identity-field visitation. */
	template <typename Function>
	void visit_multiway_model_identity_fields(
		const MultiwayModelIdentity& identity,
		Function&& function)
	{
		visit_multiway_model_identity_components(identity, function);
		function(identity.combined_hash);
	}

	/** @brief Builds stable identity fingerprints from a blueprint configuration. */
	[[nodiscard]] MultiwayModelIdentity make_multiway_model_identity(
		const MultiwayBlueprintConfig& config);

	// Policy lookup depends on model semantics, not the training runtime profile.
	/** @brief Checks semantic compatibility for policy lookup across identities. */
	[[nodiscard]] bool multiway_policy_identity_compatible(
		const MultiwayModelIdentity& actual,
		const MultiwayModelIdentity& expected) noexcept;

} // namespace texas::solver::multiway
