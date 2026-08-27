#include "solver/multiway_action_calibration.hpp"

#include <stdexcept>
#include <algorithm>

namespace texas::solver::multiway {

MultiwayActionCalibrationResult calibrate_multiway_action_abstraction(
    const std::vector<MultiwayActionCalibrationCase>& cases,
    MultiwayActionAbstractionConfig abstraction,
    MultiwayDeviationExpansionConfig expansion) {
    if (cases.empty()) throw std::invalid_argument("action calibration requires representative cases");
    expansion.validate();
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
    return result;
}

}  // namespace texas::solver::multiway
