#pragma once

#include "games/hunl_flat_graph.hpp"
#include "solver/hunl_bucket_map.hpp"
#include "util/aligned_allocator.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <type_traits>
#include <unordered_map>
#include <vector>

namespace core {

inline constexpr std::size_t HUNL_CACHELINE_BYTES = 64;

template <class T>
using HUNLAlignedVector = std::vector<T, AlignedAllocator<T, HUNL_CACHELINE_BYTES>>;

class HUNLFlatInfosetTable;

enum class HUNLFlatValueLayout : std::uint8_t {
    InfosetHandAction = 0,
    InfosetActionHand = 1,
};

enum class HUNLFlatStoragePrecision : std::uint8_t {
    Float64 = 0,
    Float32 = 1,
    Compressed16 = 2,
};

struct HUNLFlatInfosetTableMeta {
    InfosetId id{};
    std::uint32_t offset = 0;
    std::uint32_t value_count = 0;
    std::uint32_t bucket_offset = 0;
    std::uint32_t bucket_count = 0;
    std::uint32_t hand_count = 0;
    std::uint32_t last_discount_iter = 0;
    std::uint32_t reach_count = 0;
    PlayerId player = -1;
    std::uint8_t action_count = 0;
};

struct HUNLFlatRange {
    std::uint32_t begin = 0;
    std::uint32_t end = 0;
};

struct HUNLFlatWorkerAssignment {
    std::uint32_t worker_index = 0;
    HUNLFlatRange infoset_range;
    HUNLFlatRange bucket_range;
    HUNLFlatRange value_range;
    HUNLFlatRange node_range;
    std::vector<HUNLFlatRange> depth_node_ranges;
    std::vector<HUNLFlatRange> depth_reduce_ranges;
    std::vector<std::uint64_t> depth_backward_costs;
};

static_assert(std::is_trivially_copyable_v<HUNLFlatRange>, "HUNLFlatRange should stay trivially copyable");
static_assert(std::is_trivially_copyable_v<HUNLFlatInfosetTableMeta>,
              "HUNLFlatInfosetTableMeta should stay trivially copyable");

#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable: 4324)
#endif
struct alignas(HUNL_CACHELINE_BYTES) HUNLFlatWorkerScratch {
    HUNLAlignedVector<double> player0_reach;
    HUNLAlignedVector<double> player1_reach;
    HUNLAlignedVector<double> chance_reach;
    HUNLAlignedVector<double> bucket_reach;
    HUNLAlignedVector<double> row_values;
    HUNLAlignedVector<double> row_weights;
    HUNLAlignedVector<double> local_bucket_mass;
    std::vector<std::uint32_t> dirty_nodes;
    std::vector<std::uint32_t> dirty_buckets;
    std::unordered_map<std::uint32_t, std::uint32_t> next_depth_local_offsets;

    void reset_values() noexcept;
    void ensure_capacity(std::size_t node_count, std::size_t edge_count);
    void ensure_capacity(std::size_t node_count, std::size_t edge_count, std::size_t total_bucket_count);
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

struct HUNLFlatParallelPlan {
    std::vector<HUNLFlatWorkerAssignment> workers;

    static std::uint32_t estimated_backward_cost(
        const HUNLFlatNodeMeta& meta,
        const HUNLFlatInfosetTable& infoset_table);
    static HUNLFlatParallelPlan build(const HUNLFlatSolveGraph& graph, std::size_t worker_count);
    static HUNLFlatParallelPlan build(
        const HUNLFlatSolveGraph& graph,
        const HUNLFlatInfosetTable& infoset_table,
        std::size_t worker_count);
};

struct HUNLFlatMemoryEstimate {
    std::uint64_t graph_bytes = 0;
    std::uint64_t infoset_table_bytes = 0;
    std::uint64_t solver_buffers_bytes = 0;
    std::uint64_t worker_scratch_bytes = 0;
    std::uint64_t parallel_plan_bytes = 0;
    std::uint64_t auxiliary_bytes = 0;

