#pragma once

/** @file
 * @brief Declares blocker-aware class-169 ranges, terminal data and vector DCFR solving.
 */

#include "core/legacy_namespace_compat.hpp"

#include "core/types.hpp"
#include "games/hunl.hpp"
#include "preflop/preflop_equity.hpp"
#include "solver/generic/dcfr_vector.hpp"
#include "solver/generic/solver.hpp"

#include <stdexcept>
#include <unordered_map>

namespace texas::preflop
{

	/** @brief Enumerated concrete hole-card combinations grouped by canonical class. */
	struct Class169Combos
	{
		std::array<std::vector<std::array<std::uint8_t, 2>>, PREFLOP_NUM_CLASSES> combos; ///< Valid concrete hands by class index.

		/** @brief Enumerates all concrete two-card combinations for each class. */
		static Class169Combos build();
	};

	/** @brief Maps a concrete non-overlapping deal to one of three suit-equivalence variants. */
	std::size_t classify_suit_variant(const std::array<std::uint8_t, 2>& hero, const std::array<std::uint8_t, 2>& villain);
	/** @brief Computes blocker-compatible mass for each player's class ranges. */
	std::array<std::vector<double>, 2> build_class169_blocker_mass(const Class169Combos& combos);

	/** @brief Cached class-vector payoff data for one leaf. */
	struct Class169LeafEntry
	{
		/** @brief Interpretation of this leaf's payoff data. */
		enum class Kind
		{
			NonTerminal, ///< No terminal payoff is cached.
			Fold, ///< Payoff is fixed by contributions and folding player.
			Equity, ///< Payoff depends on precomputed class-pair equity.
		} kind = Kind::NonTerminal;

		std::array<double, 2> payoff = { 0.0, 0.0 }; ///< Constant payoff by player for fixed-payoff leaves.
		std::array<std::vector<double>, 2> payoff_table = { std::vector<double>{}, std::vector<double>{} }; ///< Payoff vectors by player for equity leaves.
	};

	/** @brief Betting tree specialized for class-169 range solving. */
	struct PreflopBettingTree
	{
		/** @brief Category of a node in the preflop betting tree. */
		enum class NodeKind
		{
			Fold, ///< Fold terminal node.
			EquityLeaf, ///< Showdown terminal node using equity values.
			Decision, ///< Player decision node.
		};

		/** @brief One decision or terminal node and its child/action data. */
		struct Node
		{
			NodeKind kind = NodeKind::Fold; ///< Node category.
			std::uint8_t player = 0; ///< Acting player at a decision node.
			std::vector<std::size_t> children; ///< Child node indices aligned with actions.
			std::vector<std::string> actions; ///< Text labels aligned with children.
			std::string key_suffix; ///< Betting-history suffix used in information-set keys.
			std::array<int, 2> contributions = { 0, 0 }; ///< Contributions at this node.
			std::array<int, 2> initial_contributions = { 0, 0 }; ///< Contributions at the configured root.
			int big_blind = 100; ///< Big-blind size used for payoff normalization.
			int initial_pot = 0; ///< Pot at the configured root.
			std::size_t folded_player = 0; ///< Player who folded at a fold terminal.
		};

		std::vector<Node> nodes; ///< Nodes indexed by tree-local indices.

		/** @brief Builds the class-169 preflop tree from a validated HUNL configuration. */
		static PreflopBettingTree build(const HUNLConfig& config);
	};

	/** @brief Cached terminal payoffs and shared blocker masses for one tree and equity table. */
	struct Class169TerminalCache
	{
		std::vector<Class169LeafEntry> leaves; ///< Leaf data indexed by tree node.
		std::array<std::vector<double>, 2> shared_blocker_mass; ///< Reach mass by class for each player.

		/** @brief Builds terminal data and blocker masses for the supplied inputs. */
		static Class169TerminalCache build(
			const PreflopBettingTree& tree,
			const Class169Combos& combos,
			const PreflopEquityTable& table);
	};

