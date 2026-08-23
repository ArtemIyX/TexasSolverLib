#pragma once

#include "core/legacy_namespace_compat.hpp"

#include "core/types.hpp"
#include "util/checked_numeric.hpp"
#include "core/game.hpp"
#include "util/infoset_lookup.hpp"
#include "util/infoset_registry.hpp"
#include "util/iteration_range.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <chrono>
#include <limits>
#include <iostream>
#include <numeric>
#include <stdexcept>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>

namespace texas::solver::dcfr {

/**
 * @brief Discounted Counterfactual Regret Minimization parameters.
 */
struct DCFRConfig {
    double alpha = 1.5;
    double beta = 0.0;
    double gamma = 2.0;
};

inline void validate_alpha(double alpha) {
    if (!std::isfinite(alpha) || alpha <= 0.0) {
        throw std::invalid_argument(
            "DCFR alpha must be > 0 and finite; alpha=0 silently stalls convergence");
    }
    if (alpha < 0.5) {
        std::cerr << "[dcfr] WARNING: alpha=" << alpha
                  << " is below the paper's analyzed range; production uses 1.5.\n";
    }
}

inline void validate_dcfr_config_values(double alpha, double beta, double gamma) {
    validate_alpha(alpha);
    if (!std::isfinite(beta) || !std::isfinite(gamma) ||
        beta < 0.0 || gamma < 0.0) {
        throw std::invalid_argument(
            "DCFR beta and gamma must be non-negative and finite");
    }
}

struct DCFRIterationScales {
    double positive_regret = 1.0;
    double negative_regret = 1.0;
    double strategy_sum = 1.0;
};

[[nodiscard]] inline DCFRIterationScales dcfr_iteration_scales(
    std::uint32_t iteration,
    double alpha,
    double beta,
    double gamma) {
    if (iteration == 0U) {
        throw std::invalid_argument("DCFR discount iteration must be positive");
    }
    const auto t = static_cast<double>(iteration);
    return DCFRIterationScales{
        1.0 / (1.0 + std::pow(t, -alpha)),
        1.0 / (1.0 + std::pow(t, -beta)),
        std::pow(t / (t + 1.0), gamma),
    };
}

/**
 * @brief Base type for solver implementations.
 */
class DCFRSolverBase {
public:
    virtual ~DCFRSolverBase() = default;
};

namespace detail {

struct InfosetAccum {
    std::size_t offset = 0;
    std::size_t action_count = 0;
    std::uint32_t last_discount_iter = 0;
    bool active = false;
};

struct InfosetAccumView {
    double* regret_sum = nullptr;
    double* strategy_sum = nullptr;
    std::size_t action_count = 0;
};

struct ConstInfosetAccumView {
    const double* regret_sum = nullptr;
    const double* strategy_sum = nullptr;
    std::size_t action_count = 0;
};

class IndexedStrategyTable {
public:
    void clear() noexcept {
        values_.clear();
        present_.clear();
        size_ = 0;
    }

    void set(InfosetId id, std::vector<Probability> strategy) {
        const auto index = static_cast<std::size_t>(id.value);
        if (index >= values_.size()) {
            values_.resize(index + 1);
            present_.resize(index + 1, false);
        }
        if (!present_[index]) {
            ++size_;
        }
        values_[index] = std::move(strategy);
        present_[index] = true;
    }

    [[nodiscard]] std::size_t size() const noexcept {
        return size_;
    }

    [[nodiscard]] const std::vector<Probability>* get(InfosetId id) const noexcept {
        const auto index = static_cast<std::size_t>(id.value);
        if (index >= values_.size() || !present_[index]) {
            return nullptr;
        }
        return &values_[index];
    }

private:
    std::vector<std::vector<Probability>> values_;
    std::vector<bool> present_;
    std::size_t size_ = 0;
};

class InfosetAccumTable {
public:
    void clear() noexcept {
        rows_.clear();
        active_ids_.clear();
        regret_arena_.clear();
        strategy_arena_.clear();
        current_discount_iter_ = 0;
    }

    [[nodiscard]] std::size_t size() const noexcept {
        return active_ids_.size();
    }

    [[nodiscard]] bool empty() const noexcept {
        return active_ids_.empty();
    }

    [[nodiscard]] const std::vector<InfosetId>& active_ids() const noexcept {
        return active_ids_;
    }

