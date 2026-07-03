#include "solver/hunl_sampled_export.hpp"

namespace core {

HUNLSampledRootStrategy HUNLSampledStrategyExporter::export_uniform(std::uint8_t action_count) {
    HUNLSampledRootStrategy strategy;
    if (action_count == 0) {
        return strategy;
    }

    strategy.actions.reserve(action_count);
    const auto probability = 1.0 / static_cast<double>(action_count);
    for (std::uint32_t action_index = 0; action_index < action_count; ++action_index) {
        strategy.actions.push_back({action_index, probability});
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
        total += row.strategy_sum[action_index * row.bucket_count + bucket_index];
    }

    const auto uniform = row.action_count == 0 ? 0.0 : 1.0 / static_cast<double>(row.action_count);
    for (std::uint32_t action_index = 0; action_index < row.action_count; ++action_index) {
        const auto value = row.strategy_sum[action_index * row.bucket_count + bucket_index];
        strategy.actions.push_back({
            action_index,
            total > 0.0 ? static_cast<double>(value) / total : uniform,
        });
    }

    return strategy;
}

}  // namespace core
