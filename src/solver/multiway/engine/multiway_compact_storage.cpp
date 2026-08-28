#include "solver/multiway/engine/multiway_compact_storage.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace texas::solver::multiway {

namespace {
std::size_t cell(const MultiwaySparseRowMetadata& row, std::uint32_t bucket,
    std::uint8_t action) noexcept {
    return row.regret_offset + static_cast<std::size_t>(action) * row.shape.bucket_count + bucket;
}
}

MultiwayCompactStorage::MultiwayCompactStorage(std::size_t max_rows, std::size_t max_values)
    : max_rows_(max_rows), max_values_(max_values) {}

const MultiwaySparseRowMetadata* MultiwayCompactStorage::metadata(MultiwayInfosetId infoset) const noexcept {
    const auto it = std::lower_bound(rows_.begin(), rows_.end(), infoset,
        [](const auto& row, MultiwayInfosetId key) { return row.shape.infoset < key; });
    return it == rows_.end() || !(it->shape.infoset == infoset) ? nullptr : &*it;
}

bool MultiwayCompactStorage::has_row(MultiwayInfosetId infoset) const noexcept {
    return metadata(infoset) != nullptr;
}

void MultiwayCompactStorage::admit_row(const MultiwaySparseRowShape& shape) {
    if (shape.bucket_count == 0U || shape.action_count == 0U) {
        throw std::invalid_argument("compact storage row shape is empty");
    }
    if (const auto* existing = metadata(shape.infoset)) {
        if (existing->shape.bucket_count != shape.bucket_count || existing->shape.action_count != shape.action_count) {
            throw std::invalid_argument("compact storage row shape conflicts");
        }
        return;
    }
    const auto values = static_cast<std::size_t>(shape.bucket_count) * shape.action_count;
    if (rows_.size() >= max_rows_ || values > max_values_ - regrets_.size()) {
        throw std::length_error("compact storage capacity exceeded");
    }
    MultiwaySparseRowMetadata row{shape, regrets_.size(), strategy_mass_.size()};
    const auto it = std::lower_bound(rows_.begin(), rows_.end(), shape.infoset,
        [](const auto& existing, MultiwayInfosetId key) { return existing.shape.infoset < key; });
    regrets_.resize(regrets_.size() + values, 0);
    strategy_mass_.resize(strategy_mass_.size() + values, 0);
    rows_.insert(it, row);
}

void MultiwayCompactStorage::apply_delta(MultiwayInfosetId infoset, std::uint32_t bucket,
    std::uint8_t action, double regret, double strategy_sum) {
    const auto* row = metadata(infoset);
    if (row == nullptr || bucket >= row->shape.bucket_count || action >= row->shape.action_count ||
        !std::isfinite(regret) || !std::isfinite(strategy_sum) || strategy_sum < 0.0) {
        throw std::invalid_argument("invalid compact storage delta");
    }
    const auto index = cell(*row, bucket, action);
    const auto regret_delta = std::llround(regret * 1024.0);
    const auto old_regret = regrets_[index];
    const auto next_regret = std::clamp<long long>(static_cast<long long>(old_regret) + regret_delta,
        kRegretMin, kRegretMax);
    regrets_[index] = static_cast<std::int32_t>(next_regret);
    const auto mass_delta = std::llround(strategy_sum * static_cast<double>(kMassScale));
    if (mass_delta > 0 && strategy_mass_[row->strategy_sum_offset + index - row->regret_offset] >
        std::numeric_limits<std::uint64_t>::max() - static_cast<std::uint64_t>(mass_delta)) {
        throw std::overflow_error("compact strategy mass overflow");
    }
    strategy_mass_[row->strategy_sum_offset + index - row->regret_offset] += static_cast<std::uint64_t>(std::max(0LL, mass_delta));
}

void MultiwayCompactStorage::scale_regrets(double factor) {
    if (!std::isfinite(factor) || factor <= 0.0 || factor > 1.0) {
        throw std::invalid_argument("compact regret discount must be finite and in (0, 1]");
    }
    for (auto& regret : regrets_) {
        regret = static_cast<std::int32_t>(std::clamp<long long>(
            std::llround(static_cast<double>(regret) * factor), kRegretMin, kRegretMax));
    }
}

