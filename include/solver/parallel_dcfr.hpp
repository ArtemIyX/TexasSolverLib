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
    using StrategyMap = std::unordered_map<InfosetKey, std::vector<Probability>>;
    using AccumMap = std::unordered_map<InfosetKey, detail::InfosetAccum>;

    double cfr(
        const G& state,
        PlayerId traversing_player,
        const std::array<double, 2>& reach_probs,
        double chance_reach,
        const StrategyMap& strategy,
        AccumMap& accum);
    StrategyMap build_average_strategy(const AccumMap& accum) const;
    void validate_config() const;
    static StrategyMap build_strategy_snapshot(const AccumMap& accum);
    static void merge_accum(AccumMap& dst, const AccumMap& src);

    DCFRConfig config_;
    G root_;
    std::unordered_map<InfosetKey, std::vector<Probability>> locked_;
    AccumMap infosets_;
};

}  // namespace core