    InfosetAccumView ensure(InfosetId id, std::size_t action_count) {
        if (id.value >= rows_.size()) {
            rows_.resize(static_cast<std::size_t>(id.value) + 1);
        }

        auto& row = rows_[id.value];
        if (!row.active) {
            const auto new_size = texas::util::detail::checked_size_add(
                regret_arena_.size(), action_count, "infoset accumulation arena size overflow");
            if (strategy_arena_.size() != regret_arena_.size()) {
                throw std::logic_error("infoset accumulation arenas are misaligned");
            }
            const auto offset = regret_arena_.size();
            regret_arena_.resize(new_size, 0.0);
            strategy_arena_.resize(new_size, 0.0);
            active_ids_.push_back(id);
            row.offset = offset;
            row.action_count = action_count;
            row.last_discount_iter = current_discount_iter_;
            row.active = true;
        } else if (row.action_count != action_count) {
            throw std::invalid_argument("infoset action count changed");
        }

        return InfosetAccumView{
            regret_arena_.data() + row.offset,
            strategy_arena_.data() + row.offset,
            row.action_count,
        };
    }

    [[nodiscard]] ConstInfosetAccumView view(InfosetId id, std::size_t action_count) const {
        if (id.value >= rows_.size() || !rows_[id.value].active) {
            return {};
        }

        const auto& row = rows_[id.value];
        if (row.action_count != action_count) {
            throw std::invalid_argument("infoset action count changed");
        }
        return ConstInfosetAccumView{
            regret_arena_.data() + row.offset,
            strategy_arena_.data() + row.offset,
            row.action_count,
        };
    }

