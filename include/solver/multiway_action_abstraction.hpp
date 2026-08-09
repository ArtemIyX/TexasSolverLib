#pragma once

#include "games/multiway_state.hpp"
#include "solver/multiway_solver.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace core {

// Keep this synchronized with the traversal's fixed-size action scratch.
inline constexpr std::size_t MULTIWAY_MAX_ABSTRACTED_ACTIONS = 8U;

enum class MultiwayPreflopSituation : std::uint8_t {
    Auto,
    Unopened,
    FacingSingleOpen,
    FacingOpenAndCallers,
    FacingThreeBetOrMore,
};

// Position is intentionally supplied by the hand-history layer. A betting
// snapshot does not retain the button or the action history needed to derive
// it losslessly.
enum class MultiwayRelativePosition : std::uint8_t {
    Unknown,
    InPosition,
    OutOfPosition,
};

enum class MultiwayPostflopSizingMode : std::uint8_t {
    // Preserves the original first-bet/raise configuration.
    Compatibility,
    // Uses active-player and effective-SPR-specific default templates.
    Contextual,
};

struct MultiwayActionAbstractionContext {
    MultiwayPreflopSituation preflop_situation = MultiwayPreflopSituation::Auto;
    MultiwayRelativePosition relative_position = MultiwayRelativePosition::Unknown;
    MultiwayPostflopSizingMode postflop_sizing = MultiwayPostflopSizingMode::Compatibility;
};

struct MultiwayActionAbstractionConfig {
    std::array<std::uint16_t, 3> first_bet_basis_points = {3300, 7500, 12500};
    std::array<std::uint16_t, 2> raise_basis_points = {7500, 12500};
    std::uint8_t multiway_first_bet_count = 2;
    std::uint8_t three_way_first_bet_count = 3;
    std::uint8_t heads_up_first_bet_count = 3;

    // Default preflop templates. Unopened opens use big-blind units; single-
    // open and three-bet templates use current-bet units.
    std::array<std::uint16_t, 3> unopened_raise_to_big_blind_basis_points = {22500, 30000, 45000};
    std::uint16_t single_open_in_position_basis_points = 30000;
    std::uint16_t single_open_out_of_position_basis_points = 35000;
    std::uint16_t open_caller_increment_big_blind_basis_points = 10000;
    std::uint16_t three_bet_or_more_basis_points = 22000;

    std::array<std::uint16_t, 2> contextual_multiway_first_bet_basis_points = {3300, 7500};
    std::array<std::uint16_t, 3> contextual_three_way_first_bet_basis_points = {3300, 7500, 12500};
    std::array<std::uint16_t, 4> contextual_heads_up_first_bet_basis_points = {2500, 5000, 10000, 15000};
    // The first contextual raise is always the exact minimum legal raise.
    std::array<std::uint16_t, 2> contextual_raise_basis_points = {7500, 12500};

    void validate() const;
};

class MultiwayActionAbstraction {
public:
    explicit MultiwayActionAbstraction(MultiwayActionAbstractionConfig config = {});

    [[nodiscard]] std::vector<MultiwayActionDescriptor> make_legal_actions(
        const MultiwayBettingSnapshot& betting,
        // Retained for source compatibility. Schema-v2 derives the id from
        // canonical action/target entries.
        std::uint64_t action_menu_id) const;

    [[nodiscard]] std::vector<MultiwayActionDescriptor> make_legal_actions(
        const MultiwayBettingSnapshot& betting,
        std::uint64_t action_menu_id,
        MultiwayActionAbstractionContext context) const;

    [[nodiscard]] static std::vector<MultiwayActionDescriptor> insert_exact_observed_action(
        const MultiwayBettingSnapshot& betting,
        std::vector<MultiwayActionDescriptor> menu,
        MultiwayAction observed_action,
        int target_street_contribution,
        // Retained for source compatibility. The returned menu has a newly
        // derived id when the inserted action changes its canonical entries.
        std::uint64_t action_menu_id);

private:
    MultiwayActionAbstractionConfig config_;
};

}  // namespace core
