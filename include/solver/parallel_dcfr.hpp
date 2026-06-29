#pragma once

#include "solver/dcfr.hpp"

#include <cstdint>
#include <memory>
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
    detail::InfosetAccumTable accum;
};

template <class G>
class ParallelDCFRSolver : public DCFRSolverBase {
public:
    explicit ParallelDCFRSolver(
        DCFRConfig config,
        G root = G::initial(),
        std::size_t worker_count = 1,
        std::size_t frontier_multiplier = 8);

    void set_locked_strategies(std::unordered_map<InfosetKey, std::vector<Probability>> locked);
    SolveOutput solve(std::uint32_t iterations);

private:
    using StrategyMap = std::unordered_map<InfosetId, std::vector<Probability>>;
    using SharedStrategyMap = std::shared_ptr<const StrategyMap>;

    ParallelSolvePlan build_plan() const;
    static void validate_plan(const ParallelSolvePlan& plan);
    ParallelWorkerState make_worker_state() const;
    static void merge_worker_state(
        detail::InfosetAccumTable& canonical,
        ParallelWorkerState worker_state);
    static SharedStrategyMap build_strategy_snapshot(
        const detail::InfosetAccumTable& canonical);
    double cfr(
        const G& state,
        PlayerId traversing_player,
        const std::array<double, 2>& reach_probs,
        double chance_reach,
        const StrategyMap& strategy,
        ParallelWorkerState& worker_state);
    StrategyMap build_average_strategy() const;
    DCFRConfig config_;
    G root_;
    std::size_t worker_count_ = 1;
    std::size_t frontier_multiplier_ = 8;
    std::unordered_map<InfosetKey, std::vector<Probability>> locked_;
    InfosetRegistry registry_;
    std::unordered_map<InfosetId, std::vector<Probability>> locked_by_id_;
    detail::InfosetAccumTable infosets_;
};

}  // namespace core
