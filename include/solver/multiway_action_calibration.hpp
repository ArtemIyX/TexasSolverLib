#pragma once

#include "solver/multiway_action_abstraction.hpp"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace texas::solver::multiway {

struct MultiwayEvaluationResult;

struct MultiwayActionCalibrationCase {
    MultiwayBettingSnapshot betting{};
    MultiwayAction observed_action = MultiwayAction::Check;
    int observed_target = 0;
    MultiwayActionAbstractionContext context{};
};

struct MultiwayActionCalibrationLimits {
    std::size_t maximum_actions = MULTIWAY_MAX_ABSTRACTED_ACTIONS;
    double maximum_translation_rejection_rate = 1.0;
    double maximum_artifact_miss_rate = 1.0;
    std::uint64_t maximum_estimated_menu_bytes = 1U << 20U;
    void validate() const;
};

using MultiwayActionCalibrationArtifactCoverageFn = bool (*) (
    const MultiwayActionCalibrationCase& calibration_case,
    const std::vector<MultiwayActionDescriptor>& menu,
    const void* context) noexcept;

struct MultiwayActionCalibrationResult {
    std::uint64_t profile_identity = 0U;
    std::uint64_t translation_policy_identity = 0U;
    std::size_t cases = 0U;
    std::size_t total_actions = 0U;
    std::size_t max_actions = 0U;
    std::size_t exact_translations = 0U;
    std::size_t translated_actions = 0U;
    std::size_t rejected_translations = 0U;
    std::size_t expanded_actions = 0U;
    std::uint64_t estimated_menu_bytes = 0U;
    std::size_t artifact_coverage_cases = 0U;
    std::size_t artifact_miss_cases = 0U;
    bool within_limits = false;

    [[nodiscard]] double mean_branching_factor() const noexcept {
        return cases == 0U ? 0.0 : static_cast<double>(total_actions) / static_cast<double>(cases);
    }
    [[nodiscard]] double translation_rejection_rate() const noexcept {
        return cases == 0U ? 0.0 : static_cast<double>(rejected_translations) / static_cast<double>(cases);
    }
    [[nodiscard]] double artifact_miss_rate() const noexcept {
        return cases == 0U ? 0.0 : static_cast<double>(artifact_miss_cases) / static_cast<double>(cases);
    }
};

using MultiwayActionCalibrationValueFn = double (*) (
    const MultiwayActionCalibrationResult& result,
    const void* context) noexcept;

struct MultiwayActionCalibrationQuality {
    double value = 0.0;
    double standard_error = 0.0;
};

// Nash-convergence is a lower-is-better metric; calibration selection uses
// higher-is-better quality, so this adapter negates its estimate.
[[nodiscard]] MultiwayActionCalibrationQuality multiway_action_quality_from_evaluation(
    const MultiwayEvaluationResult& evaluation) noexcept;
using MultiwayActionCalibrationQualityFn = MultiwayActionCalibrationQuality (*) (
    const MultiwayActionCalibrationResult& result,
    const void* context) noexcept;

struct MultiwayActionCalibrationSelection {
    MultiwayActionAbstractionConfig abstraction{};
    MultiwayActionCalibrationResult result{};
    double baseline_value = 0.0;
    double candidate_value = 0.0;
    bool selected = false;
};

[[nodiscard]] MultiwayActionCalibrationResult calibrate_multiway_action_abstraction(
    const std::vector<MultiwayActionCalibrationCase>& cases,
    MultiwayActionAbstractionConfig abstraction = {},
    MultiwayDeviationExpansionConfig expansion = {},
    MultiwayActionCalibrationLimits limits = {},
    MultiwayActionCalibrationArtifactCoverageFn artifact_coverage_fn = nullptr,
    const void* artifact_coverage_context = nullptr);

[[nodiscard]] MultiwayActionCalibrationSelection select_multiway_action_profile(
    const std::vector<MultiwayActionAbstractionConfig>& candidates,
    const std::vector<MultiwayActionCalibrationCase>& cases,
    double frozen_baseline_value,
    MultiwayActionCalibrationValueFn value_fn,
    const void* context = nullptr,
    MultiwayDeviationExpansionConfig expansion = {},
    MultiwayActionCalibrationLimits limits = {});

[[nodiscard]] MultiwayActionCalibrationSelection select_multiway_action_profile_statistically(
    const std::vector<MultiwayActionAbstractionConfig>& candidates,
    const std::vector<MultiwayActionCalibrationCase>& cases,
    MultiwayActionCalibrationQuality frozen_baseline,
    MultiwayActionCalibrationQualityFn quality_fn,
    const void* context = nullptr,
    double confidence_level = 0.95,
    MultiwayDeviationExpansionConfig expansion = {},
    MultiwayActionCalibrationLimits limits = {});

}  // namespace texas::solver::multiway
