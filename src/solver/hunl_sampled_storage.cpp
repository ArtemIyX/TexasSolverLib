#include "solver/hunl_sampled_storage.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace texas::solver::hunl {

namespace {

constexpr std::uint64_t kContainerGrowthSafetyFactor = 2U;

std::uint64_t saturating_add(std::uint64_t lhs, std::uint64_t rhs) noexcept {
    return lhs > std::numeric_limits<std::uint64_t>::max() - rhs
        ? std::numeric_limits<std::uint64_t>::max()
        : lhs + rhs;
}

std::uint64_t saturating_multiply(std::uint64_t lhs, std::uint64_t rhs) noexcept {
    if (lhs == 0U || rhs == 0U) return 0U;
    return lhs > std::numeric_limits<std::uint64_t>::max() / rhs
        ? std::numeric_limits<std::uint64_t>::max()
        : lhs * rhs;
}

bool valid_sampled_layout(HUNLFlatValueLayout layout) noexcept {
    return layout == HUNLFlatValueLayout::InfosetHandAction ||
           layout == HUNLFlatValueLayout::InfosetActionHand;
}

bool valid_infoset_street(Street street) noexcept {
    return street == Street::Preflop ||
           street == Street::Flop ||
           street == Street::Turn ||
           street == Street::River;
}

void validate_row_shape(const HUNLSampledInfosetShape& shape) {
    if (shape.player < 0 ||
        shape.player > 1 ||
        !valid_infoset_street(shape.street) ||
        shape.bucket_count == 0U ||
        shape.bucket_count > HUNL_SAMPLED_MAX_BUCKET_COUNT ||
        shape.action_count == 0U ||
        shape.action_count > HUNL_SAMPLED_MAX_ACTION_COUNT) {
        throw std::invalid_argument("HUNLSampledStorage received an invalid row shape");
    }
}

template <class T>
std::uint64_t vector_growth_peak_bytes(
    const std::vector<T>& values,
    std::size_t required_size) noexcept {
    if (required_size <= values.capacity()) return 0U;
    return saturating_multiply(
        saturating_multiply(static_cast<std::uint64_t>(required_size), sizeof(T)),
        kContainerGrowthSafetyFactor);
}

template <class Map>
std::uint64_t map_growth_peak_bytes(const Map& values, std::size_t required_size) noexcept {
    const auto entry_bytes = saturating_multiply(
        static_cast<std::uint64_t>(sizeof(typename Map::value_type) + sizeof(void*) * 2U),
        kContainerGrowthSafetyFactor);
    std::uint64_t bucket_bytes = 0U;
    const auto threshold = static_cast<double>(values.bucket_count()) * values.max_load_factor();
    if (static_cast<double>(required_size) > threshold) {
        const auto required_buckets = static_cast<std::uint64_t>(std::ceil(
            static_cast<double>(required_size) / values.max_load_factor()));
        bucket_bytes = saturating_multiply(
            saturating_multiply(required_buckets, sizeof(void*)),
            kContainerGrowthSafetyFactor);
    }
    return saturating_add(entry_bytes, bucket_bytes);
}

}  // namespace

HUNLSampledStorage::HUNLSampledStorage(
    HUNLFlatValueLayout layout,
    HUNLFlatStoragePrecision precision)
    : layout_(layout), precision_(precision) {
    if (!valid_sampled_layout(layout)) {
        throw std::invalid_argument("HUNLSampledStorage received an invalid value layout");
    }
    if (precision != HUNLFlatStoragePrecision::Float32) {
        throw std::invalid_argument("HUNLSampledStorage currently supports Float32 precision only");
    }
}

HUNLSampledRowView HUNLSampledStorage::ensure_row(const HUNLSampledInfosetShape& shape) {
    validate_row_shape(shape);
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
    const auto required_regret_size = regret_.size() + value_count;
    const auto required_strategy_size = strategy_sum_.size() + value_count;
    const auto required_meta_size = meta_.size() + 1U;
    const auto required_lookup_size = row_lookup_.size() + 1U;
    if (memory_limit_bytes_ != 0U) {
        const auto current = memory_estimate().total_bytes();
        auto peak = current;
        peak = saturating_add(peak, vector_growth_peak_bytes(regret_, required_regret_size));
        peak = saturating_add(peak, vector_growth_peak_bytes(strategy_sum_, required_strategy_size));
        peak = saturating_add(peak, vector_growth_peak_bytes(meta_, required_meta_size));
        peak = saturating_add(peak, map_growth_peak_bytes(row_lookup_, required_lookup_size));
        peak = saturating_add(peak, estimate_row_storage_bytes(shape));
        if (peak > memory_limit_bytes_) {
            throw std::runtime_error("sampled infoset row admission would exceed the configured memory limit");
        }
    }
    meta.regret_offset = regret_.size();
    meta.strategy_sum_offset = strategy_sum_.size();

    // Capacity growth is completed before logical mutation. If a later
    // reserve fails, the row remains absent and every existing row is intact.
    regret_.reserve(required_regret_size);
    strategy_sum_.reserve(required_strategy_size);
    meta_.reserve(required_meta_size);
    row_lookup_.reserve(required_lookup_size);
    if (memory_limit_bytes_ != 0U &&
        memory_estimate().total_bytes() > memory_limit_bytes_) {
        throw std::runtime_error("sampled infoset retained capacity exceeded the configured memory limit");
    }

    const auto old_regret_size = regret_.size();
    const auto old_strategy_size = strategy_sum_.size();
    const auto old_meta_size = meta_.size();
    const auto row_index_value = meta_.size();
    try {
        regret_.resize(required_regret_size, 0.0f);
        strategy_sum_.resize(required_strategy_size, 0.0f);
        meta_.push_back(meta);
        const auto inserted = row_lookup_.emplace(shape.id, row_index_value);
        if (!inserted.second) {
            throw std::logic_error("HUNLSampledStorage duplicate row insertion");
        }
        if (memory_limit_bytes_ != 0U &&
            memory_estimate().total_bytes() > memory_limit_bytes_) {
            throw std::runtime_error("sampled infoset row exceeded the configured retained-memory limit");
        }
    } catch (...) {
        regret_.resize(old_regret_size);
        strategy_sum_.resize(old_strategy_size);
        meta_.resize(old_meta_size);
        row_lookup_.erase(shape.id);
        throw;
    }
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
            static_cast<std::uint64_t>(sizeof(InfosetId) + sizeof(std::size_t) + sizeof(void*) * 2U) +
        static_cast<std::uint64_t>(row_lookup_.bucket_count()) * sizeof(void*);
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

void HUNLSampledStorage::set_memory_limit_bytes(std::uint64_t limit) noexcept {
    memory_limit_bytes_ = limit;
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

}  // namespace texas::solver::hunl