std::size_t MultiwayCompactStorage::prune_negative_regrets(
    double threshold, double regret_floor) noexcept {
    const auto floor_value = std::clamp<long long>(std::llround(regret_floor * 1024.0),
        kRegretMin, kRegretMax);
    const auto threshold_value = std::clamp<long long>(std::llround(threshold * 1024.0),
        kRegretMin, kRegretMax);
    std::size_t count = 0U;
    for (auto& regret : regrets_) {
        if (regret < threshold_value) {
            regret = static_cast<std::int32_t>(std::max(floor_value, threshold_value));
            ++count;
        }
    }
    return count;
}

std::vector<Probability> MultiwayCompactStorage::regret_matched_strategy(MultiwayInfosetId infoset, std::uint32_t bucket) const {
    const auto* row = metadata(infoset);
    if (row == nullptr || bucket >= row->shape.bucket_count) throw std::out_of_range("compact storage lookup");
    std::vector<Probability> result(row->shape.action_count);
    Probability total = 0.0;
    for (std::uint8_t action = 0; action < row->shape.action_count; ++action) {
        result[action] = std::max(0.0, static_cast<double>(regrets_[cell(*row, bucket, action)]) / 1024.0);
        total += result[action];
    }
    if (total == 0.0) std::fill(result.begin(), result.end(), 1.0 / result.size());
    else for (auto& value : result) value /= total;
    return result;
}

std::vector<Probability> MultiwayCompactStorage::average_strategy(MultiwayInfosetId infoset, std::uint32_t bucket) const {
    const auto* row = metadata(infoset);
    if (row == nullptr || bucket >= row->shape.bucket_count) throw std::out_of_range("compact storage lookup");
    std::vector<Probability> result(row->shape.action_count);
    std::uint64_t total = 0;
    for (std::uint8_t action = 0; action < row->shape.action_count; ++action) total += strategy_mass_[row->strategy_sum_offset + static_cast<std::size_t>(action) * row->shape.bucket_count + bucket];
    if (total == 0U) std::fill(result.begin(), result.end(), 1.0 / result.size());
    else for (std::uint8_t action = 0; action < row->shape.action_count; ++action) result[action] = static_cast<double>(strategy_mass_[row->strategy_sum_offset + static_cast<std::size_t>(action) * row->shape.bucket_count + bucket]) / total;
    return result;
}

bool MultiwayCompactStorage::action_below_regret(
    MultiwayInfosetId infoset, std::uint32_t bucket, std::uint8_t action,
    double threshold) const noexcept {
    const auto* row = metadata(infoset);
    if (row == nullptr || bucket >= row->shape.bucket_count || action >= row->shape.action_count ||
        !std::isfinite(threshold)) return false;
    return static_cast<double>(regrets_[row->regret_offset +
        static_cast<std::size_t>(action) * row->shape.bucket_count + bucket]) / 1024.0 < threshold;
}

std::vector<double> MultiwayCompactStorage::strategy_sums(
    MultiwayInfosetId infoset, std::uint32_t bucket) const {
    const auto* row = metadata(infoset);
    if (row == nullptr || bucket >= row->shape.bucket_count) throw std::out_of_range("compact storage lookup");
    std::vector<double> result(row->shape.action_count);
    for (std::uint8_t action = 0; action < row->shape.action_count; ++action) {
        result[action] = static_cast<double>(strategy_mass_[row->strategy_sum_offset +
            static_cast<std::size_t>(action) * row->shape.bucket_count + bucket]) /
            static_cast<double>(kMassScale);
    }
    return result;
}

void MultiwayCompactStorage::export_checkpoint(MultiwaySparseStorageCheckpoint& output) const {
    output.shapes.clear();
    output.regrets.clear();
    output.strategy_sums.clear();
    output.shapes.reserve(rows_.size());
    output.regrets.reserve(regrets_.size());
    output.strategy_sums.reserve(strategy_mass_.size());
    for (const auto& row : rows_) {
        output.shapes.push_back(row.shape);
        for (std::size_t offset = 0; offset < row.value_count(); ++offset) {
            output.regrets.push_back(static_cast<double>(regrets_[row.regret_offset + offset]) / 1024.0);
            output.strategy_sums.push_back(static_cast<double>(strategy_mass_[row.strategy_sum_offset + offset]) /
                static_cast<double>(kMassScale));
        }
    }
}

std::size_t MultiwayCompactStorage::memory_bytes() const noexcept {
    return rows_.capacity() * sizeof(MultiwaySparseRowMetadata) + regrets_.capacity() * sizeof(std::int32_t) + strategy_mass_.capacity() * sizeof(std::uint64_t);
}

}  // namespace texas::solver::multiway
