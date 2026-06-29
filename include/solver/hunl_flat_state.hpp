#pragma once

#include "games/hunl_flat_graph.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace core {

enum class HUNLFlatValueLayout : std::uint8_t {
    InfosetHandAction = 0,
    InfosetActionHand = 1,
};

struct HUNLFlatInfosetTableMeta {
    InfosetId id{};
    std::uint32_t offset = 0;
    std::uint32_t value_count = 0;
    std::uint32_t hand_count = 0;
    std::uint8_t action_count = 0;
    PlayerId player = -1;
    std::uint32_t last_discount_iter = 0;
};

struct HUNLFlatRange {
    std::uint32_t begin = 0;
    std::uint32_t end = 0;
};

struct HUNLFlatWorkerAssignment {
    std::uint32_t worker_index = 0;
    HUNLFlatRange infoset_range;
    HUNLFlatRange node_range;
    std::vector<HUNLFlatRange> depth_node_ranges;
};

struct HUNLFlatWorkerScratch {
    std::vector<double> terminal_values;
    std::vector<double> node_values;
    std::vector<double> action_values;
    std::vector<double> player0_reach;
    std::vector<double> player1_reach;
    std::vector<double> chance_reach;

    void reset() noexcept;
};

struct HUNLFlatParallelPlan {
    std::vector<HUNLFlatWorkerAssignment> workers;

    static HUNLFlatParallelPlan build(const HUNLFlatSolveGraph& graph, std::size_t worker_count);
};

class HUNLFlatInfosetTable {
public:
    static HUNLFlatInfosetTable build(
        const HUNLFlatSolveGraph& graph,
        const std::array<std::size_t, 2>& hand_count_per_player,
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
    [[nodiscard]] std::size_t value_index(InfosetId id, std::size_t hand_idx, std::size_t action_idx) const;
    [[nodiscard]] HUNLFlatRange infoset_value_range(HUNLFlatRange infoset_range) const;

private:
    const HUNLFlatInfosetTableMeta& meta_for(InfosetId id) const;
    HUNLFlatInfosetTableMeta& meta_for(InfosetId id);

    HUNLFlatValueLayout layout_ = HUNLFlatValueLayout::InfosetHandAction;
    std::vector<HUNLFlatInfosetTableMeta> meta_;
    std::vector<double> regret_sum_;
    std::vector<double> strategy_sum_;
    std::vector<double> current_strategy_;
};

}  // namespace core
