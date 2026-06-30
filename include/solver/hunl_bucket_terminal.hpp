#pragma once

#include "games/hunl_eval.hpp"
#include "solver/hunl_bucket_map.hpp"

#include <cstddef>
#include <cstdint>
#include <unordered_map>
#include <vector>

namespace core {

struct HUNLBucketShowdownMatrix {
    std::uint32_t node_idx = 0;
    std::uint32_t bucket_count_p0 = 0;
    std::uint32_t bucket_count_p1 = 0;
    std::vector<double> values;
    std::vector<std::uint32_t> pair_counts;

    [[nodiscard]] double value(std::size_t bucket0, std::size_t bucket1) const;
    [[nodiscard]] std::uint32_t pair_count(std::size_t bucket0, std::size_t bucket1) const;
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

private:
    std::unordered_map<std::uint32_t, HUNLBucketShowdownMatrix> showdown_matrices_;
};

double heuristic_depth_limited_value_p0(
    const HUNLFlatNodeMeta& node,
    const HUNLConfig& config);

}  // namespace core
