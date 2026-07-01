#pragma once

#include "games/hunl_flat_graph.hpp"
#include "solver/hunl_bucket_map.hpp"
#include "util/aligned_allocator.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <type_traits>
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
};

static_assert(std::is_trivially_copyable_v<HUNLFlatRange>, "HUNLFlatRange should stay trivially copyable");
static_assert(std::is_trivially_copyable_v<HUNLFlatInfosetTableMeta>,
              "HUNLFlatInfosetTableMeta should stay trivially copyable");

#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable: 4324)
#endif
struct alignas(HUNL_CACHELINE_BYTES) HUNLFlatWorkerScratch {
    HUNLAlignedVector<double> terminal_values;
    HUNLAlignedVector<double> node_values;
    HUNLAlignedVector<double> action_values;
    HUNLAlignedVector<double> player0_reach;
    HUNLAlignedVector<double> player1_reach;
    HUNLAlignedVector<double> chance_reach;
    HUNLAlignedVector<double> bucket_reach;
    HUNLAlignedVector<double> row_values;
    HUNLAlignedVector<double> row_weights;
    HUNLAlignedVector<double> local_bucket_mass;
    std::vector<std::uint32_t> dirty_nodes;
    std::vector<std::uint32_t> dirty_buckets;

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

    static HUNLFlatParallelPlan build(const HUNLFlatSolveGraph& graph, std::size_t worker_count);
    static HUNLFlatParallelPlan build(
        const HUNLFlatSolveGraph& graph,
        const HUNLFlatInfosetTable& infoset_table,
        std::size_t worker_count);
};

class HUNLFlatInfosetTable {
public:
    static HUNLFlatInfosetTable build(
        const HUNLFlatSolveGraph& graph,
        const std::array<std::size_t, 2>& bucket_count_per_player,
        HUNLFlatValueLayout layout);

    static HUNLFlatInfosetTable build(
        const HUNLFlatSolveGraph& graph,
        const std::array<std::size_t, 2>& bucket_count_per_player,
        const HUNLFlatBucketMap* bucket_map = nullptr,
        HUNLFlatValueLayout layout = HUNLFlatValueLayout::InfosetHandAction);

    [[nodiscard]] const std::vector<HUNLFlatInfosetTableMeta>& meta() const noexcept;
    [[nodiscard]] std::vector<HUNLFlatInfosetTableMeta>& meta_mut() noexcept;
    [[nodiscard]] std::size_t infoset_count() const noexcept;
    [[nodiscard]] HUNLFlatValueLayout layout() const noexcept;

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

private:
    const HUNLFlatInfosetTableMeta& meta_for(InfosetId id) const;
    HUNLFlatInfosetTableMeta& meta_for(InfosetId id);

    HUNLFlatValueLayout layout_ = HUNLFlatValueLayout::InfosetHandAction;
    std::vector<HUNLFlatInfosetTableMeta> meta_;
    HUNLAlignedVector<double> regret_sum_;
    HUNLAlignedVector<double> strategy_sum_;
    HUNLAlignedVector<double> current_strategy_;
};

}  // namespace core
