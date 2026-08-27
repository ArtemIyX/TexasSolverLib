#include "solver/multiway_action_calibration.hpp"

#include <stdexcept>
#include <algorithm>
#include <cmath>
#include <limits>

namespace texas::solver::multiway {

void MultiwayActionCalibrationLimits::validate() const {
    if (maximum_actions == 0U || maximum_actions > MULTIWAY_MAX_ABSTRACTED_ACTIONS ||
        maximum_translation_rejection_rate < 0.0 || maximum_translation_rejection_rate > 1.0 ||
        maximum_estimated_menu_bytes == 0U) {
        throw std::invalid_argument("action calibration limits are invalid");
    }
}

MultiwayActionCalibrationResult calibrate_multiway_action_abstraction(
    const std::vector<MultiwayActionCalibrationCase>& cases,
    MultiwayActionAbstractionConfig abstraction,
    MultiwayDeviationExpansionConfig expansion,
    MultiwayActionCalibrationLimits limits) {
    if (cases.empty()) throw std::invalid_argument("action calibration requires representative cases");
    expansion.validate();
    limits.validate();
    const MultiwayActionAbstraction evaluator(abstraction);
    MultiwayActionCalibrationResult result;
    result.cases = cases.size();
    result.translation_policy_identity = expansion.policy_version;
    result.translation_policy_identity ^= static_cast<std::uint64_t>(abstraction.translation_policy_version) << 32U;
    result.translation_policy_identity ^= abstraction.translation_max_pseudo_harmonic_distance_basis_points;
    for (const auto& test_case : cases) {
        const auto menu = evaluator.make_legal_actions(test_case.betting, test_case.context);
        result.profile_identity ^= evaluator.menu_profile_identity(test_case.context) +
            0x9e3779b97f4a7c15ULL + (result.profile_identity << 6U) + (result.profile_identity >> 2U);
        result.total_actions += menu.size();
        result.max_actions = std::max(result.max_actions, menu.size());
        result.estimated_menu_bytes += static_cast<std::uint64_t>(menu.size()) * sizeof(MultiwayActionDescriptor);
        const auto translation = evaluator.translate_observed_action(
            test_case.betting, menu, test_case.observed_action, test_case.observed_target, test_case.context);
        switch (translation.status) {
            case MultiwayActionTranslationStatus::ExactMenuAction: ++result.exact_translations; break;
            case MultiwayActionTranslationStatus::Translated: ++result.translated_actions; break;
            case MultiwayActionTranslationStatus::DeviationTooLarge: ++result.rejected_translations; break;
        }
        if (evaluator.classify_observed_action(
                test_case.betting, menu, test_case.observed_action, test_case.observed_target,
                expansion, test_case.context) == MultiwayDeviationDisposition::Expand) {
            ++result.expanded_actions;
        }
    }
    result.within_limits = result.max_actions <= limits.maximum_actions &&
        result.translation_rejection_rate() <= limits.maximum_translation_rejection_rate &&
        result.estimated_menu_bytes <= limits.maximum_estimated_menu_bytes;
    return result;
}

MultiwayActionCalibrationSelection select_multiway_action_profile(
    const std::vector<MultiwayActionAbstractionConfig>& candidates,
    const std::vector<MultiwayActionCalibrationCase>& cases,
    double frozen_baseline_value,
    MultiwayActionCalibrationValueFn value_fn,
    const void* context,
    MultiwayDeviationExpansionConfig expansion,
    MultiwayActionCalibrationLimits limits) {
    if (candidates.empty() || value_fn == nullptr || !std::isfinite(frozen_baseline_value)) {
        throw std::invalid_argument("action profile selection requires candidates, baseline, and scorer");
    }
    MultiwayActionCalibrationSelection selected;
    selected.baseline_value = frozen_baseline_value;
    selected.candidate_value = -std::numeric_limits<double>::infinity();
    for (const auto& candidate : candidates) {
        const auto result = calibrate_multiway_action_abstraction(cases, candidate, expansion, limits);
        const auto value = value_fn(result, context);
        if (!std::isfinite(value)) throw std::invalid_argument("action profile scorer returned a non-finite value");
        if (result.within_limits && value > selected.candidate_value && value > frozen_baseline_value) {
            selected.abstraction = candidate;
            selected.result = result;
            selected.candidate_value = value;
            selected.selected = true;
        }
    }
    return selected;
}

}  // namespace texas::solver::multiway
