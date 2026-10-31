#pragma once

/** @file
 * @brief Flat HUNL solver layouts, aligned storage, worker ranges and memory estimates.
 */

#include "games/hunl_flat_graph.hpp"
#include "solver/hunl/flat/hunl_flat_types.hpp"
#include "solver/hunl/bucket/hunl_bucket_map.hpp"
#include "util/aligned_allocator.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <type_traits>
#include <unordered_map>
#include <vector>

namespace texas::solver::hunl
{

	/** @brief Alignment boundary used by flat HUNL arrays and worker scratch. */
	inline constexpr std::size_t HUNL_CACHELINE_BYTES = 64;

	/** @brief Vector using the flat solver's configured cache-line allocator. */
	template <class T>
	using HUNLAlignedVector = std::vector<T, AlignedAllocator<T, HUNL_CACHELINE_BYTES>>;

	/** @brief Forward declaration of the flat information-set value table. */
	class HUNLFlatInfosetTable;

	/** @brief Contiguous storage offsets and dimensions for one information set. */
	struct HUNLFlatInfosetTableMeta
	{
		InfosetId id{};						  ///< Graph information-set ID.
		std::size_t offset = 0;				  ///< First value offset in flat storage.
		std::size_t value_count = 0;		  ///< Number of action-hand values.
		std::size_t bucket_offset = 0;		  ///< First bucket offset in reach storage.
		std::size_t bucket_count = 0;		  ///< Number of buckets allocated for this player.
		std::size_t hand_count = 0;			  ///< Concrete hands represented per bucket.
		std::uint32_t last_discount_iter = 0; ///< Last applied DCFR discount iteration.
		std::size_t reach_count = 0;		  ///< Number of reach values stored.
		PlayerId player = -1;				  ///< Player owning the information set.
		std::uint8_t action_count = 0;		  ///< Number of actions at this information set.
	};

	/** @brief Half-open flat-array range [begin, end). */
	struct HUNLFlatRange
	{
		std::size_t begin = 0; ///< First included index.
		std::size_t end = 0;   ///< One past the last included index.
	};

	/** @brief Per-worker partitions across infosets, buckets, values, nodes and depths. */
	struct HUNLFlatWorkerAssignment
	{
		std::uint32_t worker_index = 0;					 ///< Worker assigned these ranges.
		HUNLFlatRange infoset_range;					 ///< Information-set range.
		HUNLFlatRange bucket_range;						 ///< Bucket range.
		HUNLFlatRange value_range;						 ///< Value-array range.
		HUNLFlatRange node_range;						 ///< Node range.
		std::vector<HUNLFlatRange> depth_node_ranges;	 ///< Node ranges by depth.
		std::vector<HUNLFlatRange> depth_reduce_ranges;	 ///< Reduction ranges by depth.
		std::vector<std::uint64_t> depth_backward_costs; ///< Estimated backward work by depth.
	};

	static_assert(std::is_trivially_copyable_v<HUNLFlatRange>, "HUNLFlatRange should stay trivially copyable");
	static_assert(std::is_trivially_copyable_v<HUNLFlatInfosetTableMeta>,
		"HUNLFlatInfosetTableMeta should stay trivially copyable");

#if defined(_MSC_VER)
	#pragma warning(push)
	#pragma warning(disable : 4324)
#endif
	/** @brief Cache-line-aligned temporary arrays owned by one flat solver worker. */
	struct alignas(HUNL_CACHELINE_BYTES) HUNLFlatWorkerScratch
	{
		HUNLAlignedVector<double> player0_reach;								   ///< Temporary player-zero reach values.
		HUNLAlignedVector<double> player1_reach;								   ///< Temporary player-one reach values.
		HUNLAlignedVector<double> chance_reach;									   ///< Temporary chance reach values.
		HUNLAlignedVector<double> bucket_reach;									   ///< Temporary bucket reach values.
		HUNLAlignedVector<double> row_values;									   ///< Temporary row values.
		HUNLAlignedVector<double> row_weights;									   ///< Temporary row weights.
		HUNLAlignedVector<double> local_bucket_mass;							   ///< Worker-local bucket mass.
		std::vector<std::uint32_t> dirty_nodes;									   ///< Nodes touched by sparse worker operations.
		std::vector<std::uint32_t> dirty_buckets;								   ///< Buckets touched by sparse worker operations.
		std::unordered_map<std::uint32_t, std::uint32_t> next_depth_local_offsets; ///< Local offset by next-depth node.

