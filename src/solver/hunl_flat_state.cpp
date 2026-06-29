#include "solver/hunl_flat_state.hpp"

#include <stdexcept>

namespace core {

HUNLFlatInfosetTable HUNLFlatInfosetTable::build(
    const HUNLFlatSolveGraph& graph,
    const std::array<std::size_t, 2>& hand_count_per_player,
    HUNLFlatValueLayout layout) {
    HUNLFlatInfosetTable table;
    table.layout_ = layout;
    table.meta_.reserve(graph.infosets.size());

    std::uint32_t running_offset = 0;
    for (const auto& infoset : graph.infosets) {
        if (infoset.player < 0 || infoset.player > 1) {
            throw std::logic_error("flat infoset table requires player-owned infosets");
        }

        const auto hand_count = hand_count_per_player[static_cast<std::size_t>(infoset.player)];
        const auto value_count =
            static_cast<std::uint32_t>(hand_count * static_cast<std::size_t>(infoset.action_count));

        table.meta_.push_back(HUNLFlatInfosetTableMeta{
            infoset.id,
            running_offset,
            value_count,
            static_cast<std::uint32_t>(hand_count),
            infoset.action_count,
            infoset.player,
        });
        running_offset += value_count;
    }

    table.regret_sum_.assign(running_offset, 0.0);
    table.strategy_sum_.assign(running_offset, 0.0);
    table.current_strategy_.assign(running_offset, 0.0);
    return table;
}

const std::vector<HUNLFlatInfosetTableMeta>& HUNLFlatInfosetTable::meta() const noexcept {
    return meta_;
}

std::size_t HUNLFlatInfosetTable::infoset_count() const noexcept {
    return meta_.size();
}

HUNLFlatValueLayout HUNLFlatInfosetTable::layout() const noexcept {
    return layout_;
}

const double* HUNLFlatInfosetTable::regret(InfosetId id) const {
    return regret_sum_.data() + meta_for(id).offset;
}

double* HUNLFlatInfosetTable::regret_mut(InfosetId id) {
    return regret_sum_.data() + meta_for(id).offset;
}

const double* HUNLFlatInfosetTable::strategy_sum(InfosetId id) const {
    return strategy_sum_.data() + meta_for(id).offset;
}

double* HUNLFlatInfosetTable::strategy_sum_mut(InfosetId id) {
    return strategy_sum_.data() + meta_for(id).offset;
}

const double* HUNLFlatInfosetTable::current_strategy(InfosetId id) const {
    return current_strategy_.data() + meta_for(id).offset;
}

double* HUNLFlatInfosetTable::current_strategy_mut(InfosetId id) {
    return current_strategy_.data() + meta_for(id).offset;
}

std::size_t HUNLFlatInfosetTable::row_value_count(InfosetId id) const {
    return meta_for(id).value_count;
}

std::size_t HUNLFlatInfosetTable::total_value_count() const noexcept {
    return regret_sum_.size();
}

std::size_t HUNLFlatInfosetTable::value_index(InfosetId id, std::size_t hand_idx, std::size_t action_idx) const {
    const auto& meta = meta_for(id);
    if (hand_idx >= meta.hand_count) {
        throw std::out_of_range("HUNLFlatInfosetTable hand_idx out of range");
    }
    if (action_idx >= meta.action_count) {
        throw std::out_of_range("HUNLFlatInfosetTable action_idx out of range");
    }

    if (layout_ == HUNLFlatValueLayout::InfosetHandAction) {
        return meta.offset + hand_idx * static_cast<std::size_t>(meta.action_count) + action_idx;
    }
    return meta.offset + action_idx * static_cast<std::size_t>(meta.hand_count) + hand_idx;
}

const HUNLFlatInfosetTableMeta& HUNLFlatInfosetTable::meta_for(InfosetId id) const {
    if (id.value >= meta_.size()) {
        throw std::out_of_range("HUNLFlatInfosetTable invalid InfosetId");
    }
    return meta_[id.value];
}

HUNLFlatInfosetTableMeta& HUNLFlatInfosetTable::meta_for(InfosetId id) {
    if (id.value >= meta_.size()) {
        throw std::out_of_range("HUNLFlatInfosetTable invalid InfosetId");
    }
    return meta_[id.value];
}

}  // namespace core
