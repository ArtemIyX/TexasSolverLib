#pragma once

/** @file
 * Shared normalization kernel for resolver action policies.
 */

#include "solver/multiway/resolver/multiway_resolver.hpp"

namespace texas::solver::multiway
{

	// Scalar reference kernel shared by runtime and legacy resolver policies.
	// It rejects empty, negative, non-finite, and zero-mass policies.
	/** Normalizes a resolver policy in place, returning false for invalid mass. */
	[[nodiscard]] bool normalize_multiway_resolver_policy(
		std::vector<MultiwayResolverActionProbability>* policy) noexcept;

} // namespace texas::solver::multiway
