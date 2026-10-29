#pragma once

/** @file
 * @brief Flat graph DCFR solver with staged execution and diagnostics.
 */

#include "games/hunl_flat_graph.hpp"
#include "solver/hunl/bucket/hunl_bucket_map.hpp"
#include "solver/hunl/bucket/hunl_bucket_terminal.hpp"
#include "solver/hunl/flat/hunl_flat_expected_value.hpp"
#include "solver/hunl/flat/hunl_flat_pipeline.hpp"
#include "solver/hunl/flat/hunl_flat_state.hpp"

#include <array>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <memory>
#include <mutex>
#include <optional>
#include <thread>
#include <unordered_map>
#include <vector>

namespace texas::solver::hunl
{

#if defined(_MSC_VER)
	#pragma warning(push)
	#pragma warning(disable : 4324)
#endif
	/** @brief Per-worker elapsed time by flat solver stage. */
	struct alignas(HUNL_CACHELINE_BYTES) HUNLFlatStageProfile
	{
		double discount_seconds = 0.0; ///< Time spent discounting accumulated values.
		double strategy_seconds = 0.0; ///< Time spent deriving current strategy.
		double reach_seconds = 0.0; ///< Time spent propagating and aggregating reach.
		double terminal_seconds = 0.0; ///< Time spent evaluating terminal values.
		double backward_seconds = 0.0; ///< Time spent propagating values backward.
		double regret_seconds = 0.0; ///< Time spent updating regrets.
		double average_strategy_seconds = 0.0; ///< Time spent accumulating average strategy.
	};
#if defined(_MSC_VER)
	#pragma warning(pop)
#endif

	/** @brief Stage category used to attribute flat solver timing. */
	enum class HUNLFlatStageKind : std::uint8_t
	{
		Discount = 0, ///< DCFR discount stage.
		Strategy = 1, ///< Current strategy computation stage.
		Reach = 2, ///< Reach propagation and reduction stage.
		Terminal = 3, ///< Terminal utility evaluation stage.
		Backward = 4, ///< Backward value stage.
		Regret = 5, ///< Regret update stage.
		AverageStrategy = 6, ///< Average strategy accumulation stage.
	};

	/** @brief Timing profiles collected for workers during flat execution. */
	struct HUNLFlatSchedulerDiagnostics
	{
		std::vector<HUNLFlatStageProfile> worker_profiles; ///< One accumulated profile per worker.
	};

	static_assert(alignof(HUNLFlatStageProfile) == HUNL_CACHELINE_BYTES,
		"HUNLFlatStageProfile should be cache-line aligned");

	/** @brief DCFR solver over a compact HUNL flat graph. */
	class HUNLFlatDCFR
	{
	public:
		/** @brief Constructs a solver using explicit graph, bucket counts and storage layout. */
		explicit HUNLFlatDCFR(
			HUNLFlatSolveGraph graph,
			std::array<std::size_t, 2> bucket_count_per_player,
			HUNLFlatValueLayout layout = HUNLFlatValueLayout::InfosetActionHand,
			std::size_t workers = 1,
			double alpha = 1.5,
			double beta = 0.0,
			double gamma = 2.0,
			HUNLFlatStoragePrecision precision = HUNLFlatStoragePrecision::Float64);

		/** @brief Constructs a solver using a selected flat solve mode and storage layout. */
		explicit HUNLFlatDCFR(
			HUNLFlatSolveGraph graph,
			std::array<std::size_t, 2> bucket_count_per_player,
			HUNLFlatSolveMode solve_mode,
			HUNLFlatValueLayout layout,
			std::size_t workers = 1,
			double alpha = 1.5,
			double beta = 0.0,
			double gamma = 2.0,
			HUNLFlatStoragePrecision precision = HUNLFlatStoragePrecision::Float64);

		/** @brief Stops worker resources and releases owned graph storage. */
		~HUNLFlatDCFR();

		HUNLFlatDCFR(const HUNLFlatDCFR&) = delete;
		HUNLFlatDCFR& operator=(const HUNLFlatDCFR&) = delete;
		HUNLFlatDCFR(HUNLFlatDCFR&&) = delete;
		HUNLFlatDCFR& operator=(HUNLFlatDCFR&&) = delete;

