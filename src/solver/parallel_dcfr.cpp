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
    plan.enabled = false;
    plan.worker_count = 1;
    plan.items.push_back(ParallelWorkItem{0, 0, 0});
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

template <class G>
ParallelDCFRSolver<G>::ParallelDCFRSolver(DCFRConfig config, G root)
    : config_(config), root_(std::move(root)) {
    (void)config_;
    (void)root_;
}

template <class G>
ParallelSolvePlan ParallelDCFRSolver<G>::build_plan() const {
    return make_single_item_plan();
}

template <class G>
void ParallelDCFRSolver<G>::set_locked_strategies(
    std::unordered_map<InfosetKey, std::vector<Probability>> locked) {
    locked_ = std::move(locked);
}

template <class G>
SolveOutput ParallelDCFRSolver<G>::solve(std::uint32_t iterations) {
    const auto plan = build_plan();
    (void)plan;
    DCFRSolver<G> solver(config_, root_);
    solver.set_locked_strategies(std::move(locked_));
    return solver.solve(iterations);
}

template class ParallelDCFRSolver<KuhnState>;
template class ParallelDCFRSolver<LeducState>;
template class ParallelDCFRSolver<HUNLState>;

}  // namespace core
