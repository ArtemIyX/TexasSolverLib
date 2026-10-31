#pragma once

/** @file
 * @brief Vector-valued DCFR state, evaluation context and solve entry points.
 */

#include "core/legacy_namespace_compat.hpp"

#include "solver/generic/exploit.hpp"
#include "games/hunl.hpp"

#include <array>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace texas::solver::dcfr
{

	/** @brief Regret and strategy accumulators for one multi-hand information set. */
#pragma pack(push, 8)
	struct VectorInfosetData
	{
		std::size_t action_count = 0;		  ///< Number of actions at this information set.
		std::size_t hand_count = 0;			  ///< Number of private hands represented.
		std::vector<double> regret;			  ///< Cumulative regret indexed by hand and action.
		std::vector<double> strategy_sum;	  ///< Cumulative strategy weight indexed by hand and action.
		std::uint32_t last_discount_iter = 0; ///< Last iteration at which discounts were applied.

		/** @brief Creates empty accumulators. */
		VectorInfosetData() = default;
		/** @brief Allocates action-by-hand regret and strategy arrays. */
		VectorInfosetData(std::size_t action_count, std::size_t hand_count);
	};
#pragma pack(pop)

	/** @brief Memory and information-set counts for a vector solver. */
#pragma pack(push, 8)
	struct VectorMemoryProfile
	{
		std::uint64_t total_bytes = 0;											///< Estimated or measured total bytes for solver storage.
		std::unordered_map<std::string, std::uint64_t> by_street;				///< Storage bytes grouped by street name.
		std::uint32_t infoset_count = 0;										///< Total information sets.
		std::unordered_map<std::string, std::uint32_t> infoset_count_by_street; ///< Information-set counts by street.
		std::array<std::size_t, 2> hand_count = { 0, 0 };						///< Hand counts by player.
	};
#pragma pack(pop)

	/** @brief Average strategy and run metadata from a vector DCFR solve. */
#pragma pack(push, 8)
	struct VectorSolveOutput
	{
		std::unordered_map<std::string, std::vector<double>> average_strategy; ///< Action probabilities by information-set key.
		std::uint32_t decision_node_count = 0;								   ///< Number of decision nodes processed.
		std::uint32_t strategy_entry_count = 0;								   ///< Number of returned strategy entries.
		std::uint32_t iterations = 0;										   ///< Completed solver iterations.
		std::array<std::size_t, 2> hand_count_per_player = { 0, 0 };		   ///< Active private-hand count by player.
		VectorMemoryProfile memory_profile;									   ///< Storage and information-set measurements.
	};
#pragma pack(pop)

	/** @brief Concrete hand lists and chip scale used by vector terminal evaluation. */
#pragma pack(push, 8)
	struct EvalContext
	{
		std::array<std::size_t, 2> hand_count = { 0, 0 };			  ///< Number of concrete hands for each player.
		std::array<std::vector<std::array<std::uint8_t, 2>>, 2> hole; ///< Concrete cards by player and hand index.
		std::array<std::vector<std::string>, 2> hole_str;			  ///< Formatted cards aligned with hole.
		int big_blind = 0;											  ///< Big-blind chip amount used for utility normalization.

		/** @brief Derives hand lists from the initial state. */
		static EvalContext from_root(const HUNLState& initial);
		/** @brief Derives hand lists using suit-isomorphic representatives. */
		static EvalContext from_suit_iso(const HUNLState& initial);
		/** @brief Creates an evaluation context from explicit hand lists. */
		static EvalContext from_hand_lists(
			std::vector<std::array<std::uint8_t, 2>> p0_holes,
			std::vector<std::array<std::uint8_t, 2>> p1_holes,
			int big_blind);
	};
#pragma pack(pop)

	/** @brief DCFR solver state for vector-valued hand reach and payoff operations. */
#pragma pack(push, 8)
	struct VectorDCFR
	{
		double alpha = 1.5;										///< Positive-regret discount exponent.
		double beta = 0.0;										///< Negative-regret discount exponent.
		double gamma = 2.0;										///< Strategy-sum discount exponent.
		std::uint32_t iteration = 0;							///< Current completed iteration.
		std::vector<std::optional<VectorInfosetData>> infosets; ///< Per-information-set accumulators.
		std::vector<bool> has_chance_template;					///< Whether node chance templates have been recorded.
		std::vector<std::uint8_t> chance_depth;					///< Remaining chance depth by tree node.

		/** @brief Constructs a fresh solver without randomized regret initialization. */
		static VectorDCFR new_solver(
			const BettingTree& tree,
			std::array<std::size_t, 2> hand_count_per_player,
			double alpha,
			double beta,
			double gamma);
		/** @brief Creates a solver with seeded random regret initialization. */
		static VectorDCFR with_init_noise(
			const BettingTree& tree,
			std::array<std::size_t, 2> hand_count_per_player,
			double alpha,
			double beta,
			double gamma,
			double regret_init_noise,
			std::uint64_t rng_seed);
		/** @brief Creates a seeded solver while skipping initialization for masked entries. */
		static VectorDCFR with_init_noise_masked(
			const BettingTree& tree,
			std::array<std::size_t, 2> hand_count_per_player,
			double alpha,
			double beta,
			double gamma,
			double regret_init_noise,
			std::uint64_t rng_seed,
			const std::vector<bool>& skip_mask);

		/** @brief Computes regret-matching probabilities into out. */
		static void compute_strategy(const VectorInfosetData& info, std::vector<double>& out);
		/** @brief Computes normalized cumulative average-strategy probabilities into out. */
		static void compute_avg_strategy(const VectorInfosetData& info, std::vector<double>& out);
		/** @brief Applies DCFR discounts through iteration t. */
		static void discount(VectorInfosetData& info, std::uint32_t t, double alpha, double beta, double gamma);

		/** @brief Evaluates one terminal node for an update player. */
		using TerminalEvaluator = std::function<std::vector<double>(std::size_t node_idx, std::size_t update_player)>;

		/** @brief Traverses one tree node and returns vector-valued counterfactual values. */
		std::vector<double> traverse(
			const BettingTree& tree,
			std::size_t node_idx,
			std::size_t update_player,
			const std::vector<double>& reach_p,
			const std::vector<double>& reach_opp,
			const TerminalEvaluator& terminal_eval);

		/** @brief Runs iterations over the tree with every hand active. */
		void solve(
			const BettingTree& tree,
			std::uint32_t iterations,
			std::size_t hand_count,
			const TerminalEvaluator& terminal_eval);

		/** @brief Runs iterations over the tree, excluding hands selected by skip_mask. */
		void solve(
			const BettingTree& tree,
			std::uint32_t iterations,
			std::size_t hand_count,
			const std::vector<bool>& skip_mask,
			const TerminalEvaluator& terminal_eval);

		/** @brief Solves and exports strategy and memory metadata. */
		VectorSolveOutput solve_to_output(
			const BettingTree& tree,
			std::uint32_t iterations,
			std::size_t hand_count,
			const TerminalEvaluator& terminal_eval);

		/** @brief Solves with a hand skip mask and exports strategy and memory metadata. */
		VectorSolveOutput solve_to_output(
			const BettingTree& tree,
			std::uint32_t iterations,
			std::size_t hand_count,
			const std::vector<bool>& skip_mask,
			const TerminalEvaluator& terminal_eval);

		/** @brief Creates a terminal evaluator for an explicit list of hole-card pairs. */
		static TerminalEvaluator make_terminal_evaluator(
			const BettingTree& tree,
			const std::vector<std::array<std::array<std::uint8_t, 2>, 2>>& hole_pairs);
	};
#pragma pack(pop)

	/** @brief Runs vector DCFR for explicit hole-card pairs. */
	VectorSolveOutput solve_vector_dcfr(
		const BettingTree& tree,
		const std::vector<std::array<std::array<std::uint8_t, 2>, 2>>& hole_pairs,
		std::uint32_t iterations,
		double alpha,
		double beta,
		double gamma);

	/** @brief Runs vector DCFR for explicit hole-card pairs with a skip mask. */
	VectorSolveOutput solve_vector_dcfr(
		const BettingTree& tree,
		const std::vector<std::array<std::array<std::uint8_t, 2>, 2>>& hole_pairs,
		std::uint32_t iterations,
		double alpha,
		double beta,
		double gamma,
		const std::vector<bool>& skip_mask);

} // namespace texas::solver::dcfr
