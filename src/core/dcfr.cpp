#include "core/dcfr.hpp"

namespace core {

template <class G>
DCFRSolver<G>::DCFRSolver(DCFRConfig config) : config_(config) {}

template <class G>
void DCFRSolver<G>::set_locked_strategies(
    std::unordered_map<InfosetKey, std::vector<Probability>> locked) {
    locked_ = std::move(locked);
}

template <class G>
SolveOutput DCFRSolver<G>::solve(std::uint32_t iterations) {
    (void)config_;
    (void)locked_;
    SolveOutput out;
    out.iterations = iterations;
    return out;
}

// Explicit instantiation will be added once concrete game types exist.

}  // namespace core

