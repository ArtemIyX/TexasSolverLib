#include "solver/parallel_dcfr.hpp"

#include "games/hunl.hpp"
#include "games/kuhn.hpp"
#include "games/leduc.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <string>
#include <utility>

namespace core {

namespace {

ParallelSolvePlan make_single_item_plan() {
    ParallelSolvePlan plan;
    plan.enabled = parallel_dcfr_enabled();
    plan.worker_count = parallel_dcfr_worker_count();
    plan.items.reserve(plan.worker_count);
    for (std::size_t worker = 0; worker < plan.worker_count; ++worker) {
        plan.items.push_back(ParallelWorkItem{worker, worker, worker + 1});
    }
    return plan;
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
ParallelDCFRSolver<G>::ParallelDCFRSolver(DCFRConfig config, G root)
    : config_(config), root_(std::move(root)) {
    (void)config_;
    (void)root_;
}

template <class G>
ParallelSolvePlan ParallelDCFRSolver<G>::build_plan() const {
    (void)config_;
    (void)root_;
    (void)locked_;
    return make_single_item_plan();
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
void ParallelDCFRSolver<G>::set_locked_strategies(
    std::unordered_map<InfosetKey, std::vector<Probability>> locked) {
    locked_ = std::move(locked);
}

template <class G>
SolveOutput ParallelDCFRSolver<G>::solve(std::uint32_t iterations) {
    const auto plan = build_plan();
    validate_plan(plan);
    auto worker_state = make_worker_state();
    (void)worker_state;
    DCFRSolver<G> solver(config_, root_);
    solver.set_locked_strategies(std::move(locked_));
    return solver.solve(iterations);
}

template class ParallelDCFRSolver<KuhnState>;
template class ParallelDCFRSolver<LeducState>;
template class ParallelDCFRSolver<HUNLState>;

}  // namespace core
