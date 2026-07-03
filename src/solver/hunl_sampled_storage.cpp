#include "solver/hunl_sampled_storage.hpp"

#include <stdexcept>

namespace core {

HUNLSampledStorage::HUNLSampledStorage(
    HUNLFlatValueLayout layout,
    HUNLFlatStoragePrecision precision)
    : layout_(layout), precision_(precision) {
}

HUNLSampledRowView HUNLSampledStorage::ensure_row(const HUNLSampledInfosetShape& shape) {
    const auto it = row_lookup_.find(shape.id);
    if (it != row_lookup_.end()) {
        return view_mut(shape.id);
    }

    HUNLSampledInfosetMeta meta;
    meta.id = shape.id;
    meta.player = shape.player;
    meta.street = shape.street;
    meta.bucket_count = shape.bucket_count;
    meta.action_count = shape.action_count;
    meta.regret_offset = static_cast<std::uint32_t>(regret_.size());
    meta.strategy_sum_offset = static_cast<std::uint32_t>(strategy_sum_.size());

    const auto value_count = static_cast<std::size_t>(meta.value_count());
    regret_.resize(regret_.size() + value_count, 0.0f);
    strategy_sum_.resize(strategy_sum_.size() + value_count, 0.0f);

    const auto row_index_value = meta_.size();
    meta_.push_back(meta);
    row_lookup_.emplace(shape.id, row_index_value);
    return view_mut(shape.id);
}

HUNLSampledConstRowView HUNLSampledStorage::view(InfosetId id) const {
    const auto index = row_index(id);
    if (index == meta_.size()) {
        return {};
    }

    const auto& meta = meta_[index];
    return {
        regret_.data() + meta.regret_offset,
        strategy_sum_.data() + meta.strategy_sum_offset,
        meta.bucket_count,
        meta.action_count,
    };
}

HUNLSampledRowView HUNLSampledStorage::view_mut(InfosetId id) {
    const auto index = row_index(id);
    if (index == meta_.size()) {
        return {};
    }

    const auto& meta = meta_[index];
    return {
        regret_.data() + meta.regret_offset,
        strategy_sum_.data() + meta.strategy_sum_offset,
        meta.bucket_count,
        meta.action_count,
    };
}

const std::vector<HUNLSampledInfosetMeta>& HUNLSampledStorage::meta() const noexcept {
    return meta_;
}

std::size_t HUNLSampledStorage::row_count() const noexcept {
    return meta_.size();
}

std::size_t HUNLSampledStorage::total_value_count() const noexcept {
    return regret_.size();
}

std::uint64_t HUNLSampledStorage::storage_bytes() const noexcept {
    return static_cast<std::uint64_t>(regret_.size() + strategy_sum_.size()) * sizeof(float);
}

HUNLFlatValueLayout HUNLSampledStorage::layout() const noexcept {
    return layout_;
}

HUNLFlatStoragePrecision HUNLSampledStorage::precision() const noexcept {
    return precision_;
}

void HUNLSampledStorage::clear_keep_capacity() noexcept {
    meta_.clear();
    row_lookup_.clear();
    regret_.clear();
    strategy_sum_.clear();
}

std::size_t HUNLSampledStorage::row_index(InfosetId id) const {
    const auto it = row_lookup_.find(id);
    return it == row_lookup_.end() ? meta_.size() : it->second;
}

}  // namespace core
