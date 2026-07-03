#pragma once

#include "games/hunl.hpp"
#include "core/types.hpp"
#include "solver/hunl_flat_state.hpp"

#include <cstddef>
#include <cstdint>
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
    std::uint32_t regret_offset = 0;
    std::uint32_t strategy_sum_offset = 0;
    std::uint32_t last_discount_iter = 0;

    [[nodiscard]] std::uint32_t value_count() const noexcept {
        return bucket_count * static_cast<std::uint32_t>(action_count);
    }
};

struct HUNLSampledConstRowView {
    const float* regret = nullptr;
    const float* strategy_sum = nullptr;
    std::uint32_t bucket_count = 0;
    std::uint8_t action_count = 0;

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

class HUNLSampledStorage {
public:
    explicit HUNLSampledStorage(
        HUNLFlatValueLayout layout = HUNLFlatValueLayout::InfosetActionHand,
        HUNLFlatStoragePrecision precision = HUNLFlatStoragePrecision::Float32);

    HUNLSampledRowView ensure_row(const HUNLSampledInfosetShape& shape);
    [[nodiscard]] HUNLSampledConstRowView view(InfosetId id) const;
    [[nodiscard]] HUNLSampledRowView view_mut(InfosetId id);
    [[nodiscard]] const std::vector<HUNLSampledInfosetMeta>& meta() const noexcept;
    [[nodiscard]] std::size_t row_count() const noexcept;
    [[nodiscard]] std::size_t total_value_count() const noexcept;
    [[nodiscard]] std::uint64_t storage_bytes() const noexcept;
    [[nodiscard]] HUNLFlatValueLayout layout() const noexcept;
    [[nodiscard]] HUNLFlatStoragePrecision precision() const noexcept;
    void clear_keep_capacity() noexcept;

private:
    [[nodiscard]] std::size_t row_index(InfosetId id) const;

    HUNLFlatValueLayout layout_ = HUNLFlatValueLayout::InfosetActionHand;
    HUNLFlatStoragePrecision precision_ = HUNLFlatStoragePrecision::Float32;
    std::vector<HUNLSampledInfosetMeta> meta_;
    std::unordered_map<InfosetId, std::size_t> row_lookup_;
    std::vector<float> regret_;
    std::vector<float> strategy_sum_;
};

}  // namespace core
