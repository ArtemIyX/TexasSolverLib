#pragma once

#include "core/legacy_namespace_compat.hpp"

#include "solver/hunl_sampled_storage.hpp"
#include "games/hunl.hpp"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace texas::solver::hunl {

struct HUNLSampledActionProbability {
    std::uint32_t action_index = 0;
    ActionId action_id = ACTION_FOLD;
    int target_contribution = 0;
    std::uint64_t action_menu_id = 0;
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
    static void attach_action_descriptors(
        HUNLSampledRootStrategy& strategy,
        const std::vector<ActionId>& actions,
        const std::vector<int>& target_contributions);
};

}  // namespace texas::solver::hunl
