#pragma once

#include "games/multiway_state.hpp"
#include "solver/multiway_solver.hpp"

#include <array>
#include <cstdint>
#include <vector>

namespace core {

struct MultiwayActionAbstractionConfig {
    std::array<std::uint16_t, 3> first_bet_basis_points = {3300, 7500, 12500};
    std::array<std::uint16_t, 2> raise_basis_points = {7500, 12500};
    std::uint8_t multiway_first_bet_count = 2;
    std::uint8_t three_way_first_bet_count = 3;
    std::uint8_t heads_up_first_bet_count = 3;

    void validate() const;
};

class MultiwayActionAbstraction {
public:
    explicit MultiwayActionAbstraction(MultiwayActionAbstractionConfig config = {});

    [[nodiscard]] std::vector<MultiwayActionDescriptor> make_legal_actions(
        const MultiwayBettingSnapshot& betting,
        std::uint64_t action_menu_id) const;

    [[nodiscard]] static std::vector<MultiwayActionDescriptor> insert_exact_observed_action(
        const MultiwayBettingSnapshot& betting,
        std::vector<MultiwayActionDescriptor> menu,
        MultiwayAction observed_action,
        int target_street_contribution,
        std::uint64_t action_menu_id);

private:
    MultiwayActionAbstractionConfig config_;
};

}  // namespace core