    [[nodiscard]] std::uint64_t total_bytes() const noexcept;
};

struct HUNLFlatMemoryEstimateOptions {
    bool include_solver_buffers = true;
    bool include_worker_scratch = true;
    bool include_parallel_plan = true;
    std::size_t max_child_count = 0;
    std::size_t max_bucket_count = 0;
    std::uint64_t auxiliary_bytes = 0;
};

[[nodiscard]] HUNLFlatMemoryEstimate estimate_memory(
    const HUNLFlatSolveGraph& graph,
    const HUNLFlatInfosetTable& infoset_table,
    std::size_t worker_count,
    const HUNLFlatMemoryEstimateOptions& options = {});

class HUNLFlatInfosetTable {
public:
    static HUNLFlatInfosetTable build(
        const HUNLFlatSolveGraph& graph,
        const std::array<std::size_t, 2>& bucket_count_per_player,
        HUNLFlatValueLayout layout,
        HUNLFlatStoragePrecision precision = HUNLFlatStoragePrecision::Float64);

    static HUNLFlatInfosetTable build(
        const HUNLFlatSolveGraph& graph,
        const std::array<std::size_t, 2>& bucket_count_per_player,
        const HUNLFlatBucketMap* bucket_map = nullptr,
        HUNLFlatValueLayout layout = HUNLFlatValueLayout::InfosetHandAction,
        HUNLFlatStoragePrecision precision = HUNLFlatStoragePrecision::Float64);

    [[nodiscard]] const std::vector<HUNLFlatInfosetTableMeta>& meta() const noexcept;
    [[nodiscard]] std::vector<HUNLFlatInfosetTableMeta>& meta_mut() noexcept;
    [[nodiscard]] std::size_t infoset_count() const noexcept;
    [[nodiscard]] HUNLFlatValueLayout layout() const noexcept;
    [[nodiscard]] HUNLFlatStoragePrecision precision() const noexcept;

    [[nodiscard]] const double* regret(InfosetId id) const;
    [[nodiscard]] double* regret_mut(InfosetId id);
    [[nodiscard]] const double* strategy_sum(InfosetId id) const;
    [[nodiscard]] double* strategy_sum_mut(InfosetId id);
    [[nodiscard]] const double* current_strategy(InfosetId id) const;
    [[nodiscard]] double* current_strategy_mut(InfosetId id);

    [[nodiscard]] std::size_t row_value_count(InfosetId id) const;
    [[nodiscard]] std::size_t total_value_count() const noexcept;
    [[nodiscard]] std::size_t total_bucket_count() const noexcept;
    [[nodiscard]] std::size_t value_index(InfosetId id, std::size_t bucket_idx, std::size_t action_idx) const;
    [[nodiscard]] HUNLFlatRange infoset_bucket_range(InfosetId id) const;
    [[nodiscard]] HUNLFlatRange infoset_value_range(HUNLFlatRange infoset_range) const;
    [[nodiscard]] std::uint64_t regret_storage_bytes() const noexcept;
    [[nodiscard]] std::uint64_t strategy_sum_storage_bytes() const noexcept;
    [[nodiscard]] std::uint64_t current_strategy_storage_bytes() const noexcept;
    [[nodiscard]] double regret_value(InfosetId id, std::size_t offset) const;
    [[nodiscard]] double strategy_sum_value(InfosetId id, std::size_t offset) const;
    [[nodiscard]] double current_strategy_value(InfosetId id, std::size_t offset) const;
    void set_regret_value(InfosetId id, std::size_t offset, double value);
    void set_strategy_sum_value(InfosetId id, std::size_t offset, double value);
    void set_current_strategy_value(InfosetId id, std::size_t offset, double value);
    void copy_regret_values(InfosetId id, std::size_t offset, double* out, std::size_t count) const;
    void copy_strategy_sum_values(InfosetId id, std::size_t offset, double* out, std::size_t count) const;
    void copy_current_strategy_values(InfosetId id, std::size_t offset, double* out, std::size_t count) const;
    void write_regret_values(InfosetId id, std::size_t offset, const double* values, std::size_t count);
    void write_strategy_sum_values(InfosetId id, std::size_t offset, const double* values, std::size_t count);
    void write_current_strategy_values(InfosetId id, std::size_t offset, const double* values, std::size_t count);
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

}  // namespace core
