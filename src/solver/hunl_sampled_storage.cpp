#include "solver/hunl_sampled_storage.hpp"

#include <algorithm>
#include <limits>
#include <stdexcept>

namespace core {

HUNLSampledStorage::HUNLSampledStorage(
    HUNLFlatValueLayout layout,
    HUNLFlatStoragePrecision precision)
    : layout_(layout), precision_(precision) {
    if (precision != HUNLFlatStoragePrecision::Float32) {
        throw std::invalid_argument("HUNLSampledStorage currently supports Float32 precision only");
    }
}

HUNLSampledRowView HUNLSampledStorage::ensure_row(const HUNLSampledInfosetShape& shape) {
    const auto it = row_lookup_.find(shape.id);
    if (it != row_lookup_.end()) {
        const auto& existing = meta_.at(it->second);
        if (existing.player != shape.player ||
            existing.street != shape.street ||
            existing.bucket_count != shape.bucket_count ||
            existing.action_count != shape.action_count) {
            throw std::invalid_argument("HUNLSampledStorage row shape mismatch for reused InfosetId");
        }
        return view_mut(shape.id);
    }

    HUNLSampledInfosetMeta meta;
    meta.id = shape.id;
    meta.player = shape.player;
    meta.street = shape.street;
    meta.bucket_count = shape.bucket_count;
    meta.action_count = shape.action_count;
    const auto value_count = meta.value_count();
    if (value_count > regret_.max_size() || value_count > strategy_sum_.max_size() ||
        regret_.size() > regret_.max_size() - value_count ||
        strategy_sum_.size() > strategy_sum_.max_size() - value_count) {
        throw std::length_error("HUNLSampledStorage row allocation exceeds vector capacity");
    }
    if (regret_.size() > std::numeric_limits<std::size_t>::max() - value_count ||
        strategy_sum_.size() > std::numeric_limits<std::size_t>::max() - value_count) {
        throw std::length_error("HUNLSampledStorage row offset overflow");
    }
    meta.regret_offset = regret_.size();
    meta.strategy_sum_offset = strategy_sum_.size();

    regret_.resize(regret_.size() + value_count, 0.0f);
    strategy_sum_.resize(strategy_sum_.size() + value_count, 0.0f);

    const auto row_index_value = meta_.size();
    meta_.push_back(meta);
    row_lookup_.emplace(shape.id, row_index_value);
    return view_mut(shape.id);
}

bool HUNLSampledStorage::has_row(InfosetId id) const noexcept {
    return row_lookup_.find(id) != row_lookup_.end();
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
        layout_,
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
        layout_,
    };
}

const std::vector<HUNLSampledInfosetMeta>& HUNLSampledStorage::meta() const noexcept {
    return meta_;
}

HUNLSampledInfosetMeta* HUNLSampledStorage::meta_for_mut(InfosetId id) noexcept {
    const auto index = row_index(id);
    return index == meta_.size() ? nullptr : &meta_[index];
}

const HUNLSampledInfosetMeta* HUNLSampledStorage::meta_for(InfosetId id) const noexcept {
    const auto index = row_index(id);
    return index == meta_.size() ? nullptr : &meta_[index];
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

HUNLSampledStorageMemoryEstimate HUNLSampledStorage::memory_estimate() const noexcept {
    HUNLSampledStorageMemoryEstimate estimate;
    estimate.meta_bytes =
        static_cast<std::uint64_t>(meta_.capacity()) * sizeof(HUNLSampledInfosetMeta);
    estimate.lookup_bytes =
        static_cast<std::uint64_t>(row_lookup_.size()) *
        static_cast<std::uint64_t>(sizeof(InfosetId) + sizeof(std::size_t) + sizeof(void*) * 2U);
    estimate.regret_bytes = static_cast<std::uint64_t>(regret_.capacity()) * sizeof(float);
    estimate.strategy_sum_bytes = static_cast<std::uint64_t>(strategy_sum_.capacity()) * sizeof(float);
    estimate.sparse_rows = static_cast<std::uint64_t>(meta_.size());
    estimate.sparse_values = static_cast<std::uint64_t>(regret_.size());
    return estimate;
}

HUNLFlatValueLayout HUNLSampledStorage::layout() const noexcept {
    return layout_;
}

HUNLFlatStoragePrecision HUNLSampledStorage::precision() const noexcept {
    return precision_;
}

std::size_t HUNLSampledStorage::value_index(
    HUNLFlatValueLayout layout,
    std::uint32_t bucket_count,
    std::uint8_t action_count,
    std::size_t bucket,
    std::size_t action) noexcept {
    const auto action_count_size = static_cast<std::size_t>(action_count);
    if (layout == HUNLFlatValueLayout::InfosetHandAction) {
        return bucket * action_count_size + action;
    }
    return action * static_cast<std::size_t>(bucket_count) + bucket;
}

void HUNLSampledStorage::compute_current_strategy(
    HUNLSampledConstRowView row,
    std::size_t bucket,
    float* out) noexcept {
    if (out == nullptr || row.action_count == 0) {
        return;
    }

    const auto action_count = static_cast<std::size_t>(row.action_count);
    if (row.empty() || bucket >= row.bucket_count) {
        const auto uniform = 1.0f / static_cast<float>(action_count);
        std::fill(out, out + action_count, uniform);
        return;
    }

    float positive_total = 0.0f;
    for (std::size_t action = 0; action < action_count; ++action) {
        const auto regret =
            row.regret[value_index(row.layout, row.bucket_count, row.action_count, bucket, action)];
        out[action] = std::max(regret, 0.0f);
        positive_total += out[action];
    }

    if (positive_total > 0.0f) {
        for (std::size_t action = 0; action < action_count; ++action) {
            out[action] /= positive_total;
        }
        return;
    }

    const auto uniform = 1.0f / static_cast<float>(action_count);
    std::fill(out, out + action_count, uniform);
}

std::uint64_t HUNLSampledStorage::estimate_row_storage_bytes(
    const HUNLSampledInfosetShape& shape) noexcept {
    const auto value_count =
        static_cast<std::uint64_t>(shape.bucket_count) * static_cast<std::uint64_t>(shape.action_count);
    return static_cast<std::uint64_t>(sizeof(HUNLSampledInfosetMeta)) +
        static_cast<std::uint64_t>(sizeof(InfosetId) + sizeof(std::size_t) + sizeof(void*) * 2U) +
        value_count * sizeof(float) * 2ULL;
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
