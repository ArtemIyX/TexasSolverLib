#pragma once

#include "core/types.hpp"

#include <cstddef>
#include <string>
#include <tuple>
#include <unordered_map>
#include <vector>

namespace core {

inline constexpr std::size_t BLOCK_SIZE = 64;

struct RowMeta {
    std::uint32_t offset = 0;
    std::uint16_t num_actions = 0;
    std::uint32_t last_discount_iter = 0;
};

class FlatInfosetStore {
public:
    explicit FlatInfosetStore(std::size_t row_width);

    [[nodiscard]] std::size_t len() const noexcept;
    [[nodiscard]] bool is_empty() const noexcept;

    InfosetId intern(const std::string& key, std::size_t num_actions);

    [[nodiscard]] const std::vector<std::string>& id_to_key() const noexcept;
    [[nodiscard]] const std::vector<RowMeta>& meta() const noexcept;
    [[nodiscard]] std::size_t row_width() const noexcept;

    [[nodiscard]] const double* regret(InfosetId id) const;
    [[nodiscard]] double* regret_mut(InfosetId id);
    [[nodiscard]] const double* strategy_sum(InfosetId id) const;
    [[nodiscard]] double* strategy_sum_mut(InfosetId id);

    [[nodiscard]] std::size_t row_size(InfosetId id) const;
    [[nodiscard]] std::tuple<double*, double*, RowMeta*> row_mut(InfosetId id);

    [[nodiscard]] std::size_t regret_arena_size() const noexcept;
    [[nodiscard]] std::size_t strategy_arena_size() const noexcept;

private:
    static std::size_t arena_size_for(std::size_t required, std::size_t row_width);
    RowMeta& meta_for(InfosetId id);
    const RowMeta& meta_for(InfosetId id) const;

    std::unordered_map<std::string, InfosetId> key_to_id_;
    std::vector<std::string> id_to_key_;
    std::vector<RowMeta> meta_;
    std::vector<double> regret_arena_;
    std::vector<double> strategy_arena_;
    std::size_t row_width_ = 1;
};

}  // namespace core


