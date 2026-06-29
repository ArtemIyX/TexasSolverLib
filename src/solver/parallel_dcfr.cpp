#include "solver/parallel_dcfr.hpp"

#include "games/hunl.hpp"
#include "games/kuhn.hpp"
#include "games/leduc.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <limits>
#include <mutex>
#include <thread>
#include <string>
#include <utility>

namespace core {

namespace {

struct BranchResult {
    std::vector<double> branch_values;
    ParallelWorkerState worker_state;
};

std::size_t parse_worker_count(const char* raw) {
    if (raw == nullptr) {
        return 1;
    }
    try {
        const auto value = std::stoul(raw);
        return value == 0 ? 1U : static_cast<std::size_t>(value);
    } catch (...) {
        return 1;
    }
}

ParallelSolvePlan make_partition_plan(std::size_t branch_count, std::size_t requested_workers) {
    ParallelSolvePlan plan;
    plan.enabled = requested_workers > 1;
    plan.worker_count = std::max<std::size_t>(1, std::min(requested_workers, branch_count));
    plan.items.reserve(plan.worker_count);

    const std::size_t base = branch_count / plan.worker_count;
    const std::size_t remainder = branch_count % plan.worker_count;
    std::size_t begin = 0;
    for (std::size_t worker = 0; worker < plan.worker_count; ++worker) {
        const std::size_t width = base + (worker < remainder ? 1 : 0);
        const std::size_t end = begin + width;
        plan.items.push_back(ParallelWorkItem{worker, begin, end});
        begin = end;
    }
    return plan;
}

}  // namespace

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

std::size_t parallel_dcfr_worker_count() {
    char* raw_value = nullptr;
#if defined(_MSC_VER)
    std::size_t len = 0;
    if (_dupenv_s(&raw_value, &len, "TEXASSOLVER_PARALLEL_CFR_WORKERS") != 0) {
        raw_value = nullptr;
    }
#else
    raw_value = std::getenv("TEXASSOLVER_PARALLEL_CFR_WORKERS");
#endif
    const auto count = parse_worker_count(raw_value);
#if defined(_MSC_VER)
    if (raw_value != nullptr) {
        free(raw_value);
    }
#endif
    return count;
}

template <class G>
ParallelDCFRSolver<G>::ParallelDCFRSolver(DCFRConfig config, G root, std::size_t worker_count)
    : config_(config), root_(std::move(root)), worker_count_(std::max<std::size_t>(1, worker_count)) {
    validate_alpha(config_.alpha);
    if (config_.beta < 0.0 || config_.gamma < 0.0) {
        throw std::invalid_argument("DCFR beta and gamma must be non-negative");
    }
}

template <class G>
ParallelSolvePlan ParallelDCFRSolver<G>::build_plan() const {
    if (root_.is_terminal()) {
        return make_partition_plan(1, worker_count_);
    }

    const auto branch_count = root_.current_player() < 0
        ? root_.chance_outcomes().size()
        : root_.legal_actions().size();
    return make_partition_plan(std::max<std::size_t>(1, branch_count), worker_count_);
}

template <class G>
void ParallelDCFRSolver<G>::validate_plan(const ParallelSolvePlan& plan) {
    if (plan.worker_count == 0) {
        throw std::logic_error("parallel solve plan requires at least one worker");
    }
    if (plan.items.empty()) {
        throw std::logic_error("parallel solve plan must contain at least one work item");
    }
    if (plan.items.size() != plan.worker_count) {
        throw std::logic_error("parallel solve plan worker count must match work item count");
    }
    for (const auto& item : plan.items) {
        if (item.node_begin >= item.node_end) {
            throw std::logic_error("parallel solve plan item has an empty node range");
        }
    }
}

template <class G>
ParallelWorkerState ParallelDCFRSolver<G>::make_worker_state() const {
    return ParallelWorkerState{};
}

template <class G>
void ParallelDCFRSolver<G>::merge_worker_state(
    std::unordered_map<InfosetKey, detail::InfosetAccum>& canonical,
    ParallelWorkerState worker_state) {
    for (auto& [key, local] : worker_state.accum) {
        auto& target = canonical[key];
        if (target.regret_sum.empty()) {
            target = std::move(local);
            continue;
        }
        if (target.regret_sum.size() != local.regret_sum.size() ||
            target.strategy_sum.size() != local.strategy_sum.size()) {
            throw std::logic_error("parallel worker merge encountered mismatched action counts");
        }
        for (std::size_t i = 0; i < local.regret_sum.size(); ++i) {
            target.regret_sum[i] += local.regret_sum[i];
            target.strategy_sum[i] += local.strategy_sum[i];
        }
    }
}

template <class G>
typename ParallelDCFRSolver<G>::StrategyMap ParallelDCFRSolver<G>::build_strategy_snapshot(
    const std::unordered_map<InfosetKey, detail::InfosetAccum>& canonical) {
    StrategyMap out;
    out.reserve(canonical.size());
    for (const auto& [key, accum] : canonical) {
        if (!accum.regret_sum.empty()) {
            out.emplace(key, detail::normalize_strategy(accum.regret_sum));
        }
    }
    return out;
}

template <class G>
double ParallelDCFRSolver<G>::cfr(
    const G& state,
    PlayerId traversing_player,
    const std::array<double, 2>& reach_probs,
    double chance_reach,
    const StrategyMap& strategy,
    ParallelWorkerState& worker_state) const {
    if (state.is_terminal()) {
        return state.utility().at(static_cast<std::size_t>(traversing_player));
    }

    const PlayerId player = state.current_player();
    if (player < 0) {
        double value = 0.0;
        for (const auto& outcome : state.chance_outcomes()) {
            value += outcome.probability *
                     cfr(state.next_state(outcome.action), traversing_player, reach_probs,
                         chance_reach * outcome.probability, strategy, worker_state);
        }
        return value;
    }

    const auto actions = state.legal_actions();
    const auto key = state.infoset_key(player);
    auto& accum = worker_state.accum[key];
    if (accum.regret_sum.empty()) {
        accum.regret_sum.assign(actions.size(), 0.0);
        accum.strategy_sum.assign(actions.size(), 0.0);
    }

    std::vector<Probability> local_strategy;
    if (const auto locked_it = locked_.find(key);
        locked_it != locked_.end() && locked_it->second.size() == actions.size()) {
        local_strategy = locked_it->second;
    } else if (const auto it = strategy.find(key);
               it != strategy.end() && it->second.size() == actions.size()) {
        local_strategy = it->second;
    } else {
        local_strategy = std::vector<Probability>(actions.size(), 1.0 / static_cast<double>(actions.size()));
    }

    if (player == traversing_player) {
        for (std::size_t i = 0; i < actions.size(); ++i) {
            accum.strategy_sum[i] +=
                chance_reach * reach_probs[static_cast<std::size_t>(player)] * local_strategy[i];
        }
    }

    std::vector<double> action_values(actions.size(), 0.0);
    double node_value = 0.0;
    for (std::size_t i = 0; i < actions.size(); ++i) {
        auto next_reach = reach_probs;
        next_reach[static_cast<std::size_t>(player)] *= local_strategy[i];
        action_values[i] =
            cfr(state.next_state(actions[i]), traversing_player, next_reach, chance_reach, strategy, worker_state);
        node_value += local_strategy[i] * action_values[i];
    }

    if (player == traversing_player && locked_.find(key) == locked_.end()) {
        const PlayerId opponent = 1 - traversing_player;
        const double opponent_reach = chance_reach * reach_probs[static_cast<std::size_t>(opponent)];
        for (std::size_t i = 0; i < actions.size(); ++i) {
            accum.regret_sum[i] += opponent_reach * (action_values[i] - node_value);
        }
    }

    return node_value;
}

template <class G>
typename ParallelDCFRSolver<G>::StrategyMap ParallelDCFRSolver<G>::build_average_strategy() const {
    StrategyMap out;
    out.reserve(infosets_.size());
    for (const auto& [key, accum] : infosets_) {
        if (const auto locked_it = locked_.find(key);
            locked_it != locked_.end() && locked_it->second.size() == accum.strategy_sum.size()) {
            out.emplace(key, locked_it->second);
            continue;
        }
        out.emplace(key, detail::normalize_or_uniform(accum.strategy_sum));
    }
    return out;
}

template <class G>
void ParallelDCFRSolver<G>::set_locked_strategies(
    std::unordered_map<InfosetKey, std::vector<Probability>> locked) {
    locked_ = std::move(locked);
}

template <class G>
SolveOutput ParallelDCFRSolver<G>::solve(std::uint32_t iterations) {
    const auto plan = build_plan();
    validate_plan(plan);
    infosets_.clear();

    const auto root_player = root_.current_player();
    const auto root_actions = root_player >= 0 ? root_.legal_actions() : std::vector<ActionId>{};
    const auto root_outcomes = root_player < 0 ? root_.chance_outcomes() : std::vector<ChanceOutcome>{};
    const bool has_root_actions = root_player >= 0 && !root_actions.empty();

    if (plan.worker_count == 1) {
        for (std::uint32_t iter = 0; iter < iterations; ++iter) {
            for (std::size_t traversing_player = 0; traversing_player < 2; ++traversing_player) {
                const auto strategy = build_strategy_snapshot(infosets_);
                if (root_player < 0) {
                    ParallelWorkerState worker_state = make_worker_state();
                    for (std::size_t i = 0; i < root_outcomes.size(); ++i) {
                        cfr(
                            root_.next_state(root_outcomes[i].action),
                            static_cast<PlayerId>(traversing_player),
                            {1.0, 1.0},
                            root_outcomes[i].probability,
                            strategy,
                            worker_state);
                    }
                    merge_worker_state(infosets_, std::move(worker_state));
                    continue;
                }

                ParallelWorkerState worker_state = make_worker_state();
                std::vector<double> action_values(root_actions.size(), 0.0);
                for (std::size_t i = 0; i < root_actions.size(); ++i) {
                    action_values[i] =
                        cfr(
                            root_.next_state(root_actions[i]),
                            static_cast<PlayerId>(traversing_player),
                            {1.0, 1.0},
                            1.0,
                            strategy,
                            worker_state);
                }
                merge_worker_state(infosets_, std::move(worker_state));
            }
        }

        const auto average_strategy = build_average_strategy();
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

    for (std::uint32_t iter = 0; iter < iterations; ++iter) {
        for (std::size_t traversing_player = 0; traversing_player < 2; ++traversing_player) {
            const auto strategy = build_strategy_snapshot(infosets_);
            std::vector<BranchResult> results(plan.items.size());
            std::mutex mutex;
            std::exception_ptr worker_error;

            auto launch_worker = [&](std::size_t worker_index) {
                return std::thread([&, worker_index] {
                    try {
                        auto& result = results[worker_index];
                        result.worker_state = make_worker_state();
                        const auto& item = plan.items[worker_index];
                        result.branch_values.resize(item.node_end - item.node_begin, 0.0);
                        if (root_player < 0) {
                            for (std::size_t i = item.node_begin; i < item.node_end; ++i) {
                                const auto& outcome = root_outcomes[i];
                                result.branch_values[i - item.node_begin] =
                                    outcome.probability *
                                    cfr(
                                        root_.next_state(outcome.action),
                                        static_cast<PlayerId>(traversing_player),
                                        {1.0, 1.0},
                                        outcome.probability,
                                        strategy,
                                        result.worker_state);
                            }
                        } else if (has_root_actions) {
                            for (std::size_t i = item.node_begin; i < item.node_end; ++i) {
                                result.branch_values[i - item.node_begin] =
                                    cfr(
                                        root_.next_state(root_actions[i]),
                                        static_cast<PlayerId>(traversing_player),
                                        {1.0, 1.0},
                                        1.0,
                                        strategy,
                                        result.worker_state);
                            }
                        }
                    } catch (...) {
                        std::lock_guard<std::mutex> lock(mutex);
                        worker_error = std::current_exception();
                    }
                });
            };

            std::vector<std::thread> workers;
            workers.reserve(plan.items.size());
            for (std::size_t i = 0; i < plan.items.size(); ++i) {
                workers.emplace_back(launch_worker(i));
            }

            for (auto& worker : workers) {
                worker.join();
            }
            if (worker_error) {
                std::rethrow_exception(worker_error);
            }

            for (const auto& item : plan.items) {
                merge_worker_state(infosets_, std::move(results[item.root_node].worker_state));
            }
        }
    }

    const auto average_strategy = build_average_strategy();
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
