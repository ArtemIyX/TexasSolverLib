#include "solver/multiway_action_abstraction.hpp"

#include <algorithm>
#include <numeric>
#include <stdexcept>

namespace core {

void MultiwayActionAbstractionConfig::validate() const {
    if (multiway_first_bet_count == 0U || multiway_first_bet_count > first_bet_basis_points.size() ||
        three_way_first_bet_count == 0U || three_way_first_bet_count > first_bet_basis_points.size() ||
        heads_up_first_bet_count == 0U || heads_up_first_bet_count > first_bet_basis_points.size()) {
        throw std::invalid_argument("multiway action abstraction has invalid first-bet counts");
    }
    for (const auto size : first_bet_basis_points) {
        if (size == 0U) throw std::invalid_argument("multiway action abstraction has a zero bet size");
    }
    for (const auto size : raise_basis_points) {
        if (size == 0U) throw std::invalid_argument("multiway action abstraction has a zero raise size");
    }
}

MultiwayActionAbstraction::MultiwayActionAbstraction(MultiwayActionAbstractionConfig config)
    : config_(config) {
    config_.validate();
}

std::vector<MultiwayActionDescriptor> MultiwayActionAbstraction::make_legal_actions(
    const MultiwayBettingSnapshot& betting,
    std::uint64_t action_menu_id) const {
    if (action_menu_id == 0U) throw std::invalid_argument("multiway action abstraction requires a menu id");
    const auto state = MultiwayState::from_snapshot(betting);
    const auto base_actions = state.legal_actions();
    const auto actor = static_cast<std::size_t>(state.current_player());
    const auto pot = std::accumulate(state.contributions().begin(), state.contributions().end(), 0);
    const auto all_in_target = state.street_contributions()[actor] + state.stacks()[actor];
    const auto live_count = static_cast<std::size_t>(std::count(state.folded().begin(), state.folded().end(), false));
    const auto first_bet_count = live_count <= 2U ? config_.heads_up_first_bet_count :
        live_count == 3U ? config_.three_way_first_bet_count : config_.multiway_first_bet_count;

    std::vector<MultiwayActionDescriptor> result;
    result.reserve(base_actions.size() + first_bet_count + config_.raise_basis_points.size());
    const auto append = [&](MultiwayAction action, int target) {
        const auto successor = state.apply(action, target);
        const auto actual_target = successor.street_contributions()[actor];
        const auto duplicate = std::find_if(result.begin(), result.end(),
            [action, actual_target](const MultiwayActionDescriptor& existing) {
                return existing.action == action && existing.target_street_contribution == actual_target;
            });
        if (duplicate == result.end()) {
            result.push_back({action, static_cast<std::uint32_t>(result.size()), actual_target, action_menu_id});
        }
    };

    for (const auto action : base_actions) {
        if (action == MultiwayAction::Bet) {
            for (std::uint8_t index = 0; index < first_bet_count; ++index) {
                const auto wager = std::max(state.last_full_raise_size(),
                    pot * static_cast<int>(config_.first_bet_basis_points[index]) / 10000);
                const auto target = std::min(all_in_target, state.street_contributions()[actor] + wager);
                if (target < all_in_target) append(action, target);
            }
        } else if (action == MultiwayAction::Raise) {
            const auto call = state.current_bet() - state.street_contributions()[actor];
            for (const auto fraction : config_.raise_basis_points) {
                const auto raise = std::max(state.last_full_raise_size(),
                    (pot + call) * static_cast<int>(fraction) / 10000);
                const auto target = std::min(all_in_target, state.current_bet() + raise);
                if (target < all_in_target) append(action, target);
            }
        } else {
            append(action, 0);
        }
    }
    return result;
}

std::vector<MultiwayActionDescriptor> MultiwayActionAbstraction::insert_exact_observed_action(
    const MultiwayBettingSnapshot& betting,
    std::vector<MultiwayActionDescriptor> menu,
    MultiwayAction observed_action,
    int target_street_contribution,
    std::uint64_t action_menu_id) {
    if (action_menu_id == 0U) throw std::invalid_argument("multiway observed action requires a menu id");
    const auto state = MultiwayState::from_snapshot(betting);
    const auto legal = state.legal_actions();
    if (std::find(legal.begin(), legal.end(), observed_action) == legal.end()) {
        throw std::invalid_argument("multiway observed action is not legal in this public state");
    }
    const auto successor = state.apply(observed_action, target_street_contribution);
    const auto actor = static_cast<std::size_t>(state.current_player());
    const auto exact_target = successor.street_contributions()[actor];
    const auto duplicate = std::find_if(menu.begin(), menu.end(),
        [observed_action, exact_target](const MultiwayActionDescriptor& action) {
            return action.action == observed_action && action.target_street_contribution == exact_target;
        });
    if (duplicate == menu.end()) {
        menu.push_back({observed_action, 0U, exact_target, action_menu_id});
    }
    for (std::size_t index = 0; index < menu.size(); ++index) {
        menu[index].action_index = static_cast<std::uint32_t>(index);
        menu[index].action_menu_id = action_menu_id;
    }
    return menu;
}

}  // namespace core
