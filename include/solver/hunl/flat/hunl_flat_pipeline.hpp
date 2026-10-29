#pragma once

/** @file
 * @brief Plans and runs ordered stages of a flat HUNL DCFR iteration.
 */

#include "solver/hunl/flat/hunl_flat_state.hpp"

#include <cstdint>
#include <vector>

namespace texas::solver::hunl
{

	/** @brief Forward declaration of the flat DCFR solver driven by this pipeline. */
	class HUNLFlatDCFR;

	/** @brief Flat solver stage identifier. */
	enum class HUNLFlatPipelineStageId : std::uint8_t
	{
		ForwardProfile = 0, ///< Propagate policy and chance reach.
		AggregateReach = 1, ///< Aggregate hand reach by bucket.
		OpponentReach = 2, ///< Build opponent counterfactual reach.
		ShowdownEquity = 3, ///< Evaluate showdown terminals.
		DepthLimitedEval = 4, ///< Evaluate configured depth-limited leaves.
		BackwardCFV = 5, ///< Propagate counterfactual values backward.
		RegretUpdate = 6, ///< Update cumulative regrets.
	};

	/** @brief Data domain partitioned by a pipeline stage. */
	enum class HUNLFlatPipelineDomain : std::uint8_t
	{
		Infoset = 0, ///< Stage processes information sets.
		Node = 1, ///< Stage processes nodes.
		DepthNode = 2, ///< Stage processes nodes grouped by depth.
	};

	/** @brief Graph and table counts used to size pipeline buffers. */
	struct HUNLFlatPipelineBuffers
	{
		std::uint32_t infoset_count = 0; ///< Number of graph information sets.
		std::uint32_t node_count = 0; ///< Number of graph nodes.
		std::uint32_t bucket_count = 0; ///< Number of table buckets.
		std::uint32_t value_count = 0; ///< Number of table values.
		std::uint32_t terminal_node_count = 0; ///< Number of terminal nodes.
		std::uint32_t showdown_node_count = 0; ///< Number of showdown terminals.
		std::uint32_t depth_limited_node_count = 0; ///< Number of depth-limited leaves.
		bool has_depth_limited_nodes = false; ///< Whether any depth-limited stage work exists.
	};

	/** @brief One pipeline stage and its worker-range assignment. */
	struct HUNLFlatPipelineStagePlan
	{
		HUNLFlatPipelineStageId id = HUNLFlatPipelineStageId::ForwardProfile; ///< Stage identifier.
		HUNLFlatPipelineDomain domain = HUNLFlatPipelineDomain::Infoset; ///< Work-item domain.
		bool optional = false; ///< Whether the stage may be skipped when no work exists.
		std::vector<HUNLFlatRange> worker_ranges; ///< One range by worker.
		std::vector<std::vector<HUNLFlatRange>> worker_depth_ranges; ///< Depth ranges by worker.
		std::vector<std::uint64_t> worker_costs; ///< Estimated work by worker.
	};

	/** @brief Buffer requirements and ordered worker plan for flat stages. */
	class HUNLFlatPipelinePlan
	{
	public:
		/** @brief Builds a stage plan from graph, table and parallel partitions. */
		static HUNLFlatPipelinePlan build(
			const HUNLFlatSolveGraph& graph,
			const HUNLFlatInfosetTable& infoset_table,
			const HUNLFlatParallelPlan& parallel_plan);

		/** @brief Returns buffer counts derived from graph and table. */
		[[nodiscard]] const HUNLFlatPipelineBuffers& buffers() const noexcept;
		/** @brief Returns the ordered stage plans. */
		[[nodiscard]] const std::vector<HUNLFlatPipelineStagePlan>& stages() const noexcept;
		/** @brief Returns the plan for one stage identifier. */
		[[nodiscard]] const HUNLFlatPipelineStagePlan& stage(HUNLFlatPipelineStageId id) const;

	private:
		HUNLFlatPipelineBuffers buffers_;
		std::vector<HUNLFlatPipelineStagePlan> stages_;
	};

	/** @brief Executes the planned stages for one solver iteration. */
	class HUNLFlatPipeline
	{
	public:
		/** @brief Takes ownership of a prepared stage plan. */
		explicit HUNLFlatPipeline(HUNLFlatPipelinePlan plan);

		/** @brief Returns the owned plan. */
		[[nodiscard]] const HUNLFlatPipelinePlan& plan() const noexcept;
		/** @brief Runs one full iteration on solver using the planned stage order. */
		void run_iteration(HUNLFlatDCFR& solver) const;

	private:
		HUNLFlatPipelinePlan plan_;
	};

} // namespace texas::solver::hunl