		/** @brief Runs one DCFR iteration. */
		void run_iteration();
		/** @brief Runs the requested number of additional iterations. */
		void run_iterations(std::uint32_t iterations);

		/** @brief Returns the owned solve graph. */
		[[nodiscard]] const HUNLFlatSolveGraph& graph() const noexcept;
		/** @brief Returns the regret and strategy table. */
		[[nodiscard]] const HUNLFlatInfosetTable& infoset_table() const noexcept;
		/** @brief Returns mutable access to the regret and strategy table. */
		[[nodiscard]] HUNLFlatInfosetTable& infoset_table_mut() noexcept;
		/** @brief Returns accumulated stage timing. */
		[[nodiscard]] const HUNLFlatStageProfile& profile() const noexcept;
		/** @brief Returns completed iteration count. */
		[[nodiscard]] std::uint32_t iterations() const noexcept;
		/** @brief Returns player-zero reach values by node. */
		[[nodiscard]] const HUNLAlignedVector<double>& player0_reach() const noexcept;
		/** @brief Returns player-one reach values by node. */
		[[nodiscard]] const HUNLAlignedVector<double>& player1_reach() const noexcept;
		/** @brief Returns chance reach values by node. */
		[[nodiscard]] const HUNLAlignedVector<double>& chance_reach() const noexcept;
		/** @brief Returns aggregate bucket reach values. */
		[[nodiscard]] const HUNLAlignedVector<double>& bucket_reach() const noexcept;
		/** @brief Returns normalized bucket reach values. */
		[[nodiscard]] const HUNLAlignedVector<double>& normalized_bucket_reach() const noexcept;
		/** @brief Returns information-set bucket totals. */
		[[nodiscard]] const HUNLAlignedVector<double>& infoset_bucket_totals() const noexcept;
		/** @brief Returns terminal values prepared for the current iteration. */
		[[nodiscard]] const HUNLAlignedVector<double>& terminal_values() const noexcept;
		/** @brief Returns computed node values. */
		[[nodiscard]] const HUNLAlignedVector<double>& node_values() const noexcept;
		/** @brief Returns computed action values. */
		[[nodiscard]] const HUNLAlignedVector<double>& action_values() const noexcept;
		/** @brief Returns the configured worker count. */
		[[nodiscard]] std::size_t worker_count() const noexcept;
		/** @brief Returns per-worker scheduler diagnostics. */
		[[nodiscard]] const HUNLFlatSchedulerDiagnostics& scheduler_diagnostics() const noexcept;
		/** @brief Returns the bucket map, or null when no abstraction map is active. */
		[[nodiscard]] const HUNLFlatBucketMap* bucket_map() const noexcept;
		/** @brief Returns the pipeline plan used by this solver. */
		[[nodiscard]] const HUNLFlatPipelinePlan& pipeline_plan() const noexcept;
		/** @brief Estimates graph, table, solver and worker memory use. */
		[[nodiscard]] HUNLFlatMemoryEstimate memory_estimate() const;
		/** @brief Returns regret and strategy storage precision. */
		[[nodiscard]] HUNLFlatStoragePrecision storage_precision() const noexcept;

		/** @brief Exports average action probabilities keyed by infoset string. */
		[[nodiscard]] std::unordered_map<std::string, std::vector<double>> export_average_strategy() const;
		/** @brief Exports average strategy in flat table layout. */
		[[nodiscard]] HUNLFlatAverageStrategyTable export_average_strategy_table() const;

	private:
		friend class HUNLFlatPipeline;

		enum class StageCommand : std::uint8_t
		{
			Discount = 0,
			Strategy = 1,
			ReachSeed = 2,
			ReachReduceNodes = 3,
			ReachReduceBuckets = 4,
			NormalizeBucketReach = 5,
			ShowdownEquity = 6,
			DepthLimitedEval = 7,
			BackwardDepth = 8,
			Regret = 9,
			AverageStrategy = 10,
		};

		class WorkerPool
		{
		public:
			WorkerPool(HUNLFlatDCFR& owner, std::size_t worker_count);
			~WorkerPool();

			WorkerPool(const WorkerPool&) = delete;
			WorkerPool& operator=(const WorkerPool&) = delete;

