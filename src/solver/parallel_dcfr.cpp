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
#include <condition_variable>
#include <atomic>
#include <string>
#include <utility>

namespace core {

namespace {

struct WorkBatch {
    std::size_t begin = 0;
    std::size_t end = 0;
};

template <class G>
struct FrontierSeed {
    G state;
    double chance_reach = 1.0;
};

template <class G>
std::size_t child_count(const G& state) {
    if (state.is_terminal()) {
        return 0;
    }
    if (state.current_player() < 0) {
        return state.chance_outcomes().size();
    }
    return state.legal_actions().size();
}

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
ParallelDCFRSolver<G>::ParallelDCFRSolver(
    DCFRConfig config,
    G root,
    std::size_t worker_count,
    std::size_t frontier_multiplier)
    : config_(config),
      root_(std::move(root)),
      worker_count_(std::max<std::size_t>(1, worker_count)),
      frontier_multiplier_(std::max<std::size_t>(1, frontier_multiplier)) {
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
std::vector<FrontierSeed<G>> build_frontier(const G& root, std::size_t target_seeds) {
    std::vector<FrontierSeed<G>> frontier;
    if (root.is_terminal()) {
        frontier.push_back(FrontierSeed<G>{root, 1.0});
        return frontier;
    }

    if (root.current_player() < 0) {
        const auto outcomes = root.chance_outcomes();
        frontier.reserve(std::max<std::size_t>(outcomes.size(), target_seeds));
        for (const auto& outcome : outcomes) {
            frontier.push_back(FrontierSeed<G>{
                root.next_state(outcome.action),
                outcome.probability});
        }
    } else {
        const auto actions = root.legal_actions();
        frontier.reserve(std::max<std::size_t>(actions.size(), target_seeds));
        for (const auto action : actions) {
            frontier.push_back(FrontierSeed<G>{root.next_state(action), 1.0});
        }
    }

    if (frontier.empty()) {
        frontier.push_back(FrontierSeed<G>{root, 1.0});
        return frontier;
    }

    while (frontier.size() < target_seeds) {
        std::size_t best_index = frontier.size();
        std::size_t best_children = 0;

        for (std::size_t i = 0; i < frontier.size(); ++i) {
            const auto& state = frontier[i].state;
            const auto branch_count = child_count(state);

            if (branch_count > best_children) {
                best_children = branch_count;
                best_index = i;
            }
        }

        if (best_index == frontier.size() || best_children <= 1) {
            break;
        }

        const auto seed = frontier[best_index];
        frontier.erase(frontier.begin() + static_cast<std::ptrdiff_t>(best_index));
        const auto& state = seed.state;
        if (state.current_player() < 0) {
            for (const auto& outcome : state.chance_outcomes()) {
                frontier.push_back(FrontierSeed<G>{
                    state.next_state(outcome.action),
                    seed.chance_reach * outcome.probability});
            }
        } else {
            for (const auto action : state.legal_actions()) {
                frontier.push_back(FrontierSeed<G>{state.next_state(action), seed.chance_reach});
            }
        }
    }

    return frontier;
}

std::size_t frontier_seed_target(std::size_t worker_count, std::size_t frontier_multiplier) {
    return std::max<std::size_t>(1, worker_count * frontier_multiplier);
}

std::vector<WorkBatch> make_work_batches(
    std::size_t seed_count,
    std::size_t worker_count,
    std::size_t frontier_multiplier) {
    const std::size_t target_batches = std::max<std::size_t>(1, std::min(seed_count, worker_count * 2));
    const std::size_t min_batch_size =
        std::max<std::size_t>(1, frontier_multiplier / 2);
    const std::size_t batch_size =
        std::max<std::size_t>(min_batch_size, (seed_count + target_batches - 1) / target_batches);

    std::vector<WorkBatch> batches;
    for (std::size_t begin = 0; begin < seed_count; begin += batch_size) {
        batches.push_back(WorkBatch{begin, std::min(seed_count, begin + batch_size)});
    }
    return batches;
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
    const auto frontier = build_frontier(root_, frontier_seed_target(plan.worker_count, frontier_multiplier_));
    const auto batches = make_work_batches(frontier.size(), plan.worker_count, frontier_multiplier_);
    if (frontier.empty()) {
        SolveOutput out;
        out.iterations = iterations;
        return out;
    }

    if (plan.worker_count == 1) {
        for (std::uint32_t iter = 0; iter < iterations; ++iter) {
            for (std::size_t traversing_player = 0; traversing_player < 2; ++traversing_player) {
                const auto strategy = build_strategy_snapshot(infosets_);
                ParallelWorkerState worker_state = make_worker_state();
                if (root_player < 0) {
                    for (const auto& outcome : root_.chance_outcomes()) {
                        cfr(
                            root_.next_state(outcome.action),
                            static_cast<PlayerId>(traversing_player),
                            {1.0, 1.0},
                            outcome.probability,
                            strategy,
                            worker_state);
                    }
                } else {
                    for (const auto action : root_.legal_actions()) {
                        cfr(
                            root_.next_state(action),
                            static_cast<PlayerId>(traversing_player),
                            {1.0, 1.0},
                            1.0,
                            strategy,
                            worker_state);
                    }
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

    struct WorkerPoolState {
        std::mutex mutex;
        std::condition_variable cv;
        std::size_t phase_id = 0;
        std::size_t completed = 0;
        std::atomic<std::size_t> next_batch{0};
        std::size_t traversing_player = 0;
        bool stop = false;
        std::unordered_map<InfosetKey, std::vector<Probability>> strategy;
        std::vector<ParallelWorkerState> results;
        std::exception_ptr error;
    };

    WorkerPoolState pool;
    pool.results.resize(plan.worker_count);
    std::vector<std::size_t> merge_order(plan.worker_count);
    for (std::size_t i = 0; i < merge_order.size(); ++i) {
        merge_order[i] = i;
    }

    auto worker_fn = [&](std::size_t worker_index) {
        std::size_t seen_phase = 0;
        while (true) {
            std::unique_lock<std::mutex> lock(pool.mutex);
            pool.cv.wait(lock, [&] { return pool.stop || pool.phase_id != seen_phase; });
            if (pool.stop) {
                return;
            }

            const auto local_phase = pool.phase_id;
            const auto local_player = pool.traversing_player;
            const auto local_strategy = pool.strategy;
            lock.unlock();

            bool published = false;
            try {
                auto local_state = make_worker_state();
                for (;;) {
                    const auto batch_index = pool.next_batch.fetch_add(1, std::memory_order_relaxed);
                    if (batch_index >= batches.size()) {
                        break;
                    }
                    const auto batch = batches[batch_index];
                    for (std::size_t i = batch.begin; i < batch.end; ++i) {
                        const auto& seed = frontier[i];
                        cfr(
                            seed.state,
                            static_cast<PlayerId>(local_player),
                            {1.0, 1.0},
                            seed.chance_reach,
                            local_strategy,
                            local_state);
                    }
                }
                {
                    std::lock_guard<std::mutex> guard(pool.mutex);
                    pool.results[worker_index] = std::move(local_state);
                }
                published = true;
            } catch (...) {
                std::lock_guard<std::mutex> guard(pool.mutex);
                if (!pool.error) {
                    pool.error = std::current_exception();
                }
            }

            lock.lock();
            seen_phase = local_phase;
            if (published && ++pool.completed == plan.worker_count) {
                pool.cv.notify_all();
            }
            if (!published && pool.error) {
                pool.cv.notify_all();
            }
        }
    };

    std::vector<std::thread> workers;
    workers.reserve(plan.worker_count);
    for (std::size_t i = 0; i < plan.worker_count; ++i) {
        workers.emplace_back(worker_fn, i);
    }

    for (std::uint32_t iter = 0; iter < iterations; ++iter) {
        for (std::size_t traversing_player = 0; traversing_player < 2; ++traversing_player) {
            {
                std::lock_guard<std::mutex> lock(pool.mutex);
                pool.traversing_player = traversing_player;
                pool.strategy = build_strategy_snapshot(infosets_);
                pool.completed = 0;
                pool.next_batch.store(0, std::memory_order_relaxed);
                pool.error = nullptr;
                ++pool.phase_id;
            }
            pool.cv.notify_all();

            {
                std::unique_lock<std::mutex> lock(pool.mutex);
                pool.cv.wait(lock, [&] {
                    return pool.completed == plan.worker_count || pool.error != nullptr;
                });
                if (pool.error) {
                    pool.stop = true;
                    pool.cv.notify_all();
                    lock.unlock();
                    for (auto& worker : workers) {
                        worker.join();
                    }
                    std::rethrow_exception(pool.error);
                }
            }

            for (const auto worker_index : merge_order) {
                merge_worker_state(infosets_, std::move(pool.results[worker_index]));
            }
        }
    }

    {
        std::lock_guard<std::mutex> lock(pool.mutex);
        pool.stop = true;
    }
    pool.cv.notify_all();
    for (auto& worker : workers) {
        worker.join();
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
