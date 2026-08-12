#include "util/layout.hpp"

#include <algorithm>
#include <limits>
#include <stdexcept>

namespace texas::util {

FlatInfosetStore::FlatInfosetStore(std::size_t row_width)
    : row_width_(row_width) {
    if (row_width == 0 ||
        row_width > std::numeric_limits<std::uint16_t>::max()) {
        throw std::invalid_argument(
            "FlatInfosetStore row_width must fit the row action metadata");
    }
}

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
    if (regret_arena_.size() != strategy_arena_.size()) {
        throw std::logic_error("FlatInfosetStore arena sizes diverged");
    }

    if (const auto it = key_to_id_.find(key); it != key_to_id_.end()) {
        if (meta_for(it->second).num_actions != num_actions) {
            throw std::invalid_argument(
                "FlatInfosetStore reused key with a different action count");
        }
        return it->second;
    }

    if (meta_.size() > std::numeric_limits<std::uint32_t>::max()) {
        throw std::length_error("FlatInfosetStore exhausted InfosetId values");
    }
    if (meta_.size() >
        std::numeric_limits<std::size_t>::max() / row_width_) {
        throw std::length_error("FlatInfosetStore row offset overflow");
    }
    const InfosetId id{static_cast<std::uint32_t>(meta_.size())};
    const auto offset = meta_.size() * row_width_;
    if (offset > std::numeric_limits<std::size_t>::max() - row_width_) {
        throw std::length_error("FlatInfosetStore arena requirement overflow");
    }
    const auto needed = offset + row_width_;
    if (needed > regret_arena_.max_size() ||
        needed > strategy_arena_.max_size()) {
        throw std::length_error("FlatInfosetStore arena exceeds vector capacity");
    }
    auto new_size = regret_arena_.size();
    if (needed > regret_arena_.size()) {
        new_size = arena_size_for(needed, row_width_);
        if (new_size > regret_arena_.max_size() ||
            new_size > strategy_arena_.max_size()) {
            throw std::length_error("FlatInfosetStore rounded arena exceeds vector capacity");
        }
    }

    regret_arena_.reserve(new_size);
    strategy_arena_.reserve(new_size);
    meta_.reserve(meta_.size() + 1U);
    id_to_key_.reserve(id_to_key_.size() + 1U);
    key_to_id_.reserve(key_to_id_.size() + 1U);

    const auto old_regret_size = regret_arena_.size();
    const auto old_strategy_size = strategy_arena_.size();
    const auto old_meta_size = meta_.size();
    const auto old_key_size = id_to_key_.size();
    try {
        regret_arena_.resize(new_size, 0.0);
        strategy_arena_.resize(new_size, 0.0);
        meta_.push_back(RowMeta{
            offset,
            static_cast<std::uint16_t>(num_actions),
            0,
        });
        id_to_key_.push_back(key);
        const auto inserted = key_to_id_.emplace(id_to_key_.back(), id);
        if (!inserted.second) {
            throw std::logic_error("FlatInfosetStore duplicate key insertion");
        }
    } catch (...) {
        regret_arena_.resize(old_regret_size);
        strategy_arena_.resize(old_strategy_size);
        meta_.resize(old_meta_size);
        id_to_key_.resize(old_key_size);
        throw;
    }
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
    if (row_width == 0 ||
        row_width > std::numeric_limits<std::size_t>::max() / BLOCK_SIZE) {
        throw std::length_error("FlatInfosetStore block width overflow");
    }
    const auto block_width = BLOCK_SIZE * row_width;
    if (required > std::numeric_limits<std::size_t>::max() - (block_width - 1U)) {
        throw std::length_error("FlatInfosetStore block rounding overflow");
    }
    const auto blocks_needed = (required + block_width - 1) / block_width;
    if (blocks_needed > std::numeric_limits<std::size_t>::max() / block_width) {
        throw std::length_error("FlatInfosetStore rounded arena overflow");
    }
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

}  // namespace texas::util


