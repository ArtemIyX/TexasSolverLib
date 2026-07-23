#pragma once

#include "games/hunl.hpp"
#include "core/types.hpp"
#include "solver/hunl_flat_state.hpp"

#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <limits>
#include <type_traits>
#include <unordered_map>
#include <vector>

namespace core {

struct HUNLSampledInfosetShape {
    InfosetId id{};
    PlayerId player = -1;
    Street street = Street::Preflop;
    std::uint32_t bucket_count = 0;
    std::uint8_t action_count = 0;
};

struct HUNLSampledInfosetMeta {
    InfosetId id{};
    PlayerId player = -1;
    Street street = Street::Preflop;
    std::uint32_t bucket_count = 0;
    std::uint8_t action_count = 0;
    std::size_t regret_offset = 0;
    std::size_t strategy_sum_offset = 0;
    std::uint32_t last_discount_iter = 0;

    [[nodiscard]] std::size_t value_count() const noexcept {
        const auto actions = static_cast<std::size_t>(action_count);
        if (actions != 0 &&
            static_cast<std::size_t>(bucket_count) > std::numeric_limits<std::size_t>::max() / actions) {
            return std::numeric_limits<std::size_t>::max();
        }
        return static_cast<std::size_t>(bucket_count) * actions;
    }
};

struct HUNLSampledConstRowView {
    const float* regret = nullptr;
    const float* strategy_sum = nullptr;
    std::uint32_t bucket_count = 0;
    std::uint8_t action_count = 0;
    HUNLFlatValueLayout layout = HUNLFlatValueLayout::InfosetActionHand;

    [[nodiscard]] std::size_t value_count() const noexcept {
        return static_cast<std::size_t>(bucket_count) * static_cast<std::size_t>(action_count);
    }

    [[nodiscard]] bool empty() const noexcept {
        return regret == nullptr || strategy_sum == nullptr || value_count() == 0;
    }
};

struct HUNLSampledRowView {
    float* regret = nullptr;
    float* strategy_sum = nullptr;
    std::uint32_t bucket_count = 0;
    std::uint8_t action_count = 0;
    HUNLFlatValueLayout layout = HUNLFlatValueLayout::InfosetActionHand;

    [[nodiscard]] std::size_t value_count() const noexcept {
        return static_cast<std::size_t>(bucket_count) * static_cast<std::size_t>(action_count);
    }

    [[nodiscard]] bool empty() const noexcept {
        return regret == nullptr || strategy_sum == nullptr || value_count() == 0;
    }
};

static_assert(
    std::is_trivially_copyable_v<HUNLSampledInfosetMeta>,
    "HUNLSampledInfosetMeta should stay trivially copyable");

struct HUNLSampledStorageMemoryEstimate {
    std::uint64_t meta_bytes = 0;
    std::uint64_t lookup_bytes = 0;
    std::uint64_t regret_bytes = 0;
    std::uint64_t strategy_sum_bytes = 0;
    std::uint64_t sparse_rows = 0;
    std::uint64_t sparse_values = 0;

    [[nodiscard]] std::uint64_t total_bytes() const noexcept {
        std::uint64_t total = 0;
        for (const auto value : {meta_bytes, lookup_bytes, regret_bytes, strategy_sum_bytes}) {
            if (value > std::numeric_limits<std::uint64_t>::max() - total) {
                return std::numeric_limits<std::uint64_t>::max();
            }
            total += value;
        }
        return total;
    }
};

class HUNLSampledStorage {
public:
    explicit HUNLSampledStorage(
        HUNLFlatValueLayout layout = HUNLFlatValueLayout::InfosetActionHand,
        HUNLFlatStoragePrecision precision = HUNLFlatStoragePrecision::Float32);

    // Returned raw-pointer views remain valid until the next operation that
    // grows storage; reacquire the view after adding a new row.
    HUNLSampledRowView ensure_row(const HUNLSampledInfosetShape& shape);
    [[nodiscard]] bool has_row(InfosetId id) const noexcept;
    [[nodiscard]] HUNLSampledConstRowView view(InfosetId id) const;
    [[nodiscard]] HUNLSampledRowView view_mut(InfosetId id);
    [[nodiscard]] const std::vector<HUNLSampledInfosetMeta>& meta() const noexcept;
    [[nodiscard]] HUNLSampledInfosetMeta* meta_for_mut(InfosetId id) noexcept;
    [[nodiscard]] const HUNLSampledInfosetMeta* meta_for(InfosetId id) const noexcept;
    [[nodiscard]] std::size_t row_count() const noexcept;
    [[nodiscard]] std::size_t total_value_count() const noexcept;
    [[nodiscard]] std::uint64_t storage_bytes() const noexcept;
    [[nodiscard]] HUNLSampledStorageMemoryEstimate memory_estimate() const noexcept;
    [[nodiscard]] HUNLFlatValueLayout layout() const noexcept;
    [[nodiscard]] HUNLFlatStoragePrecision precision() const noexcept;
    [[nodiscard]] static std::size_t value_index(
        HUNLFlatValueLayout layout,
        std::uint32_t bucket_count,
        std::uint8_t action_count,
        std::size_t bucket,
        std::size_t action) noexcept;
    static void compute_current_strategy(
        HUNLSampledConstRowView row,
        std::size_t bucket,
        float* out) noexcept;
    [[nodiscard]] static std::uint64_t estimate_row_storage_bytes(
        const HUNLSampledInfosetShape& shape) noexcept;
    void set_memory_limit_bytes(std::uint64_t limit) noexcept;
    void clear_keep_capacity() noexcept;

private:
    [[nodiscard]] std::size_t row_index(InfosetId id) const;

    HUNLFlatValueLayout layout_ = HUNLFlatValueLayout::InfosetActionHand;
    HUNLFlatStoragePrecision precision_ = HUNLFlatStoragePrecision::Float32;
    std::vector<HUNLSampledInfosetMeta> meta_;
    std::unordered_map<InfosetId, std::size_t> row_lookup_;
    std::vector<float> regret_;
    std::vector<float> strategy_sum_;
    std::uint64_t memory_limit_bytes_ = 0;
};

}  // namespace core