			void run_stage(StageCommand command, HUNLFlatStageKind stage_kind, std::size_t depth) noexcept(false);
			[[nodiscard]] std::size_t worker_count() const noexcept;

		private:
			void worker_loop(std::size_t worker_index);

			HUNLFlatDCFR& owner_;
			std::vector<std::thread> threads_;
			std::mutex mutex_;
			std::condition_variable cv_;
			std::condition_variable finished_cv_;
			StageCommand command_ = StageCommand::Discount;
			HUNLFlatStageKind stage_kind_ = HUNLFlatStageKind::Discount;
			std::size_t depth_ = 0;
			std::exception_ptr stage_error_;
			std::size_t generation_ = 0;
			std::size_t completed_workers_ = 0;
			bool stop_ = false;
		};

		void run_stage_workers(HUNLFlatStageKind stage, StageCommand command, std::size_t depth = 0);
		static void add_stage_seconds(HUNLFlatStageProfile& profile, HUNLFlatStageKind stage, double seconds);
		void execute_stage_command(StageCommand command, std::size_t worker_index, std::size_t depth);
		void worker_discount_stage(std::size_t worker_index);
		void worker_strategy_stage(std::size_t worker_index);
		void worker_reach_seed_depth(std::size_t worker_index, std::size_t depth);
		void worker_reach_reduce_nodes_depth(std::size_t worker_index, std::size_t depth);
		void worker_reach_reduce_buckets_depth(std::size_t worker_index);
		void worker_normalize_bucket_reach_stage(std::size_t worker_index);
		void worker_showdown_equity_stage(std::size_t worker_index);
		void worker_depth_limited_eval_stage(std::size_t worker_index);
		void worker_backward_depth(std::size_t worker_index, std::size_t depth);
		void worker_regret_stage(std::size_t worker_index);
		void worker_average_strategy_stage(std::size_t worker_index);
		void apply_dcfr_discount_stage();
		void compute_strategy_stage();
		void forward_reach_stage();
		void normalize_bucket_reach_stage();
		void showdown_equity_stage();
		void depth_limited_eval_stage();
		void terminal_utility_stage();
		void backward_value_stage();
		void regret_update_stage();
		void average_strategy_stage();
		static std::optional<HUNLFlatBucketMap> load_bucket_map_for_graph(
			const HUNLFlatSolveGraph& graph,
			HUNLFlatSolveMode solve_mode);
		static std::optional<HUNLBucketTerminalTable> build_terminal_table_for_graph(
			const HUNLFlatSolveGraph& graph,
			const std::optional<HUNLFlatBucketMap>& bucket_map);

		HUNLFlatSolveGraph graph_;
		std::optional<HUNLFlatBucketMap> bucket_map_;
		std::optional<HUNLBucketTerminalTable> terminal_table_;
		HUNLFlatInfosetTable infoset_table_;
		HUNLAlignedVector<double> player0_reach_;
		HUNLAlignedVector<double> player1_reach_;
		HUNLAlignedVector<double> chance_reach_;
		HUNLAlignedVector<double> bucket_reach_;
		HUNLAlignedVector<double> normalized_bucket_reach_;
		HUNLAlignedVector<double> infoset_bucket_totals_;
		HUNLAlignedVector<double> terminal_values_;
		HUNLAlignedVector<double> node_values_;
		HUNLAlignedVector<double> action_values_;
		HUNLFlatStageProfile profile_;
		HUNLFlatStoragePrecision precision_ = HUNLFlatStoragePrecision::Float64;
		std::size_t worker_count_ = 1;
		HUNLFlatParallelPlan parallel_plan_;
		HUNLFlatPipeline pipeline_;
		std::unique_ptr<WorkerPool> worker_pool_;
		std::vector<HUNLFlatWorkerScratch, AlignedAllocator<HUNLFlatWorkerScratch, HUNL_CACHELINE_BYTES>> worker_scratch_;
		HUNLFlatSchedulerDiagnostics scheduler_diagnostics_;
		double alpha_ = 1.5;
		double beta_ = 0.0;
		double gamma_ = 2.0;
		std::uint32_t iterations_ = 0;
	};

} // namespace texas::solver::hunl
