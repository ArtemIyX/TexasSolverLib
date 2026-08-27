#pragma once

#include "solver/multiway_action_abstraction.hpp"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace texas::solver::multiway {

struct MultiwayActionCalibrationCase {
    MultiwayBettingSnapshot betting{};
    MultiwayAction observed_action = MultiwayAction::Check;
    int observed_target = 0;
    MultiwayActionAbstractionContext context{};
};

struct MultiwayActionCalibrationLimits {
    std::size_t maximum_actions = MULTIWAY_MAX_ABSTRACTED_ACTIONS;
    double maximum_translation_rejection_rate = 1.0;
    std::uint64_t maximum_estimated_menu_bytes = 1U << 20U;
    void validate() const;
};

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
    bool within_limits = false;

    [[nodiscard]] double mean_branching_factor() const noexcept {
        return cases == 0U ? 0.0 : static_cast<double>(total_actions) / static_cast<double>(cases);
    }
    [[nodiscard]] double translation_rejection_rate() const noexcept {
        return cases == 0U ? 0.0 : static_cast<double>(rejected_translations) / static_cast<double>(cases);
    }
};

[[nodiscard]] MultiwayActionCalibrationResult calibrate_multiway_action_abstraction(
    const std::vector<MultiwayActionCalibrationCase>& cases,
    MultiwayActionAbstractionConfig abstraction = {},
    MultiwayDeviationExpansionConfig expansion = {},
    MultiwayActionCalibrationLimits limits = {});

}  // namespace texas::solver::multiway
