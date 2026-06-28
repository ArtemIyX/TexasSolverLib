#pragma once

#include "core/types.hpp"
#include "solver/dcfr.hpp"
#include "solver/parallel_dcfr.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <unordered_map>
#include <utility>
#include <vector>

namespace core {

/**
 * @brief Validate DCFR parameters before solving.
 */
void validate_dcfr_parameters(double alpha, double beta, double gamma);
/**
 * @brief Solve Kuhn poker with DCFR.
 */
SolveOutput solve_kuhn(std::uint32_t iterations, double alpha, double beta, double gamma);
/**
 * @brief Solve Leduc poker with DCFR.
 */
SolveOutput solve_leduc(std::uint32_t iterations, double alpha, double beta, double gamma);

namespace detail {

using StrategyMap = std::unordered_map<InfosetKey, std::vector<Probability>>;

template <class G>
inline StrategyMap make_strategy_map(
    const std::vector<std::pair<InfosetKey, std::vector<Probability>>>& entries) {
    StrategyMap strategy;
    strategy.reserve(entries.size());
    for (const auto& [key, probs] : entries) {
        strategy.emplace(key, probs);
    }
    return strategy;
}

inline const std::vector<Probability>& strategy_for_infoset(
    const StrategyMap& strategy,
    const InfosetKey& key,
    std::size_t action_count,
    std::vector<Probability>& fallback_buffer) {
    const auto it = strategy.find(key);
    if (it != strategy.end() && it->second.size() == action_count) {
        return it->second;
    }

    fallback_buffer.assign(action_count, 0.0);
    if (action_count > 0) {
        const auto uniform = 1.0 / static_cast<double>(action_count);
        std::fill(fallback_buffer.begin(), fallback_buffer.end(), uniform);
    }
    return fallback_buffer;
}

template <class G>
std::array<Value, 2> expected_value(const G& state, const StrategyMap& strategy) {
    if (state.is_terminal()) {
        const auto utility = state.utility();
        return {utility.at(0), utility.at(1)};
    }

    const PlayerId player = state.current_player();
    if (player == -1) {
        std::array<Value, 2> value = {0.0, 0.0};
        for (const auto& outcome : state.chance_outcomes()) {
            const auto child = expected_value(state.next_state(outcome.action), strategy);
            value[0] += outcome.probability * child[0];
            value[1] += outcome.probability * child[1];
        }
        return value;
    }

    const auto actions = state.legal_actions();
    std::vector<Probability> fallback;
    const auto& probs =
        strategy_for_infoset(strategy, state.infoset_key(player), actions.size(), fallback);

    std::array<Value, 2> value = {0.0, 0.0};
    for (std::size_t idx = 0; idx < actions.size(); ++idx) {
        const auto child = expected_value(state.next_state(actions[idx]), strategy);
        value[0] += probs[idx] * child[0];
        value[1] += probs[idx] * child[1];
    }
    return value;
}

template <class G>
Value br_state_value(
    const G& state,
    std::size_t br_player,
    const std::unordered_map<InfosetKey, std::size_t>& best_action,
    const StrategyMap& strategy) {
    if (state.is_terminal()) {
        return state.utility().at(br_player);
    }

    const PlayerId player = state.current_player();
    if (player == -1) {
        Value value = 0.0;
        for (const auto& outcome : state.chance_outcomes()) {
            value += outcome.probability *
                     br_state_value(state.next_state(outcome.action), br_player, best_action, strategy);
        }
        return value;
    }

    const auto actions = state.legal_actions();
    if (static_cast<std::size_t>(player) == br_player) {
        const auto key = state.infoset_key(player);
        const auto best_it = best_action.find(key);
        const auto idx = best_it != best_action.end() ? best_it->second : 0U;
        return br_state_value(state.next_state(actions.at(idx)), br_player, best_action, strategy);
    }

    std::vector<Probability> fallback;
    const auto& probs =
        strategy_for_infoset(strategy, state.infoset_key(player), actions.size(), fallback);
    Value value = 0.0;
    for (std::size_t idx = 0; idx < actions.size(); ++idx) {
        value += probs[idx] *
                 br_state_value(state.next_state(actions[idx]), br_player, best_action, strategy);
    }
    return value;
}

template <class G>
void collect_infosets(
    const G& state,
    Probability counterfactual_reach,
    std::size_t br_player,
    const StrategyMap& strategy,
    std::unordered_map<InfosetKey, std::vector<std::pair<G, Probability>>>& groups) {
    if (state.is_terminal()) {
        return;
    }

    const PlayerId player = state.current_player();
    if (player == -1) {
        for (const auto& outcome : state.chance_outcomes()) {
            collect_infosets(
                state.next_state(outcome.action),
                counterfactual_reach * outcome.probability,
                br_player,
                strategy,
                groups);
        }
        return;
    }

    const auto actions = state.legal_actions();
    if (static_cast<std::size_t>(player) == br_player) {
        const auto key = state.infoset_key(player);
        groups[key].push_back({state, counterfactual_reach});
        for (const auto action : actions) {
            collect_infosets(state.next_state(action), counterfactual_reach, br_player, strategy, groups);
        }
        return;
    }

    std::vector<Probability> fallback;
    const auto& probs =
        strategy_for_infoset(strategy, state.infoset_key(player), actions.size(), fallback);
    for (std::size_t idx = 0; idx < actions.size(); ++idx) {
        collect_infosets(
            state.next_state(actions[idx]),
            counterfactual_reach * probs[idx],
            br_player,
            strategy,
            groups);
    }
}

template <class G>
Value best_response_value(const StrategyMap& strategy, std::size_t br_player) {
    std::unordered_map<InfosetKey, std::vector<std::pair<G, Probability>>> groups;
    collect_infosets(G::initial(), 1.0, br_player, strategy, groups);

    std::unordered_map<InfosetKey, std::size_t> best_action;
    while (true) {
        const auto previous = best_action;
        for (const auto& [key, entries] : groups) {
            const auto num_actions = entries.front().first.legal_actions().size();
            std::vector<Value> action_values(num_actions, 0.0);
            for (const auto& [state, counterfactual_reach] : entries) {
                const auto actions = state.legal_actions();
                for (std::size_t idx = 0; idx < actions.size(); ++idx) {
                    action_values[idx] +=
                        counterfactual_reach *
                        br_state_value(state.next_state(actions[idx]), br_player, best_action, strategy);
                }
            }

            std::size_t best_idx = 0;
            for (std::size_t idx = 1; idx < action_values.size(); ++idx) {
                if (action_values[idx] > action_values[best_idx]) {
                    best_idx = idx;
                }
            }
            best_action[key] = best_idx;
        }

        if (best_action == previous) {
            break;
        }
    }

    return br_state_value(G::initial(), br_player, best_action, strategy);
}

template <class G>
Value exploitability(const StrategyMap& strategy) {
    const auto on_policy = expected_value(G::initial(), strategy);
    Value total = 0.0;
    for (std::size_t player = 0; player < on_policy.size(); ++player) {
        total += best_response_value<G>(strategy, player) - on_policy[player];
    }
    return total / static_cast<Value>(on_policy.size());
}

template <class G>
SolveOutput solve_generic(
    std::uint32_t iterations,
    double alpha,
    double beta,
    double gamma,
    std::unordered_map<InfosetKey, std::vector<Probability>> locked_strategies = {}) {
    validate_dcfr_parameters(alpha, beta, gamma);

    SolveOutput output;
    if (parallel_dcfr_enabled()) {
        ParallelDCFRSolver<G> solver(DCFRConfig{alpha, beta, gamma}, G::initial());
        solver.set_locked_strategies(std::move(locked_strategies));
        output = solver.solve(iterations);
    } else {
        DCFRSolver<G> solver(DCFRConfig{alpha, beta, gamma}, G::initial());
        solver.set_locked_strategies(std::move(locked_strategies));
        output = solver.solve(iterations);
    }

    const auto strategy = make_strategy_map<G>(output.average_strategy);
    const auto value = expected_value(G::initial(), strategy);
    output.game_value = value[0];
    output.exploitability = exploitability<G>(strategy);
    return output;
}

}  // namespace detail

}  // namespace core


