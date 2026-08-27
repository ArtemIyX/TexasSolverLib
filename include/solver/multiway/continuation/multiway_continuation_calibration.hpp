#pragma once

#include "solver/multiway/continuation/multiway_continuation_policy.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace texas::solver::multiway {

struct MultiwayContinuationCalibrationCase {
    std::array<double, MULTIWAY_FIXED_CONTINUATION_POLICIES.size()> policy_values{};
    double reference_value = 0.0;
    bool held_out = false;
};

struct MultiwayContinuationCalibrationConfig {
    MultiwayContinuationPolicyKind policy = MultiwayContinuationPolicyKind::Blueprint;
    std::uint32_t rollout_samples = 1U;
    std::uint32_t cache_limit = 0U;
    std::uint32_t street_boundary = 0U;
    double bias_factor = 5.0;
    std::uint64_t identity_version = 1U;
    void validate() const;
};

struct MultiwayContinuationCalibrationLimits {
    double maximum_held_out_error = 1.0;
    double maximum_bias = 1.0;
    double minimum_nested_improvement = 0.0;
    void validate() const;
};

struct MultiwayContinuationCalibrationResult {
    std::uint64_t identity = 0U;
    std::size_t held_out_samples = 0U;
    double held_out_error = 0.0;
    double bias = 0.0;
    double variance = 0.0;
    double nested_improvement = 0.0;
    bool within_limits = false;
};

[[nodiscard]] MultiwayContinuationCalibrationResult calibrate_multiway_continuation(
    const MultiwayContinuationCalibrationConfig& config,
    const std::vector<MultiwayContinuationCalibrationCase>& cases,
    const MultiwayContinuationCalibrationLimits& limits = {});

}  // namespace texas::solver::multiway