    void begin_dcfr_iteration(std::uint32_t target, const DCFRConfig& config) {
        if (target == 0U) {
            throw std::invalid_argument("DCFR discount iteration must be positive");
        }
        if (target < current_discount_iter_) {
            throw std::invalid_argument("DCFR discount iteration cannot move backwards");
        }
        if (target == current_discount_iter_) {
            return;
        }

        for (const auto id : active_ids_) {
            auto& row = rows_[id.value];
            for_each_u32_after(
                row.last_discount_iter,
                target,
                [&](std::uint32_t iteration) {
                    const auto scales = dcfr_iteration_scales(
                        iteration, config.alpha, config.beta, config.gamma);
                    for (std::size_t action = 0; action < row.action_count; ++action) {
                        auto& regret = regret_arena_[row.offset + action];
                        if (regret > 0.0) {
                            regret *= scales.positive_regret;
                        } else if (regret < 0.0) {
                            regret *= scales.negative_regret;
                        }
                        strategy_arena_[row.offset + action] *= scales.strategy_sum;
                    }
                });
            row.last_discount_iter = target;
        }
        current_discount_iter_ = target;
    }

private:
    std::vector<InfosetAccum> rows_;
    std::vector<InfosetId> active_ids_;
    std::vector<double> regret_arena_;
    std::vector<double> strategy_arena_;
    std::uint32_t current_discount_iter_ = 0;
};

inline std::vector<Probability> normalize_strategy(const double* regrets, std::size_t regret_count) {
    std::vector<Probability> strategy(regret_count, 0.0);
    double positive_sum = 0.0;
    for (std::size_t i = 0; i < regret_count; ++i) {
        if (regrets[i] > 0.0) {
            positive_sum += regrets[i];
        }
    }

    if (positive_sum > 0.0) {
        for (std::size_t i = 0; i < regret_count; ++i) {
            strategy[i] = regrets[i] > 0.0 ? regrets[i] / positive_sum : 0.0;
        }
        return strategy;
    }

    if (!strategy.empty()) {
        const double uniform = 1.0 / static_cast<double>(strategy.size());
        std::fill(strategy.begin(), strategy.end(), uniform);
    }
    return strategy;
}

inline std::vector<Probability> normalize_strategy(const std::vector<double>& regrets) {
    return normalize_strategy(regrets.data(), regrets.size());
}

inline std::vector<Probability> normalize_or_uniform(const double* values, std::size_t value_count) {
    const double sum = std::accumulate(values, values + value_count, 0.0);
    if (sum > 0.0) {
        std::vector<Probability> out(value_count, 0.0);
        for (std::size_t i = 0; i < value_count; ++i) {
            out[i] = values[i] / sum;
        }
        return out;
    }

    if (value_count == 0) {
        return {};
    }

    return std::vector<Probability>(value_count, 1.0 / static_cast<double>(value_count));
}

inline std::vector<Probability> normalize_or_uniform(const std::vector<double>& values) {
    return normalize_or_uniform(values.data(), values.size());
}

template <class G>
std::array<Value, 2> profile_values(
    const G& state,
    const std::unordered_map<InfosetKey, std::vector<Probability>>& strategy) {
    if (state.is_terminal()) {
        const auto utility = state.utility();
        return {utility.at(0), utility.at(1)};
    }

    const PlayerId player = state.current_player();
    if (player < 0) {
        std::array<Value, 2> value = {0.0, 0.0};
        for (const auto& outcome : state.chance_outcomes()) {
            const auto child = profile_values(
                state.next_state(outcome.action), strategy);
            value[0] += outcome.probability * child[0];
            value[1] += outcome.probability * child[1];
        }
        return value;
    }

    const auto actions = state.legal_actions();
    const auto it = strategy.find(state.infoset_key(player));
    const auto has_strategy =
        it != strategy.end() && it->second.size() == actions.size();
    const auto uniform = actions.empty()
        ? 0.0
        : 1.0 / static_cast<double>(actions.size());
    std::array<Value, 2> value = {0.0, 0.0};
    for (std::size_t action = 0; action < actions.size(); ++action) {
        const auto probability =
            has_strategy ? it->second[action] : uniform;
        const auto child =
            profile_values(state.next_state(actions[action]), strategy);
        value[0] += probability * child[0];
        value[1] += probability * child[1];
    }
    return value;
}

template <class G>
Value constrained_br_state_value(
    const G& state,
    std::size_t br_player,
    const std::unordered_map<InfosetKey, std::size_t>& best_action,
    const std::unordered_map<InfosetKey, std::vector<Probability>>& strategy) {
    if (state.is_terminal()) {
        return state.utility().at(br_player);
    }

    const PlayerId player = state.current_player();
    if (player < 0) {
        Value value = 0.0;
        for (const auto& outcome : state.chance_outcomes()) {
            value += outcome.probability * constrained_br_state_value(
                state.next_state(outcome.action),
                br_player,
                best_action,
                strategy);
        }
        return value;
    }

    const auto actions = state.legal_actions();
    if (static_cast<std::size_t>(player) == br_player) {
        const auto it = best_action.find(state.infoset_key(player));
        const auto action = it == best_action.end() ? 0U : it->second;
        return constrained_br_state_value(
            state.next_state(actions.at(action)),
            br_player,
            best_action,
            strategy);
    }

    const auto it = strategy.find(state.infoset_key(player));
    const auto has_strategy =
        it != strategy.end() && it->second.size() == actions.size();
    const auto uniform = actions.empty()
        ? 0.0
        : 1.0 / static_cast<double>(actions.size());
    Value value = 0.0;
    for (std::size_t action = 0; action < actions.size(); ++action) {
        const auto probability =
            has_strategy ? it->second[action] : uniform;
        value += probability * constrained_br_state_value(
            state.next_state(actions[action]),
            br_player,
            best_action,
            strategy);
    }
    return value;
}

template <class G>
void collect_br_infosets(
    const G& state,
    Probability counterfactual_reach,
    std::size_t br_player,
    const std::unordered_map<InfosetKey, std::vector<Probability>>& strategy,
    std::unordered_map<
        InfosetKey,
        std::vector<std::pair<G, Probability>>>& groups) {
    if (state.is_terminal()) {
        return;
    }

    const PlayerId player = state.current_player();
    if (player < 0) {
        for (const auto& outcome : state.chance_outcomes()) {
            collect_br_infosets(
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
        groups[state.infoset_key(player)].push_back(
            {state, counterfactual_reach});
        for (const auto action : actions) {
            collect_br_infosets(
                state.next_state(action),
                counterfactual_reach,
                br_player,
                strategy,
                groups);
        }
        return;
    }

    const auto it = strategy.find(state.infoset_key(player));
    const auto has_strategy =
        it != strategy.end() && it->second.size() == actions.size();
    const auto uniform = actions.empty()
        ? 0.0
        : 1.0 / static_cast<double>(actions.size());
    for (std::size_t action = 0; action < actions.size(); ++action) {
        const auto probability =
            has_strategy ? it->second[action] : uniform;
        collect_br_infosets(
            state.next_state(actions[action]),
            counterfactual_reach * probability,
            br_player,
            strategy,
            groups);
    }
}

template <class G>
Value constrained_best_response_value(
    const G& root,
    const std::unordered_map<InfosetKey, std::vector<Probability>>& strategy,
    std::size_t br_player) {
    std::unordered_map<
        InfosetKey,
        std::vector<std::pair<G, Probability>>> groups;
    collect_br_infosets(root, 1.0, br_player, strategy, groups);

    std::unordered_map<InfosetKey, std::size_t> best_action;
    for (;;) {
        const auto previous = best_action;
        for (const auto& [key, entries] : groups) {
            const auto action_count = entries.front().first.legal_actions().size();
            std::vector<Value> action_values(action_count, 0.0);
            for (const auto& [state, counterfactual_reach] : entries) {
                const auto actions = state.legal_actions();
                for (std::size_t action = 0; action < actions.size(); ++action) {
                    action_values[action] +=
                        counterfactual_reach * constrained_br_state_value(
                            state.next_state(actions[action]),
                            br_player,
                            best_action,
                            strategy);
                }
            }

            std::size_t best = 0;
            for (std::size_t action = 1; action < action_values.size(); ++action) {
                if (action_values[action] > action_values[best]) {
                    best = action;
                }
            }
            best_action[key] = best;
        }
        if (best_action == previous) {
            break;
        }
    }

    return constrained_br_state_value(
        root, br_player, best_action, strategy);
}

template <class G>
Value mean_unilateral_improvement(
    const G& root,
    const std::unordered_map<InfosetKey, std::vector<Probability>>& strategy) {
    const auto on_policy = profile_values(root, strategy);
    Value total_improvement = 0.0;
    for (std::size_t player = 0; player < on_policy.size(); ++player) {
        total_improvement +=
            constrained_best_response_value(root, strategy, player) -
            on_policy[player];
    }
    return total_improvement / static_cast<Value>(on_policy.size());
}

}  // namespace detail

template <class G>
class DCFRSolver : public DCFRSolverBase {
public:
    explicit DCFRSolver(DCFRConfig config, G root = G::initial());

    void set_locked_strategies(std::unordered_map<InfosetKey, std::vector<Probability>> locked);
    SolveOutput solve(std::uint32_t iterations);

private:
    using StrategyMap = detail::IndexedStrategyTable;

    double cfr(
        const G& state,
        PlayerId traversing_player,
        const std::array<double, 2>& reach_probs,
        double chance_reach);
    void validate_config() const;
    StrategyMap build_average_strategy() const;

    DCFRConfig config_;
    G root_;
    std::unordered_map<InfosetKey, std::vector<Probability>> locked_;
    InfosetRegistry registry_;
    detail::IndexedStrategyTable locked_by_id_;
    detail::InfosetAccumTable infosets_;
};

template <class G>
DCFRSolver<G>::DCFRSolver(DCFRConfig config, G root) : config_(config), root_(std::move(root)) {
    validate_config();
}

template <class G>
void DCFRSolver<G>::validate_config() const {
    validate_dcfr_config_values(config_.alpha, config_.beta, config_.gamma);
}

template <class G>
void DCFRSolver<G>::set_locked_strategies(
    std::unordered_map<InfosetKey, std::vector<Probability>> locked) {
    locked_ = std::move(locked);
}

template <class G>
double DCFRSolver<G>::cfr(
    const G& state,
    PlayerId traversing_player,
    const std::array<double, 2>& reach_probs,
    double chance_reach) {
    if (state.is_terminal()) {
        return state.utility().at(static_cast<std::size_t>(traversing_player));
    }

    const PlayerId player = state.current_player();
    if (player < 0) {
        double value = 0.0;
        for (const auto& outcome : state.chance_outcomes()) {
            value += outcome.probability *
                     cfr(state.next_state(outcome.action), traversing_player, reach_probs,
                         chance_reach * outcome.probability);
        }
        return value;
    }

    const auto actions = state.legal_actions();
    const auto id = texas::lookup_infoset_id(
        state, player, registry_, actions.size(), &locked_, &locked_by_id_);
    auto accum = infosets_.ensure(id, actions.size());

    std::vector<Probability> strategy;
    if (const auto* locked_strategy = locked_by_id_.get(id);
        locked_strategy != nullptr && locked_strategy->size() == actions.size()) {
        strategy = *locked_strategy;
    } else {
        strategy = detail::normalize_strategy(accum.regret_sum, accum.action_count);
    }

    if (player == traversing_player) {
        for (std::size_t i = 0; i < actions.size(); ++i) {
            accum.strategy_sum[i] += chance_reach * reach_probs[static_cast<std::size_t>(player)] * strategy[i];
        }
    }

    std::vector<double> action_values(actions.size(), 0.0);
    double node_value = 0.0;
    for (std::size_t i = 0; i < actions.size(); ++i) {
        auto next_reach = reach_probs;
        next_reach[static_cast<std::size_t>(player)] *= strategy[i];
        action_values[i] =
            cfr(state.next_state(actions[i]), traversing_player, next_reach, chance_reach);
        node_value += strategy[i] * action_values[i];
    }

    if (player == traversing_player && locked_by_id_.get(id) == nullptr) {
        // Child recursion may activate new infosets and grow the flat arenas,
        // so reacquire the row view before writing through its pointers.
        accum = infosets_.ensure(id, actions.size());
        const PlayerId opponent = 1 - traversing_player;
        const double opponent_reach = chance_reach * reach_probs[static_cast<std::size_t>(opponent)];
        for (std::size_t i = 0; i < actions.size(); ++i) {
            accum.regret_sum[i] += opponent_reach * (action_values[i] - node_value);
        }
    }

    return node_value;
}

template <class G>
typename DCFRSolver<G>::StrategyMap DCFRSolver<G>::build_average_strategy() const {
    StrategyMap out;
    for (const auto id : infosets_.active_ids()) {
        const auto action_count = registry_.meta_for(id).action_count;
        const auto accum = infosets_.view(id, action_count);
        if (const auto* locked_strategy = locked_by_id_.get(id);
            locked_strategy != nullptr && locked_strategy->size() == accum.action_count) {
            out.set(id, *locked_strategy);
            continue;
        }
        out.set(id, detail::normalize_or_uniform(accum.strategy_sum, accum.action_count));
    }
    return out;
}

template <class G>
SolveOutput DCFRSolver<G>::solve(std::uint32_t iterations) {
    infosets_.clear();
    registry_.clear();
    locked_by_id_.clear();
    const auto traversal_start = std::chrono::steady_clock::now();
    for (std::uint32_t iter = 0; iter < iterations; ++iter) {
        infosets_.begin_dcfr_iteration(iter + 1U, config_);
        cfr(root_, 0, {1.0, 1.0}, 1.0);
        cfr(root_, 1, {1.0, 1.0}, 1.0);
    }
    const auto traversal_finish = std::chrono::steady_clock::now();

    const auto finalize_start = std::chrono::steady_clock::now();
    const auto average_strategy = build_average_strategy();
    std::unordered_map<InfosetKey, std::vector<Probability>> average_strategy_by_key;
    average_strategy_by_key.reserve(infosets_.size());
    for (const auto id : infosets_.active_ids()) {
        if (const auto* strategy = average_strategy.get(id); strategy != nullptr) {
            average_strategy_by_key.emplace(registry_.key_for(id), *strategy);
        }
    }

    SolveOutput out;
    out.iterations = iterations;
    out.used_parallel = false;
    out.traversal_seconds = std::chrono::duration<double>(traversal_finish - traversal_start).count();
    const auto values =
        detail::profile_values(root_, average_strategy_by_key);
    out.game_value = values[0];
    out.exploitability =
        detail::mean_unilateral_improvement(
            root_, average_strategy_by_key);
    out.average_strategy.reserve(average_strategy.size());
    for (auto& [key, strategy] : average_strategy_by_key) {
        out.average_strategy.emplace_back(std::move(key), std::move(strategy));
    }
    std::sort(
        out.average_strategy.begin(),
        out.average_strategy.end(),
        [](const auto& lhs, const auto& rhs) { return lhs.first < rhs.first; });
    out.finalize_seconds = std::chrono::duration<double>(
        std::chrono::steady_clock::now() - finalize_start).count();
    return out;
}

}  // namespace texas::solver::dcfr


