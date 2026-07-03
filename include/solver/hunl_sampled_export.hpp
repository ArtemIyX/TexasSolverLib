#pragma once

#include "solver/hunl_sampled_storage.hpp"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace core {

struct HUNLSampledActionProbability {
    std::uint32_t action_index = 0;
    double probability = 0.0;
};

struct HUNLSampledRootStrategy {
    std::vector<HUNLSampledActionProbability> actions;
};

class HUNLSampledStrategyExporter {
public:
    [[nodiscard]] static HUNLSampledRootStrategy export_uniform(std::uint8_t action_count);
    [[nodiscard]] static HUNLSampledRootStrategy export_average_strategy(
        HUNLSampledConstRowView row,
        std::size_t bucket_index = 0);
};

}  // namespace core
