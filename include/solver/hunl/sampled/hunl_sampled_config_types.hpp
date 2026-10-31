#pragma once

/** @file
 * @brief Sampled HUNL solver configuration values and policies.
 */

#include "solver/hunl/flat/hunl_flat_types.hpp"

#include <cstddef>
#include <cstdint>

namespace texas::solver::hunl
{
	/** @brief Traversal policy used by sampled flat MCCFR. */
	enum class HUNLFlatSamplingMode : std::uint8_t
	{
		Exact = 0,			 ///< Expand all chance and decision branches.
		PublicChance = 1,	 ///< Sample public chance outcomes.
		External = 2,		 ///< Sample opponent actions and expand traverser actions.
		AverageStrategy = 3, ///< Sample actions according to the average strategy.
	};

	/** @brief Baseline policy used for sampled estimator variance reduction. */
	enum class HUNLFlatBaselineMode : std::uint8_t
	{
		None = 0,		   ///< Do not use a baseline.
		MovingAverage = 1, ///< Use observed moving-average action values.
	};

	/** @brief Worker, precision, RNG and memory limits for a sampled solver. */
	struct HUNLSampledSolverConfig
	{
		std::uint64_t seed = 1;													  ///< Base deterministic sampling seed.
		std::uint32_t minibatch_size = 64;										  ///< Trajectories grouped into one batch.
		HUNLFlatStoragePrecision precision = HUNLFlatStoragePrecision::Float32;	  ///< Sparse table storage precision.
		HUNLFlatValueLayout layout = HUNLFlatValueLayout::InfosetActionHand;	  ///< Action and bucket memory order.
		bool enable_memory_guardrails = true;									  ///< Enforce configured memory warning and failure limits.
		bool adaptive_memory_fallback = true;									  ///< Allow selection of a lower-memory mode when available.
		std::size_t workers = 1;												  ///< Worker count.
		std::uint64_t memory_warning_bytes = 48ULL * 1024ULL * 1024ULL * 1024ULL; ///< Warning threshold in bytes.
		std::uint64_t memory_fail_bytes = 60ULL * 1024ULL * 1024ULL * 1024ULL;	  ///< Rejection threshold in bytes.
	};

	/** @brief Traversal, sampling and regret settings for flat MCCFR. */
	struct HUNLFlatMCCFRConfig
	{
		HUNLFlatSamplingMode mode = HUNLFlatSamplingMode::External;		 ///< Traversal sampling mode.
		std::uint64_t seed = 1;											 ///< Base deterministic sampling seed.
		std::uint32_t traversals_per_iteration = 1024;					 ///< Trajectories per player update iteration.
		std::uint32_t batch_size = 64;									 ///< Trajectories processed in each worker batch.
		double as_epsilon = 0.05;										 ///< Exploration floor for average-strategy sampling.
		double as_tau = 1000.0;											 ///< Temperature schedule parameter for average-strategy sampling.
		double as_beta = 1e6;											 ///< Scaling parameter for average-strategy sampling.
		bool update_both_players = true;								 ///< Update both players during each iteration.
		bool use_discounting = false;									 ///< Apply DCFR discounts to regrets and strategy sums.
		double dcfr_alpha = 1.5;										 ///< Positive-regret discount exponent.
		double dcfr_beta = 0.0;											 ///< Negative-regret discount exponent.
		double dcfr_gamma = 2.0;										 ///< Strategy-sum discount exponent.
		bool use_sparse_storage = false;								 ///< Allocate rows only when touched.
		bool keep_dense_validation_backend = false;						 ///< Retain dense backend for validation comparisons.
		HUNLFlatBaselineMode baseline_mode = HUNLFlatBaselineMode::None; ///< Variance-reduction baseline mode.
		bool use_iterative_external_dense_traversal = false;			 ///< Use iterative traversal for supported dense configurations.
	};

	/** @brief Non-throwing sampled config validation result. */
	struct HUNLSampledConfigValidation
	{
		bool ok = true;			  ///< Whether validation succeeded.
		const char* message = ""; ///< Static validation message when ok is false.
	};
} // namespace texas::solver::hunl
