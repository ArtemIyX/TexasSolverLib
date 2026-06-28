#pragma once

#include "solver/dcfr.hpp"

#include <cstdint>
#include <unordered_map>
#include <vector>

namespace core {

bool parallel_dcfr_enabled();
std::size_t parallel_dcfr_worker_count();

struct ParallelWorkItem {
    std::size_t root_node = 0;
    std::size_t node_begin = 0;
    std::size_t node_end = 0;
};

struct ParallelSolvePlan {
    bool enabled = false;
    std::size_t worker_count = 1;
    std::vector<ParallelWorkItem> items;
};

struct ParallelWorkerState {
    std::unordered_map<InfosetKey, detail::InfosetAccum> accum;
};

template <class G>
class ParallelDCFRSolver : public DCFRSolverBase {
public:
    explicit ParallelDCFRSolver(DCFRConfig config, G root = G::initial());

    void set_locked_strategies(std::unordered_map<InfosetKey, std::vector<Probability>> locked);
    SolveOutput solve(std::uint32_t iterations);

private:
    ParallelSolvePlan build_plan() const;
    static void validate_plan(const ParallelSolvePlan& plan);
    ParallelWorkerState make_worker_state() const;
    static void merge_worker_state(
        std::unordered_map<InfosetKey, detail::InfosetAccum>& canonical,
        ParallelWorkerState worker_state);
    DCFRConfig config_;
    G root_;
    std::unordered_map<InfosetKey, std::vector<Probability>> locked_;
};

}  // namespace core
