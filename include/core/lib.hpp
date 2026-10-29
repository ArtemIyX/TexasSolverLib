#pragma once

/** @file
 * @brief Provides a narrow compatibility facade over selected TexasSolver entrypoints.
 */

// Narrow compatibility facade. Internal solver, storage, artifact, and
// scheduler types must be included from their owning subsystem headers.
#include "games/hunl_solver.hpp"
#include "preflop/preflop.hpp"
#include "solver/generic/exploit.hpp"
#include "solver/hunl/sampled/hunl_sampled_solver.hpp"
#include "solver/generic/solver.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

/** @brief Compatibility aliases and forwarding helpers for selected public solver APIs. */
namespace texas::core::lib
{

	using ::texas::ExploitOutput;
	using ::texas::HUNLBackendSelection;
	using ::texas::HUNLConfig;
	using ::texas::HUNLLeafEvaluator;
	using ::texas::HUNLSampledSolver;
	using ::texas::HUNLSampledSolverConfig;
	using ::texas::HUNLSampledSolveRequest;
	using ::texas::HUNLSampledSolveResult;
	using ::texas::HUNLSolveOutput;
	using ::texas::HUNLStructuredRootRequest;
	using ::texas::PreflopEquityTable;
	using ::texas::PreflopSolveOutput;
	using ::texas::SolveOutput;

	/** @brief Validates a structured root and runs sampled heads-up postflop batches.
	 * @param root Structured initial state and action-tree description to solve.
	 * @param config Sampled solver configuration.
	 * @param batches Number of batches requested from the solver.
	 * @param leaf_evaluator Optional borrowed evaluator; it must outlive this call.
	 * @return The sampled solve result returned by run_batches().
	 */
	inline HUNLSampledSolveResult solve_hunl_postflop_sampled(
		const HUNLStructuredRootRequest& root,
		HUNLSampledSolverConfig config = {},
		std::uint32_t batches = 1U,
		const HUNLLeafEvaluator* leaf_evaluator = nullptr)
	{
		root.validate();
		HUNLSampledSolver solver(config);
		HUNLSampledSolveRequest request;
		request.structured_root = root;
		request.leaf_evaluator = leaf_evaluator;
		return solver.run_batches(request, batches);
	}

	/** @brief Solves the two-player Kuhn game through the generic solver entrypoint.
	 * @param iterations Number of training iterations.
	 * @param alpha DCFR alpha parameter forwarded to the solver.
	 * @param beta DCFR beta parameter forwarded to the solver.
	 * @param gamma DCFR gamma parameter forwarded to the solver.
	 * @param workers Requested worker count.
	 * @param frontier_multiplier Multiplier forwarded for parallel frontier sizing.
	 * @return Solver output containing strategy, values and timing data.
	 */
	inline SolveOutput solve_kuhn(
		std::uint32_t iterations,
		double alpha,
		double beta,
		double gamma,
		std::size_t workers = 1U,
		std::size_t frontier_multiplier = 8U)
	{
		return ::texas::solve_kuhn(iterations, alpha, beta, gamma, workers, frontier_multiplier);
	}

	/** @brief Solves the two-player Leduc game through the generic solver entrypoint.
	 * @param iterations Number of training iterations.
	 * @param alpha DCFR alpha parameter forwarded to the solver.
	 * @param beta DCFR beta parameter forwarded to the solver.
	 * @param gamma DCFR gamma parameter forwarded to the solver.
	 * @param workers Requested worker count.
	 * @param frontier_multiplier Multiplier forwarded for parallel frontier sizing.
	 * @return Solver output containing strategy, values and timing data.
	 */
	inline SolveOutput solve_leduc(
		std::uint32_t iterations,
		double alpha,
		double beta,
		double gamma,
		std::size_t workers = 1U,
		std::size_t frontier_multiplier = 8U)
	{
		return ::texas::solve_leduc(iterations, alpha, beta, gamma, workers, frontier_multiplier);
	}

	/** @brief Solves a heads-up postflop game using the selected backend.
	 * @param config Game, board, stacks, ranges and action-sizing configuration.
	 * @param iterations Number of training iterations.
	 * @param alpha DCFR alpha parameter forwarded to the solver.
	 * @param beta DCFR beta parameter forwarded to the solver.
	 * @param gamma DCFR gamma parameter forwarded to the solver.
	 * @param workers Requested worker count.
	 * @param frontier_multiplier Multiplier forwarded for parallel frontier sizing.
	 * @param force_parallel Whether to require the parallel solver path.
	 * @param backend Backend selection passed to the HUNL solver.
	 * @return Solver output produced by the HUNL postflop entrypoint.
	 */
	inline HUNLSolveOutput solve_hunl_postflop(
		const HUNLConfig& config,
		std::uint32_t iterations,
		double alpha,
		double beta,
		double gamma,
		std::size_t workers = 1U,
		std::size_t frontier_multiplier = 8U,
		bool force_parallel = false,
		HUNLBackendSelection backend = HUNLBackendSelection::Auto)
	{
		return ::texas::solve_hunl_postflop(
			config, iterations, alpha, beta, gamma, workers, frontier_multiplier, force_parallel, backend);
	}

	/** @brief Solves a heads-up preflop game using the configured game model.
	 * @param config Heads-up game configuration.
	 * @param iterations Number of training iterations.
	 * @param alpha DCFR alpha parameter forwarded to the solver.
	 * @param beta DCFR beta parameter forwarded to the solver.
	 * @param gamma DCFR gamma parameter forwarded to the solver.
	 * @return Preflop output produced by the canonical solver entrypoint.
	 */
	inline PreflopSolveOutput solve_hunl_preflop(
		const HUNLConfig& config,
		std::uint32_t iterations,
		double alpha,
		double beta,
		double gamma)
	{
		return ::texas::solve_hunl_preflop(config, iterations, alpha, beta, gamma);
	}

	/** @brief Computes exploitability and game value for a supplied strategy.
	 * @param config Heads-up game configuration defining the evaluated game.
	 * @param strategy Mapping from information-set keys to action probabilities.
	 * @return Exploitability and value from the canonical evaluation entrypoint.
	 */
	inline ExploitOutput compute_exploitability(
		const HUNLConfig& config,
		const std::unordered_map<std::string, std::vector<double>>& strategy)
	{
		return ::texas::compute_exploitability_and_value(config, strategy);
	}

	/** @brief Evaluates a strategy in a game restricted to supplied private hands.
	 * @param config Heads-up game configuration defining the evaluated game.
	 * @param strategy Mapping from information-set keys to action probabilities.
	 * @param p0_holes Private-card pairs available to player zero.
	 * @param p1_holes Private-card pairs available to player one.
	 * @return Restricted-game value from the canonical evaluation entrypoint.
	 */
	inline double compute_restricted_game_value(
		const HUNLConfig& config,
		const std::unordered_map<std::string, std::vector<double>>& strategy,
		const std::vector<std::array<std::uint8_t, 2>>& p0_holes,
		const std::vector<std::array<std::uint8_t, 2>>& p1_holes)
	{
		return ::texas::compute_restricted_game_value(config, strategy, p0_holes, p1_holes);
	}

} // namespace texas::core::lib
