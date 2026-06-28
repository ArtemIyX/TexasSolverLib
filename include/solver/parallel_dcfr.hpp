#pragma once

#include "solver/dcfr.hpp"

#include <cstdint>
#include <unordered_map>
#include <vector>

namespace core {

bool parallel_dcfr_enabled();

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

template <class G>
class ParallelDCFRSolver : public DCFRSolverBase {
public:
    explicit ParallelDCFRSolver(DCFRConfig config, G root = G::initial());

    void set_locked_strategies(std::unordered_map<InfosetKey, std::vector<Probability>> locked);
    SolveOutput solve(std::uint32_t iterations);

private:
    ParallelSolvePlan build_plan() const;
    DCFRConfig config_;
    G root_;
    std::unordered_map<InfosetKey, std::vector<Probability>> locked_;
};

}  // namespace core
