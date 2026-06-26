#pragma once

#include "solver/dcfr_vector.hpp"

#include <cstddef>
#include <vector>

namespace core {

struct ParallelChanceRange {
    std::size_t start = 0;
    std::size_t end = 0;
    std::size_t root_idx = 0;
};

std::vector<ParallelChanceRange> derive_parallel_chance_ranges(
    const BettingTree& tree,
    const std::vector<std::size_t>& children);

bool parallel_chance_enabled();

}  // namespace core