		/** @brief Clears active values while retaining allocated capacity. */
		void reset_values() noexcept;
		/** @brief Ensures node and edge scratch capacities. */
		void ensure_capacity(std::size_t node_count, std::size_t edge_count);
		/** @brief Ensures node, edge and bucket scratch capacities. */
		void ensure_capacity(std::size_t node_count, std::size_t edge_count, std::size_t total_bucket_count);
		/** @brief Ensures scratch capacities using graph maxima for child and bucket fanout. */
		void ensure_capacity(
			std::size_t node_count,
			std::size_t edge_count,
			std::size_t total_bucket_count,
			std::size_t max_child_count,
			std::size_t max_bucket_count);
	};
#if defined(_MSC_VER)
	#pragma warning(pop)
#endif

	/** @brief Partition of flat graph arrays into worker assignments. */
	struct HUNLFlatParallelPlan
	{
		std::vector<HUNLFlatWorkerAssignment> workers; ///< Assignment for each worker index.

		/** @brief Estimates backward-pass cost for one node. */
		static std::uint32_t estimated_backward_cost(
			const HUNLFlatNodeMeta& meta,
			const HUNLFlatInfosetTable& infoset_table);
		/** @brief Builds a plan using graph structure and worker count. */
		static HUNLFlatParallelPlan build(const HUNLFlatSolveGraph& graph, std::size_t worker_count);
		/** @brief Builds a cost-aware plan using graph and infoset metadata. */
		static HUNLFlatParallelPlan build(
			const HUNLFlatSolveGraph& graph,
			const HUNLFlatInfosetTable& infoset_table,
			std::size_t worker_count);
	};

	/** @brief Estimated memory use of graph, solver and worker storage. */
	struct HUNLFlatMemoryEstimate
	{
		std::uint64_t graph_bytes = 0;			///< Graph and graph-index storage.
		std::uint64_t infoset_table_bytes = 0;	///< Regret and strategy table storage.
		std::uint64_t solver_buffers_bytes = 0; ///< Solver-wide temporary arrays.
		std::uint64_t worker_scratch_bytes = 0; ///< Per-worker scratch arrays.
		std::uint64_t parallel_plan_bytes = 0;	///< Worker partition metadata.
		std::uint64_t auxiliary_bytes = 0;		///< Caller-supplied additional estimate.

		/** @brief Sums the estimate components. */
		[[nodiscard]] std::uint64_t total_bytes() const noexcept;
	};

	/** @brief Controls which buffers contribute to a flat solver memory estimate. */
	struct HUNLFlatMemoryEstimateOptions
	{
		bool include_solver_buffers = true; ///< Include solver-wide temporary storage.
		bool include_worker_scratch = true; ///< Include per-worker scratch storage.
		bool include_parallel_plan = true;	///< Include worker partition metadata.
		std::size_t max_child_count = 0;	///< Optional maximum child fanout for scratch sizing.
		std::size_t max_bucket_count = 0;	///< Optional maximum bucket fanout for scratch sizing.
		std::uint64_t auxiliary_bytes = 0;	///< Additional caller-provided bytes.
	};

	/** @brief Estimates memory required by a flat graph and solver configuration. */
	[[nodiscard]] HUNLFlatMemoryEstimate estimate_memory(
		const HUNLFlatSolveGraph& graph,
		const HUNLFlatInfosetTable& infoset_table,
		std::size_t worker_count,
		const HUNLFlatMemoryEstimateOptions& options = {});

