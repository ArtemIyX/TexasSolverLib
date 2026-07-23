#include "solver/hunl_sampled_config.hpp"

namespace core {

HUNLSampledConfigValidation validate_sampled_config(
    const HUNLSampledSolverConfig& config) noexcept {
    if (config.precision != HUNLFlatStoragePrecision::Float32) {
        return {false, "sampled storage currently supports Float32 precision only"};
    }
    if (config.minibatch_size == 0) {
        return {false, "minibatch_size must be positive"};
    }
    if (config.workers == 0) {
        return {false, "workers must be positive"};
    }
    if (config.depth_limit_plies_hint != 0U) {
        return {false, "sampled depth_limit_plies_hint requires an unavailable typed leaf evaluator"};
    }
    if (config.memory_warning_bytes == 0 || config.memory_fail_bytes == 0) {
        return {false, "memory guardrail thresholds must be positive"};
    }
    if (config.memory_warning_bytes > config.memory_fail_bytes) {
        return {false, "memory warning threshold must be <= memory fail threshold"};
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
