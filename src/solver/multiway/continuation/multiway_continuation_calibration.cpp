#include "solver/multiway/continuation/multiway_continuation_calibration.hpp"

#include <cmath>
#include <stdexcept>

namespace texas::solver::multiway {

void MultiwayContinuationCalibrationConfig::validate() const {
    if (!is_valid_multiway_continuation_policy(policy) || rollout_samples == 0U ||
        bias_factor <= 0.0 || !std::isfinite(bias_factor) || identity_version == 0U) {
        throw std::invalid_argument("continuation calibration config is invalid");
    }
}

void MultiwayContinuationCalibrationLimits::validate() const {
    if (!std::isfinite(maximum_held_out_error) || maximum_held_out_error < 0.0 ||
        !std::isfinite(maximum_bias) || maximum_bias < 0.0 ||
        !std::isfinite(minimum_nested_improvement)) {
        throw std::invalid_argument("continuation calibration limits are invalid");
    }
}

MultiwayContinuationCalibrationResult calibrate_multiway_continuation(
    const MultiwayContinuationCalibrationConfig& config,
    const std::vector<MultiwayContinuationCalibrationCase>& cases,
    const MultiwayContinuationCalibrationLimits& limits) {
    config.validate(); limits.validate();
    if (cases.empty()) throw std::invalid_argument("continuation calibration requires cases");
    for (const auto& calibration_case : cases) {
        if (!std::isfinite(calibration_case.reference_value)) {
            throw std::invalid_argument("continuation calibration reference is not finite");
        }
        for (const auto value : calibration_case.policy_values) {
            if (!std::isfinite(value)) throw std::invalid_argument("continuation calibration value is not finite");
        }
    }
    MultiwayContinuationCalibrationResult result;
    result.identity = config.identity_version ^ (static_cast<std::uint64_t>(config.rollout_samples) << 16U) ^
        (static_cast<std::uint64_t>(config.cache_limit) << 32U) ^ (static_cast<std::uint64_t>(config.street_boundary) << 48U) ^
        static_cast<std::uint64_t>(config.policy);
    double error_sum = 0.0; double baseline_error_sum = 0.0; double signed_sum = 0.0; double squared_sum = 0.0;
    for (const auto& calibration_case : cases) {
        if (!calibration_case.held_out) continue;
        double estimate = calibration_case.policy_values[0];
        if (config.policy != MultiwayContinuationPolicyKind::Blueprint) {
            const auto index = static_cast<std::size_t>(config.policy);
            estimate = calibration_case.policy_values[index];
        }
        const auto error = estimate - calibration_case.reference_value;
        baseline_error_sum += std::abs(calibration_case.policy_values[0] - calibration_case.reference_value);
        error_sum += std::abs(error); signed_sum += error; squared_sum += error * error;
        ++result.held_out_samples;
    }
    if (result.held_out_samples != 0U) {
        result.held_out_error = error_sum / result.held_out_samples;
        result.bias = std::abs(signed_sum / result.held_out_samples);
        result.variance = std::max(0.0, squared_sum / result.held_out_samples -
            (signed_sum / result.held_out_samples) * (signed_sum / result.held_out_samples));
    }
    result.nested_improvement = result.held_out_samples == 0U ? 0.0 :
        baseline_error_sum / result.held_out_samples - result.held_out_error;
    result.within_limits = result.held_out_error <= limits.maximum_held_out_error &&
        result.bias <= limits.maximum_bias && result.nested_improvement >= limits.minimum_nested_improvement;
    return result;
}
}  // namespace texas::solver::multiway