	/** @brief Flat regret, strategy-sum and current-strategy storage by information set. */
	class HUNLFlatInfosetTable
	{
	public:
		/** @brief Builds table storage from graph, per-player bucket counts and layout. */
		static HUNLFlatInfosetTable build(
			const HUNLFlatSolveGraph& graph,
			const std::array<std::size_t, 2>& bucket_count_per_player,
			HUNLFlatValueLayout layout,
			HUNLFlatStoragePrecision precision = HUNLFlatStoragePrecision::Float64);

		/** @brief Builds table storage using optional graph-aligned bucket mapping. */
		static HUNLFlatInfosetTable build(
			const HUNLFlatSolveGraph& graph,
			const std::array<std::size_t, 2>& bucket_count_per_player,
			const HUNLFlatBucketMap* bucket_map = nullptr,
			HUNLFlatValueLayout layout = HUNLFlatValueLayout::InfosetHandAction,
			HUNLFlatStoragePrecision precision = HUNLFlatStoragePrecision::Float64);

		/** @brief Returns read-only information-set metadata. */
		[[nodiscard]] const std::vector<HUNLFlatInfosetTableMeta>& meta() const noexcept;
		/** @brief Returns mutable information-set metadata. */
		[[nodiscard]] std::vector<HUNLFlatInfosetTableMeta>& meta_mut() noexcept;
		/** @brief Returns the number of allocated information sets. */
		[[nodiscard]] std::size_t infoset_count() const noexcept;
		/** @brief Returns the table's action-hand value layout. */
		[[nodiscard]] HUNLFlatValueLayout layout() const noexcept;
		/** @brief Returns the table's scalar storage precision. */
		[[nodiscard]] HUNLFlatStoragePrecision precision() const noexcept;

		/** @brief Returns the regret row for an information set. */
		[[nodiscard]] const double* regret(InfosetId id) const;
		/** @brief Returns the mutable regret row for an information set. */
		[[nodiscard]] double* regret_mut(InfosetId id);
		/** @brief Returns the cumulative strategy row for an information set. */
		[[nodiscard]] const double* strategy_sum(InfosetId id) const;
		/** @brief Returns the mutable cumulative strategy row. */
		[[nodiscard]] double* strategy_sum_mut(InfosetId id);
		/** @brief Returns current regret-matched strategy values for an information set. */
		[[nodiscard]] const double* current_strategy(InfosetId id) const;
		/** @brief Returns mutable current strategy values for an information set. */
		[[nodiscard]] double* current_strategy_mut(InfosetId id);

