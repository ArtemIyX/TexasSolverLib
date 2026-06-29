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
};

class HUNLFlatInfosetTable {
public:
    static HUNLFlatInfosetTable build(
        const HUNLFlatSolveGraph& graph,
        const std::array<std::size_t, 2>& hand_count_per_player,
        HUNLFlatValueLayout layout = HUNLFlatValueLayout::InfosetHandAction);

    [[nodiscard]] const std::vector<HUNLFlatInfosetTableMeta>& meta() const noexcept;
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
