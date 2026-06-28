#pragma once

#include "solver/dcfr.hpp"

#include <cstdint>
#include <unordered_map>

namespace core {

bool parallel_dcfr_enabled();

template <class G>
class ParallelDCFRSolver : public DCFRSolverBase {
public:
    explicit ParallelDCFRSolver(DCFRConfig config, G root = G::initial());

    void set_locked_strategies(std::unordered_map<InfosetKey, std::vector<Probability>> locked);
    SolveOutput solve(std::uint32_t iterations);

private:
    DCFRConfig config_;
    G root_;
    std::unordered_map<InfosetKey, std::vector<Probability>> locked_;
};

}  // namespace core