	/** @brief Computes both players' class-vector payoff for an equity terminal. */
	std::array<std::vector<double>, 2> build_class169_leaf_payoff(
		const std::array<int, 2>& contributions,
		int big_blind,
		int initial_pot,
		const std::array<int, 2>& initial_contributions,
		const PreflopEquityTable& equity_table,
		const Class169Combos& class_combos);

	/** @brief Average strategy and execution metadata for a class-169 RvR solve. */
	struct Class169RvrOutput
	{
		std::unordered_map<std::string, std::vector<double>> average_strategy; ///< Probabilities by information-set key.
		std::uint32_t decision_node_count = 0; ///< Number of decision nodes traversed.
		std::uint32_t strategy_entry_count = 0; ///< Number of strategy entries returned.
		std::uint32_t iterations = 0; ///< Completed iterations.
		std::array<std::size_t, 2> hand_count_per_player = { 0, 0 }; ///< Active concrete hands by player.
		double wallclock_seconds = 0.0; ///< Total elapsed time in seconds.
	};

	/** @brief Solves a preflop configuration using the vector DCFR implementation. */
	VectorSolveOutput solve_hunl_vector_dcfr(
		const HUNLConfig& config,
		std::uint32_t iterations,
		double alpha,
		double beta,
		double gamma);

	/** @brief Vectorized DCFR solver over class-169 hand ranges. */
	class Class169VectorDCFR
	{
	public:
		/** @brief Creates a solver for a fixed hand count and DCFR exponents. */
		Class169VectorDCFR(std::size_t hand_count, double alpha, double beta, double gamma);

		/** @brief Runs the requested iterations over a betting tree and cached terminal data. */
		void solve(
			const PreflopBettingTree& tree,
			const Class169TerminalCache& cache,
			std::uint32_t iterations,
			const std::vector<double>& root_reach_p0,
			const std::vector<double>& root_reach_p1);

		/** @brief Exports the average strategy indexed by information-set key. */
		std::unordered_map<std::string, std::vector<double>> average_strategy() const;
		/** @brief Returns the number of completed solver iterations. */
		std::uint32_t iteration() const;

	private:
		std::size_t hand_count_ = 0;
		double alpha_ = 1.5;
		double beta_ = 0.0;
		double gamma_ = 2.0;
		std::uint32_t iteration_ = 0;
		std::vector<std::optional<VectorInfosetData>> infosets_;
		std::vector<std::string> infoset_key_suffixes_;

		static void compute_strategy(const VectorInfosetData& info, std::vector<double>& out);
		static void compute_avg_strategy(const VectorInfosetData& info, std::vector<double>& out);
		static void discount(VectorInfosetData& info, std::uint32_t t, double alpha, double beta, double gamma);
		std::vector<double> traverse(
			const PreflopBettingTree& tree,
			const Class169TerminalCache& cache,
			std::size_t node_idx,
			std::size_t update_player,
			const std::vector<double>& reach_p,
			const std::vector<double>& reach_opp);
	};

	/** @brief Solves preflop range-vs-range using class-169 vector DCFR.
	 * @param config Preflop HUNL configuration.
	 * @param table Precomputed concrete-hand equity table.
	 * @param root_reach_p0 Root range weights for player zero.
	 * @param root_reach_p1 Root range weights for player one.
	 * @param iterations Number of solver iterations.
	 * @param alpha DCFR positive-regret discount exponent.
	 * @param beta DCFR negative-regret discount exponent.
	 * @param gamma DCFR average-strategy weighting exponent.
	 * @return Average strategy and solve metadata.
	 */
	Class169RvrOutput solve_hunl_preflop_rvr_class169(
		const HUNLConfig& config,
		const PreflopEquityTable& table,
		std::vector<double> root_reach_p0,
		std::vector<double> root_reach_p1,
		std::uint32_t iterations,
		double alpha,
		double beta,
		double gamma);

} // namespace texas::preflop
