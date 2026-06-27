#include "solver/parallel_dcfr.hpp"

#include "games/hunl.hpp"
#include "games/kuhn.hpp"
#include "games/leduc.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <future>
#include <stdexcept>
#include <string>
#include <utility>

namespace core {

bool parallel_dcfr_enabled() {
    char* raw_value = nullptr;
#if defined(_MSC_VER)
    std::size_t len = 0;
    if (_dupenv_s(&raw_value, &len, "TEXASSOLVER_PARALLEL_CFR") != 0) {
        raw_value = nullptr;
    }
#else
    raw_value = std::getenv("TEXASSOLVER_PARALLEL_CFR");
#endif
    if (raw_value == nullptr) {
        return false;
    }
    std::string value(raw_value);
#if defined(_MSC_VER)
    free(raw_value);
#endif
    value.erase(value.begin(), std::find_if(value.begin(), value.end(), [](unsigned char ch) {
        return !std::isspace(ch);
    }));
    value.erase(std::find_if(value.rbegin(), value.rend(), [](unsigned char ch) {
        return !std::isspace(ch);
    }).base(), value.end());
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return !(value == "0" || value == "false" || value == "off");
}

template <class G>
ParallelDCFRSolver<G>::ParallelDCFRSolver(DCFRConfig config, G root)
    : config_(config), root_(std::move(root)) {
    validate_config();
}

template <class G>
void ParallelDCFRSolver<G>::validate_config() const {
    validate_alpha(config_.alpha);
    if (config_.beta < 0.0 || config_.gamma < 0.0) {
        throw std::invalid_argument("DCFR beta and gamma must be non-negative");
    }
}

template <class G>
void ParallelDCFRSolver<G>::set_locked_strategies(
    std::unordered_map<InfosetKey, std::vector<Probability>> locked) {
    locked_ = std::move(locked);
}

template <class G>
typename ParallelDCFRSolver<G>::StrategyMap ParallelDCFRSolver<G>::build_strategy_snapshot(
    const AccumMap& accum) {
    StrategyMap snapshot;
    snapshot.reserve(accum.size());
    for (const auto& [key, info] : accum) {
        if (info.regret_sum.empty()) {
            continue;
        }
        snapshot.emplace(key, detail::normalize_strategy(info.regret_sum));
    }
    return snapshot;
}

template <class G>
void ParallelDCFRSolver<G>::merge_accum(AccumMap& dst, const AccumMap& src) {
    for (const auto& [key, delta] : src) {
        auto& out = dst[key];
        if (out.regret_sum.empty()) {
            out.regret_sum = delta.regret_sum;
            out.strategy_sum = delta.strategy_sum;
            continue;
        }
        if (out.regret_sum.size() != delta.regret_sum.size() ||
            out.strategy_sum.size() != delta.strategy_sum.size()) {
            throw std::logic_error("parallel CFR merge encountered mismatched action counts");
        }
        for (std::size_t i = 0; i < delta.regret_sum.size(); ++i) {
            out.regret_sum[i] += delta.regret_sum[i];
            out.strategy_sum[i] += delta.strategy_sum[i];
        }
    }
}

template <class G>
double ParallelDCFRSolver<G>::cfr(
    const G& state,
    PlayerId traversing_player,
    const std::array<double, 2>& reach_probs,
    double chance_reach,
    const StrategyMap& strategy,
    AccumMap& accum) {
    if (state.is_terminal()) {
        return state.utility().at(static_cast<std::size_t>(traversing_player));
    }

    const PlayerId player = state.current_player();
    if (player < 0) {
        double value = 0.0;
        for (const auto& outcome : state.chance_outcomes()) {
            value += outcome.probability *
                     cfr(state.next_state(outcome.action), traversing_player, reach_probs,
                         chance_reach * outcome.probability, strategy, accum);
        }
        return value;
    }

    const auto actions = state.legal_actions();
    const auto key = state.infoset_key(player);
    auto& local = accum[key];
    if (local.regret_sum.empty()) {
        local.regret_sum.assign(actions.size(), 0.0);
        local.strategy_sum.assign(actions.size(), 0.0);
    }

    std::vector<Probability> current_strategy;
    if (const auto locked_it = locked_.find(key);
        locked_it != locked_.end() && locked_it->second.size() == actions.size()) {
        current_strategy = locked_it->second;
    } else if (const auto it = strategy.find(key);
               it != strategy.end() && it->second.size() == actions.size()) {
        current_strategy = it->second;
    } else {
        current_strategy = std::vector<Probability>(actions.size(), 1.0 / static_cast<double>(actions.size()));
    }

    if (player == traversing_player) {
        for (std::size_t i = 0; i < actions.size(); ++i) {
            local.strategy_sum[i] +=
                chance_reach * reach_probs[static_cast<std::size_t>(player)] * current_strategy[i];
        }
    }

    std::vector<double> action_values(actions.size(), 0.0);
    double node_value = 0.0;
    for (std::size_t i = 0; i < actions.size(); ++i) {
        auto next_reach = reach_probs;
        next_reach[static_cast<std::size_t>(player)] *= current_strategy[i];
        action_values[i] =
            cfr(state.next_state(actions[i]), traversing_player, next_reach, chance_reach, strategy, accum);
        node_value += current_strategy[i] * action_values[i];
    }

    if (player == traversing_player && locked_.find(key) == locked_.end()) {
        const PlayerId opponent = 1 - traversing_player;
        const double opponent_reach = chance_reach * reach_probs[static_cast<std::size_t>(opponent)];
        for (std::size_t i = 0; i < actions.size(); ++i) {
            local.regret_sum[i] += opponent_reach * (action_values[i] - node_value);
        }
    }

    return node_value;
}

template <class G>
typename ParallelDCFRSolver<G>::StrategyMap ParallelDCFRSolver<G>::build_average_strategy(
    const AccumMap& accum) const {
    StrategyMap out;
    for (const auto& [key, local] : accum) {
        if (const auto locked_it = locked_.find(key);
            locked_it != locked_.end() && locked_it->second.size() == local.strategy_sum.size()) {
            out.emplace(key, locked_it->second);
            continue;
        }
        out.emplace(key, detail::normalize_or_uniform(local.strategy_sum));
    }
    return out;
}

template <class G>
SolveOutput ParallelDCFRSolver<G>::solve(std::uint32_t iterations) {
    infosets_.clear();
    for (std::uint32_t iter = 0; iter < iterations; ++iter) {
        const auto strategy = build_strategy_snapshot(infosets_);
        auto f0 = std::async(std::launch::async, [&] {
            AccumMap delta;
            cfr(root_, 0, {1.0, 1.0}, 1.0, strategy, delta);
            return delta;
        });
        auto f1 = std::async(std::launch::async, [&] {
            AccumMap delta;
            cfr(root_, 1, {1.0, 1.0}, 1.0, strategy, delta);
            return delta;
        });
        merge_accum(infosets_, f0.get());
        merge_accum(infosets_, f1.get());
    }

    const auto average_strategy = build_average_strategy(infosets_);

    SolveOutput out;
    out.iterations = iterations;
    out.game_value = detail::expected_value_player(root_, average_strategy, 0);
    const double br0 = detail::best_response_value(root_, average_strategy, 0);
    const double br1 = detail::best_response_value(root_, average_strategy, 1);
    out.exploitability = br0 + br1;
    out.average_strategy.reserve(average_strategy.size());
    for (const auto& [key, strategy] : average_strategy) {
        out.average_strategy.emplace_back(key, strategy);
    }
    std::sort(
        out.average_strategy.begin(),
        out.average_strategy.end(),
        [](const auto& lhs, const auto& rhs) { return lhs.first < rhs.first; });
    return out;
}

template class ParallelDCFRSolver<KuhnState>;
template class ParallelDCFRSolver<LeducState>;
template class ParallelDCFRSolver<HUNLState>;

}  // namespace core
