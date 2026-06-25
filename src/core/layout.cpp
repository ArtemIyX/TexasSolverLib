#include "core/layout.hpp"

#include <algorithm>
#include <stdexcept>

namespace core {

FlatInfosetStore::FlatInfosetStore(std::size_t row_width) : row_width_(std::max<std::size_t>(row_width, 1)) {}

std::size_t FlatInfosetStore::len() const noexcept {
    return meta_.size();
}

bool FlatInfosetStore::is_empty() const noexcept {
    return meta_.empty();
}

InfosetId FlatInfosetStore::intern(const std::string& key, std::size_t num_actions) {
    if (num_actions == 0) {
        throw std::invalid_argument("FlatInfosetStore::intern requires num_actions > 0");
    }
    if (num_actions > row_width_) {
        throw std::invalid_argument("FlatInfosetStore::intern num_actions exceeds row_width");
    }

    if (const auto it = key_to_id_.find(key); it != key_to_id_.end()) {
        return it->second;
    }

    const InfosetId id{static_cast<std::uint32_t>(meta_.size())};
    const auto offset = static_cast<std::uint32_t>(static_cast<std::size_t>(id.value) * row_width_);
    const auto needed = static_cast<std::size_t>(offset) + row_width_;
    if (needed > regret_arena_.size()) {
        const auto new_size = arena_size_for(needed, row_width_);
        regret_arena_.resize(new_size, 0.0);
        strategy_arena_.resize(new_size, 0.0);
    }

    meta_.push_back(RowMeta{
        offset,
        static_cast<std::uint16_t>(num_actions),
        0,
    });
    id_to_key_.push_back(key);
    key_to_id_.emplace(key, id);
    return id;
}

const std::vector<std::string>& FlatInfosetStore::id_to_key() const noexcept {
    return id_to_key_;
}

const std::vector<RowMeta>& FlatInfosetStore::meta() const noexcept {
    return meta_;
}

std::size_t FlatInfosetStore::row_width() const noexcept {
    return row_width_;
}

const double* FlatInfosetStore::regret(InfosetId id) const {
    const auto& meta = meta_for(id);
    return regret_arena_.data() + meta.offset;
}

double* FlatInfosetStore::regret_mut(InfosetId id) {
    const auto& meta = meta_for(id);
    return regret_arena_.data() + meta.offset;
}

const double* FlatInfosetStore::strategy_sum(InfosetId id) const {
    const auto& meta = meta_for(id);
    return strategy_arena_.data() + meta.offset;
}

double* FlatInfosetStore::strategy_sum_mut(InfosetId id) {
    const auto& meta = meta_for(id);
    return strategy_arena_.data() + meta.offset;
}

std::size_t FlatInfosetStore::row_size(InfosetId id) const {
    return meta_for(id).num_actions;
}

std::tuple<double*, double*, RowMeta*> FlatInfosetStore::row_mut(InfosetId id) {
    auto& meta = meta_for(id);
    return {regret_arena_.data() + meta.offset, strategy_arena_.data() + meta.offset, &meta};
}

std::size_t FlatInfosetStore::regret_arena_size() const noexcept {
    return regret_arena_.size();
}

std::size_t FlatInfosetStore::strategy_arena_size() const noexcept {
    return strategy_arena_.size();
}

std::size_t FlatInfosetStore::arena_size_for(std::size_t required, std::size_t row_width) {
    const auto block_width = BLOCK_SIZE * row_width;
    const auto blocks_needed = (required + block_width - 1) / block_width;
    return blocks_needed * block_width;
}

RowMeta& FlatInfosetStore::meta_for(InfosetId id) {
    if (id.value >= meta_.size()) {
        throw std::out_of_range("FlatInfosetStore invalid InfosetId");
    }
    return meta_[id.value];
}

const RowMeta& FlatInfosetStore::meta_for(InfosetId id) const {
    if (id.value >= meta_.size()) {
        throw std::out_of_range("FlatInfosetStore invalid InfosetId");
    }
    return meta_[id.value];
}

}  // namespace core
