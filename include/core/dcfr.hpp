#pragma once

#include "core/core.hpp"
#include "core/game.hpp"

#include <unordered_map>

namespace core {

struct DCFRConfig {
    double alpha = 1.5;
    double beta = 0.0;
    double gamma = 2.0;
};

class DCFRSolverBase {
public:
    virtual ~DCFRSolverBase() = default;
};

template <class G>
class DCFRSolver : public DCFRSolverBase {
public:
    explicit DCFRSolver(DCFRConfig config);

    void set_locked_strategies(std::unordered_map<InfosetKey, std::vector<Probability>> locked);
    SolveOutput solve(std::uint32_t iterations);

private:
    DCFRConfig config_;
    std::unordered_map<InfosetKey, std::vector<Probability>> locked_;
};

}  // namespace core

