#pragma once

/** @file
 * @brief Parallel generic DCFR solver and its traversal work plan.
 */

#include "core/arena.hpp"
#include "solver/generic/dcfr.hpp"

#include <cstdint>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <vector>

namespace texas::solver::dcfr
{

	/** @brief One subproblem assigned to a worker. */
#pragma pack(push, 8)
	struct ParallelWorkItem
	{
		std::size_t root_node = 0;	///< Root state index represented by this work item.
		std::size_t node_begin = 0; ///< Beginning of the assigned node interval.
		std::size_t node_end = 0;	///< One past the end of the assigned node interval.
	};
#pragma pack(pop)

	/** @brief Worker count and work items selected for one solve. */
#pragma pack(push, 8)
	struct ParallelSolvePlan
	{
		bool enabled = false;				 ///< Whether parallel traversal is enabled.
		std::size_t worker_count = 1;		 ///< Number of workers in the plan.
		std::vector<ParallelWorkItem> items; ///< Work ranges assigned by the planner.
	};
#pragma pack(pop)

	namespace detail
	{

#pragma pack(push, 8)
		struct WorkerInfosetAccumRow
		{
			InfosetId id{};
			double* regret_sum = nullptr;
			double* strategy_sum = nullptr;
		};
#pragma pack(pop)

		class WorkerInfosetAccumTable
		{
		public:
			void reset();
			[[nodiscard]] std::size_t size() const noexcept;
			[[nodiscard]] const std::vector<InfosetId>& active_ids() const noexcept;
			InfosetAccumView ensure(InfosetId id, std::size_t action_count);
			[[nodiscard]] ConstInfosetAccumView view(InfosetId id, std::size_t action_count) const;

		private:
			Arena arena_;
			std::vector<WorkerInfosetAccumRow*> rows_by_id_;
			std::vector<InfosetId> active_ids_;
		};

	} // namespace detail

	/** @brief Accumulators owned by one parallel traversal worker. */
#pragma pack(push, 8)
	struct ParallelWorkerState
	{
		detail::WorkerInfosetAccumTable accum; ///< Worker-local regret and strategy sums.
	};
#pragma pack(pop)

	/** @brief DCFR solver that partitions traversal work among worker threads. */
	template <class G>
	class ParallelDCFRSolver : public DCFRSolverBase
	{
	public:
		/** @brief Creates a parallel solver over config, root and worker settings. */
		explicit ParallelDCFRSolver(
			DCFRConfig config,
			G root = G::initial(),
			std::size_t worker_count = 1,
			std::size_t frontier_multiplier = 8);

		/** @brief Replaces externally fixed strategies by information-set key. */
		void set_locked_strategies(std::unordered_map<InfosetKey, std::vector<Probability>> locked);
		/** @brief Runs parallel iterations and returns the average strategy and metadata. */
		SolveOutput solve(std::uint32_t iterations);

	private:
		using StrategyMap = detail::IndexedStrategyTable;
		using SharedStrategyMap = std::shared_ptr<const StrategyMap>;

		ParallelSolvePlan build_plan() const;
		static void validate_plan(const ParallelSolvePlan& plan);
		static void reset_worker_state(ParallelWorkerState& worker_state);
		void merge_worker_state(
			detail::InfosetAccumTable& canonical,
			ParallelWorkerState worker_state);
		SharedStrategyMap build_strategy_snapshot(
			const detail::InfosetAccumTable& canonical);
		double cfr(
			const G& state,
			PlayerId traversing_player,
			const std::array<double, 2>& reach_probs,
			double chance_reach,
			const StrategyMap& strategy,
			ParallelWorkerState& worker_state);
		std::pair<InfosetId, std::vector<Probability>> resolve_infoset(
			const G& state,
			PlayerId player,
			std::size_t action_count);
		StrategyMap build_average_strategy() const;
		DCFRConfig config_;
		G root_;
		std::size_t worker_count_ = 1;
		std::size_t frontier_multiplier_ = 8;
		std::unordered_map<InfosetKey, std::vector<Probability>> locked_;
		InfosetRegistry registry_;
		detail::IndexedStrategyTable locked_by_id_;
		detail::InfosetAccumTable infosets_;
		mutable std::mutex infoset_mutex_;
	};

} // namespace texas::solver::dcfr
