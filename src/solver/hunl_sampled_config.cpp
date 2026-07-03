#include "solver/hunl_sampled_config.hpp"

namespace core {

HUNLSampledConfigValidation validate_sampled_config(
    const HUNLSampledSolverConfig& config) noexcept {
    if (config.traversals_per_iteration == 0) {
        return {false, "traversals_per_iteration must be positive"};
    }
    if (config.minibatch_size == 0) {
        return {false, "minibatch_size must be positive"};
    }
    if (config.workers == 0) {
        return {false, "workers must be positive"};
    }
    if (config.as_epsilon < 0.0 || config.as_epsilon > 1.0) {
        return {false, "as_epsilon must be in [0, 1]"};
    }
    if (config.as_tau < 0.0) {
        return {false, "as_tau must be non-negative"};
    }
    if (config.as_beta < 0.0) {
        return {false, "as_beta must be non-negative"};
    }
    if (config.use_average_strategy_sampling &&
        config.mode != HUNLFlatSamplingMode::AverageStrategy) {
        return {false, "use_average_strategy_sampling requires AverageStrategy mode"};
    }
    return {};
}

void validate_sampled_config_or_throw(const HUNLSampledSolverConfig& config) {
    const auto validation = validate_sampled_config(config);
    if (!validation.ok) {
        throw std::invalid_argument(validation.message);
    }
}

}  // namespace core
