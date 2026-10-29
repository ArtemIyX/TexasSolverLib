#pragma once

/** @file
 * Fixed continuation-policy identifiers and validation helpers.
 */

#include <array>
#include <cstdint>

namespace texas::solver::multiway
{

	/** Selects the fixed policy used to choose actions at a continuation leaf. */
	enum class MultiwayContinuationPolicyKind : std::uint8_t
	{
		Blueprint,
		FoldBiased,
		CallBiased,
		RaiseBiased,
	};

	/** All policy kinds supported by the fixed continuation kernel. */
	inline constexpr std::array<MultiwayContinuationPolicyKind, 4>
		MULTIWAY_FIXED_CONTINUATION_POLICIES = {
			MultiwayContinuationPolicyKind::Blueprint,
			MultiwayContinuationPolicyKind::FoldBiased,
			MultiwayContinuationPolicyKind::CallBiased,
			MultiwayContinuationPolicyKind::RaiseBiased,
		};

	/** Returns whether policy is one of the fixed continuation policy kinds. */
	[[nodiscard]] constexpr bool is_valid_multiway_continuation_policy(
		MultiwayContinuationPolicyKind policy) noexcept
	{
		return policy == MultiwayContinuationPolicyKind::Blueprint || policy == MultiwayContinuationPolicyKind::FoldBiased || policy == MultiwayContinuationPolicyKind::CallBiased || policy == MultiwayContinuationPolicyKind::RaiseBiased;
	}

} // namespace texas::solver::multiway
