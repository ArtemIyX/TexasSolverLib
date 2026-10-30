#pragma once

/** @file
 * Non-owning request and callback boundary for multiway leaf evaluation.
 */

#include "core/types.hpp"
#include "games/multiway_state.hpp"
#include "solver/multiway/continuation/multiway_continuation_policy_kind.hpp"
#include "solver/multiway/engine/multiway_solver.hpp"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace texas::solver::multiway
{

	class MultiwaySamplerDealToken;
	class MultiwayTerminalAdapter;

	// A narrow non-owning depth-limit boundary. The caller owns model state and
	// supplies a plain function pointer so traversal does not use std::function
	// or virtual dispatch in its hot path.
	/** Public state and caller-owned views supplied for one leaf evaluation. */
	struct MultiwayLeafEvaluationRequest
	{
		const MultiwayBettingSnapshot* betting = nullptr;
		const std::vector<std::uint8_t>* board = nullptr;
		PlayerId traverser = -1;
		MultiwayPublicStateId public_state{};
		PlayerId continuation_actor = -1;
		std::uint32_t future_bucket = 0;
		std::uint64_t action_abstraction_version = 0;
		std::uint64_t leaf_model_version = 0;
		MultiwayContinuationPolicyKind continuation_policy =
			MultiwayContinuationPolicyKind::Blueprint;
		// The deal remains opaque. Leaf adapters may use the bound terminal
		// adapter to read the sampled cards needed for a private rollout.
		const MultiwaySamplerDealToken* private_deal = nullptr;
		const MultiwayTerminalAdapter* terminal_adapter = nullptr;
		const Probability* player_reaches = nullptr;
		std::size_t player_count = 0;
		// Opaque request-local identities used only to prevent continuation-cache
		// reuse across different ranges/reaches or sampled private deals.
		std::uint64_t range_context_identity = 0;
		std::uint64_t private_context_identity = 0;
	};

	/** Callback signature for evaluating one leaf request. */
	using MultiwayLeafEvaluateFn = Value (*)(
		const MultiwayLeafEvaluationRequest& request,
		const void* context) noexcept;

	/** Lightweight function-pointer evaluator with a non-owning context. */
	struct MultiwayLeafEvaluator
	{
		MultiwayLeafEvaluateFn evaluate = nullptr;
		const void* context = nullptr;

		/** Returns whether an evaluation callback is installed. */
		[[nodiscard]] bool valid() const noexcept { return evaluate != nullptr; }
		/** Invokes the configured callback with its bound context. */
		[[nodiscard]] Value operator()(const MultiwayLeafEvaluationRequest& request) const noexcept
		{
			return evaluate(request, context);
		}
	};

} // namespace texas::solver::multiway
