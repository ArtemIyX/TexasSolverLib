#include "solver/hunl_sampled_config.hpp"
#include "solver/hunl_sampled_storage.hpp"

namespace core {

HUNLSampledConfigValidation validate_sampled_config(
    const HUNLSampledSolverConfig& config) noexcept {
    if (config.layout != HUNLFlatValueLayout::InfosetHandAction &&
        config.layout != HUNLFlatValueLayout::InfosetActionHand) {
        return {false, "sampled storage layout is invalid"};
    }
    if (config.precision != HUNLFlatStoragePrecision::Float32) {
        return {false, "sampled storage currently supports Float32 precision only"};
    }
    if (config.minibatch_size == 0) {
        return {false, "minibatch_size must be positive"};
    }
    if (config.workers == 0) {
        return {false, "workers must be positive"};
    }
    if (config.bucket_count_hint > HUNL_SAMPLED_MAX_BUCKET_COUNT) {
        return {false, "bucket_count_hint exceeds the sampled row limit"};
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
