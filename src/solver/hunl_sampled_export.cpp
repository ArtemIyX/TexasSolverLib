#include "solver/hunl_sampled_export.hpp"

#include <stdexcept>

namespace core {

namespace {

std::uint64_t action_menu_id(const std::vector<ActionId>& actions, const std::vector<int>& targets) noexcept {
    std::uint64_t hash = 1469598103934665603ULL;
    for (std::size_t index = 0; index < actions.size(); ++index) {
        hash ^= static_cast<std::uint64_t>(actions[index]);
        hash *= 1099511628211ULL;
        hash ^= static_cast<std::uint64_t>(static_cast<std::uint32_t>(targets[index]));
        hash *= 1099511628211ULL;
    }
    return hash;
}

}  // namespace

HUNLSampledRootStrategy HUNLSampledStrategyExporter::export_uniform(std::uint8_t action_count) {
    HUNLSampledRootStrategy strategy;
    if (action_count == 0) {
        return strategy;
    }

    strategy.actions.reserve(action_count);
    const auto probability = 1.0 / static_cast<double>(action_count);
    for (std::uint32_t action_index = 0; action_index < action_count; ++action_index) {
        strategy.actions.push_back({action_index, ACTION_FOLD, 0, 0, probability});
    }
    return strategy;
}

HUNLSampledRootStrategy HUNLSampledStrategyExporter::export_average_strategy(
    HUNLSampledConstRowView row,
    std::size_t bucket_index) {
    HUNLSampledRootStrategy strategy;
    if (row.empty() || bucket_index >= row.bucket_count) {
        return strategy;
    }

    strategy.actions.reserve(row.action_count);
    double total = 0.0;
    for (std::uint32_t action_index = 0; action_index < row.action_count; ++action_index) {
        total += row.strategy_sum[HUNLSampledStorage::value_index(
            row.layout,
            row.bucket_count,
            row.action_count,
            bucket_index,
            action_index)];
    }

    const auto uniform = row.action_count == 0 ? 0.0 : 1.0 / static_cast<double>(row.action_count);
    for (std::uint32_t action_index = 0; action_index < row.action_count; ++action_index) {
        const auto value = row.strategy_sum[HUNLSampledStorage::value_index(
            row.layout,
            row.bucket_count,
            row.action_count,
            bucket_index,
            action_index)];
        strategy.actions.push_back({
            action_index,
            ACTION_FOLD,
            0,
            0,
            total > 0.0 ? static_cast<double>(value) / total : uniform,
        });
    }

    return strategy;
}

void HUNLSampledStrategyExporter::attach_action_descriptors(
    HUNLSampledRootStrategy& strategy,
    const std::vector<ActionId>& actions,
    const std::vector<int>& target_contributions) {
    if (strategy.actions.size() != actions.size() || actions.size() != target_contributions.size()) {
        throw std::invalid_argument("root action descriptor shape does not match strategy");
    }
    const auto menu_id = action_menu_id(actions, target_contributions);
    for (std::size_t index = 0; index < actions.size(); ++index) {
        strategy.actions[index].action_id = actions[index];
        strategy.actions[index].target_contribution = target_contributions[index];
        strategy.actions[index].action_menu_id = menu_id;
    }
}

}  // namespace core