		/** @brief Returns value count for an information-set row. */
		[[nodiscard]] std::size_t row_value_count(InfosetId id) const;
		/** @brief Returns total allocated value count. */
		[[nodiscard]] std::size_t total_value_count() const noexcept;
		/** @brief Returns total bucket count across information sets. */
		[[nodiscard]] std::size_t total_bucket_count() const noexcept;
		/** @brief Computes the flat row offset for a bucket and action. */
		[[nodiscard]] std::size_t value_index(InfosetId id, std::size_t bucket_idx, std::size_t action_idx) const;
		/** @brief Returns the bucket range owned by an information-set range. */
		[[nodiscard]] HUNLFlatRange infoset_bucket_range(InfosetId id) const;
		/** @brief Converts an information-set range to its value-storage range. */
		[[nodiscard]] HUNLFlatRange infoset_value_range(HUNLFlatRange infoset_range) const;
		/** @brief Reports bytes allocated for regret values. */
		[[nodiscard]] std::uint64_t regret_storage_bytes() const noexcept;
		/** @brief Reports bytes allocated for strategy sums. */
		[[nodiscard]] std::uint64_t strategy_sum_storage_bytes() const noexcept;
		/** @brief Reports bytes allocated for current strategy values. */
		[[nodiscard]] std::uint64_t current_strategy_storage_bytes() const noexcept;
		/** @brief Reads one regret value by row offset. */
		[[nodiscard]] double regret_value(InfosetId id, std::size_t offset) const;
		/** @brief Reads one strategy-sum value by row offset. */
		[[nodiscard]] double strategy_sum_value(InfosetId id, std::size_t offset) const;
		/** @brief Reads one current-strategy value by row offset. */
		[[nodiscard]] double current_strategy_value(InfosetId id, std::size_t offset) const;
		/** @brief Writes one regret value by row offset. */
		void set_regret_value(InfosetId id, std::size_t offset, double value);
		/** @brief Writes one strategy-sum value by row offset. */
		void set_strategy_sum_value(InfosetId id, std::size_t offset, double value);
		/** @brief Writes one current-strategy value by row offset. */
		void set_current_strategy_value(InfosetId id, std::size_t offset, double value);
		/** @brief Copies regret values from a row into out. */
		void copy_regret_values(InfosetId id, std::size_t offset, double* out, std::size_t count) const;
		/** @brief Copies strategy sums from a row into out. */
		void copy_strategy_sum_values(InfosetId id, std::size_t offset, double* out, std::size_t count) const;
		/** @brief Copies current strategy values from a row into out. */
		void copy_current_strategy_values(InfosetId id, std::size_t offset, double* out, std::size_t count) const;
		/** @brief Writes regret values from an input span into a row. */
		void write_regret_values(InfosetId id, std::size_t offset, const double* values, std::size_t count);
		/** @brief Writes strategy sums from an input span into a row. */
		void write_strategy_sum_values(InfosetId id, std::size_t offset, const double* values, std::size_t count);
		/** @brief Writes current strategy values from an input span into a row. */
		void write_current_strategy_values(InfosetId id, std::size_t offset, const double* values, std::size_t count);
		/** @brief Applies positive-regret, negative-regret and strategy-sum multipliers to a row. */
		void discount_values(
			InfosetId id,
			double pos_scale,
			double neg_scale,
			double strat_scale);

	private:
		const HUNLFlatInfosetTableMeta& meta_for(InfosetId id) const;
		HUNLFlatInfosetTableMeta& meta_for(InfosetId id);
		template <class T>
		[[nodiscard]] static std::uint64_t storage_bytes(const HUNLAlignedVector<T>& values) noexcept;
		template <class T>
		[[nodiscard]] static double value_at(const HUNLAlignedVector<T>& values, std::size_t offset) noexcept;
		template <class T>
		static void set_value_at(HUNLAlignedVector<T>& values, std::size_t offset, double value);
		template <class T>
		static void copy_values_from_storage(
			const HUNLAlignedVector<T>& values,
			std::size_t offset,
			double* out,
			std::size_t count);
		template <class T>
		static void write_values_to_storage(
			HUNLAlignedVector<T>& values,
			std::size_t offset,
			const double* in,
			std::size_t count);
		template <class T>
		static void discount_storage(
			HUNLAlignedVector<T>& regret_values,
			HUNLAlignedVector<T>& strategy_values,
			std::size_t offset,
			std::size_t count,
			double pos_scale,
			double neg_scale,
			double strat_scale);

		HUNLFlatValueLayout layout_ = HUNLFlatValueLayout::InfosetHandAction;
		HUNLFlatStoragePrecision precision_ = HUNLFlatStoragePrecision::Float64;
		std::vector<HUNLFlatInfosetTableMeta> meta_;
		HUNLAlignedVector<double> regret_sum_;
		HUNLAlignedVector<double> strategy_sum_;
		HUNLAlignedVector<double> current_strategy_;
		HUNLAlignedVector<float> regret_sum_f32_;
		HUNLAlignedVector<float> strategy_sum_f32_;
		HUNLAlignedVector<float> current_strategy_f32_;
	};

} // namespace texas::solver::hunl
