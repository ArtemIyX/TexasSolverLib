#pragma once

/** @file
 * @brief Shared value types and solver result summaries.
 */

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace texas::core
{
	/** @brief Player index used by game-state interfaces; -1 means chance / no player. */
	using PlayerId = int;
	/** @brief Action identifier used by game-state transitions. */
	using ActionId = int;
	/** @brief Information-set lookup key. */
	using InfosetKey = std::string;
	/** @brief Probability mass for strategy vectors and chance outcomes. */
	using Probability = double;
	/** @brief Utility or expected value in solver output. */
	using Value = double;

	/** @brief Describes one possible chance action and its probability. */
	struct ChanceOutcome
	{
		ActionId action{};           ///< Chance action ID to apply.
		Probability probability{};   ///< Probability assigned to this outcome.
	};

	/** @brief Timing and work counters collected for one solver worker. */
	struct WorkerProfile
	{
		double cfr_seconds = 0.0;         ///< Time spent in CFR work, in seconds.
		std::uint64_t batches_taken = 0;   ///< Number of batches handled by the worker.
		std::uint64_t seeds_processed = 0; ///< Number of traversal seeds handled by the worker.
		std::uint64_t infoset_count = 0;   ///< Number of information sets attributed to the worker.
	};

	/** @brief Optional aggregate timing and work counters for a solve. */
	struct SolveProfile
	{
		bool enabled = false;                   ///< Whether the solver collected profile measurements.
		double snapshot_seconds = 0.0;           ///< Time spent snapshotting solver state, in seconds.
		double merge_seconds = 0.0;              ///< Time spent merging worker state, in seconds.
		double frontier_seconds = 0.0;           ///< Time spent building the traversal frontier, in seconds.
		double batch_build_seconds = 0.0;         ///< Time spent building batches, in seconds.
		double discount_seconds = 0.0;            ///< Time spent applying discount weights, in seconds.
		double strategy_seconds = 0.0;            ///< Time spent updating strategy data, in seconds.
		double reach_seconds = 0.0;               ///< Time spent updating reach data, in seconds.
		double terminal_seconds = 0.0;             ///< Time spent evaluating terminal states, in seconds.
		double backward_seconds = 0.0;             ///< Time spent in backward traversal work, in seconds.
		double regret_seconds = 0.0;               ///< Time spent updating regrets, in seconds.
		double average_strategy_seconds = 0.0;     ///< Time spent updating average strategy, in seconds.
		std::uint64_t frontier_seed_count = 0;     ///< Number of seeds placed in the traversal frontier.
		std::uint64_t batch_count = 0;             ///< Number of batches included in the profile.
		std::vector<WorkerProfile> workers;        ///< Per-worker measurements when available.
	};

	/** @brief Summary returned by solver entrypoints. */
	struct SolveOutput
	{
		std::vector<std::pair<InfosetKey, std::vector<Probability>>> average_strategy; ///< Strategy probabilities grouped by information-set key.
		Value exploitability = 0.0;                                                       ///< Exploitability in game utility units.
		Value game_value = 0.0;                                                           ///< Game value in game utility units.
		std::uint32_t iterations = 0;                                                     ///< Number of iterations completed.
		double traversal_seconds = 0.0;                                                   ///< Traversal time, in seconds.
		double finalize_seconds = 0.0;                                                    ///< Finalization time, in seconds.
		bool used_parallel = false;                                                       ///< Whether the solve used a parallel path.
		SolveProfile profile;                                                             ///< Detailed timing data when profiling was enabled.
	};

} // namespace texas::core
