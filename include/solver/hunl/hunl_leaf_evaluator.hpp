#pragma once

/** @file
 * @brief Declares the batch callback contract for depth-limited HUNL leaf values.
 */

#include "games/hunl.hpp"

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace texas::solver::hunl
{

	/** @brief Unit used by a leaf evaluator's returned values. */
	enum class HUNLLeafValueUnits : std::uint8_t
	{
		Chips, ///< Raw chip values.
		BigBlinds, ///< Values measured in big blinds.
		PotFraction, ///< Values expressed as a fraction of pot.
		NormalizedStackFraction ///< Values normalized by stack size.
	};
	/** @brief Information scope represented by a batch leaf value. */
	enum class HUNLLeafEvaluationScope : std::uint8_t
	{
		DealConditional ///< Value is conditional on the exact private deal.
	};

	// Boundary contract for a future batched exact/value-network leaf backend.
	// A production solver must use this contract for every depth cutoff or reject
	// the cutoff request; it must never substitute a backend-specific heuristic.
	/** @brief One public state and exact private deal submitted to a leaf backend. */
	struct HUNLLeafEvaluationRequest
	{
		HUNLState public_state; ///< Public state; its hole_cards field is intentionally unset.
		// Exact sampled private deal. public_state deliberately remains private-card free.
		std::array<std::array<std::uint8_t, 2>, 2> private_hole_cards = {}; ///< Exact private cards by player.
		std::array<std::vector<double>, 2> bucket_reach; ///< Reach mass by player and bucket.
		HUNLLeafEvaluationScope scope = HUNLLeafEvaluationScope::DealConditional; ///< Requested value scope.
		HUNLLeafValueUnits units = HUNLLeafValueUnits::Chips; ///< Requested result units.
		// A timed solve supplies its cooperative cancellation deadline. Backends
		// must return promptly once it is reached.
		std::optional<std::chrono::steady_clock::time_point> deadline = std::nullopt; ///< Optional cooperative cancellation deadline.
		std::string abstraction_version; ///< Expected abstraction version label.
		std::string model_version; ///< Expected value-model version label.
	};

	/** @brief One evaluated leaf result and the metadata needed to validate it. */
	struct HUNLLeafEvaluationResult
	{
		std::array<double, 2> values = { 0.0, 0.0 }; ///< Utility by player.
		HUNLLeafValueUnits units = HUNLLeafValueUnits::Chips; ///< Units used by values.
		HUNLLeafEvaluationScope scope = HUNLLeafEvaluationScope::DealConditional; ///< Scope represented by values.
		// A backend must acknowledge every submitted request. This lets callers
		// reject a successful callback that left part of the output batch unset.
		bool populated = false; ///< Whether the callback wrote this result.
		std::string abstraction_version; ///< Abstraction version used to compute the result.
		std::string model_version; ///< Model version used to compute the result.
	};

	// A non-owning, non-virtual batch callback.  Callers submit requests in
	// deterministic traversal order and must write one same-unit result per
	// request. Timed callbacks must observe each request deadline. Returning false
	// rejects the solve at its last completed batch; there is no heuristic fallback
	// for a failed depth-limited value lookup.
	/** @brief Non-owning batch evaluator; false rejects the solve without fallback values. */
	using HUNLLeafEvaluateBatchFn = bool (*)(
		void* context,
		const HUNLLeafEvaluationRequest* requests,
		HUNLLeafEvaluationResult* results,
		std::size_t count);

	/** @brief Context and function pointer for batch leaf evaluation. */
	struct HUNLLeafEvaluator
	{
		void* context = nullptr; ///< Opaque data passed to evaluate_batch.
		HUNLLeafEvaluateBatchFn evaluate_batch = nullptr; ///< Callback that fills one result per request.

		/** @brief Reports whether a batch callback is installed. */
		[[nodiscard]] bool valid() const noexcept
		{
			return evaluate_batch != nullptr;
		}
	};

} // namespace texas::solver::hunl
