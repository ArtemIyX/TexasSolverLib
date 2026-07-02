#pragma once

#include "games/hunl_eval.hpp"
#include "solver/hunl_bucket_map.hpp"

#include <cstddef>
#include <cstdint>
#include <unordered_map>
#include <vector>

namespace core {

struct HUNLBucketShowdownMatrix {
    std::uint32_t bucket_count_p0 = 0;
    std::uint32_t bucket_count_p1 = 0;
    std::vector<std::uint32_t> valid_pair_counts;
    std::vector<std::int32_t> net_win_counts;
    std::vector<std::uint32_t> tie_pair_counts;
    std::vector<std::uint32_t> bucket_hand_counts_p0;
    std::vector<std::uint32_t> bucket_hand_counts_p1;

    [[nodiscard]] std::uint32_t valid_pair_count(std::size_t bucket0, std::size_t bucket1) const;
    [[nodiscard]] std::int32_t net_win_count(std::size_t bucket0, std::size_t bucket1) const;
    [[nodiscard]] std::uint32_t tie_pair_count(std::size_t bucket0, std::size_t bucket1) const;
    [[nodiscard]] std::uint64_t estimated_bytes() const noexcept;
};

struct HUNLBucketTerminalBinding {
    std::uint32_t cache_index = 0;
    std::array<int, 2> contributions = {0, 0};
};

struct HUNLBucketTerminalCacheKey {
    HUNLFlatPackedBoard board{};
    std::uint32_t bucket_count_p0 = 0;
    std::uint32_t bucket_count_p1 = 0;

    bool operator==(const HUNLBucketTerminalCacheKey& other) const noexcept;
};

class HUNLBucketTerminalTable {
public:
    static HUNLBucketTerminalTable build(
        const HUNLFlatSolveGraph& graph,
        const HUNLFlatBucketMap& bucket_map);

    [[nodiscard]] bool has_showdown_matrix(std::uint32_t node_idx) const noexcept;
    [[nodiscard]] const HUNLBucketShowdownMatrix& showdown_matrix(std::uint32_t node_idx) const;
    [[nodiscard]] double expected_showdown_value(
        std::uint32_t node_idx,
        const std::vector<double>* p0_bucket_weights = nullptr,
        const std::vector<double>* p1_bucket_weights = nullptr) const;
    [[nodiscard]] std::uint64_t estimated_bytes() const noexcept;

private:
    std::vector<HUNLBucketShowdownMatrix> showdown_cache_;
    std::unordered_map<std::uint32_t, HUNLBucketTerminalBinding> node_bindings_;
    std::array<int, 2> initial_contributions_ = {0, 0};
    int initial_pot_ = 0;
    int big_blind_ = 100;
};

double heuristic_depth_limited_value_p0(
    const HUNLFlatNodeMeta& node,
    const HUNLConfig& config);

}  // namespace core

namespace std {

template <>
struct hash<core::HUNLBucketTerminalCacheKey> {
    std::size_t operator()(const core::HUNLBucketTerminalCacheKey& key) const noexcept;
};

}  // namespace std
