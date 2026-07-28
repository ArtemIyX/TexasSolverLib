#pragma once

#include "util/abstraction.hpp"
#include "util/api.hpp"
#include "solver/dcfr.hpp"
#include "solver/hunl_flat_dcfr.hpp"
#include "solver/hunl_bucket_map.hpp"
#include "solver/hunl_bucket_terminal.hpp"
#include "solver/hunl_sampled_solver.hpp"
#include "solver/hunl_sampled_range.hpp"
#include "solver/multiway_cfr.hpp"
#include "solver/multiway_blueprint_config.hpp"
#include "solver/multiway_action_abstraction.hpp"
#include "solver/multiway_bucket_model.hpp"
#include "solver/multiway_memory.hpp"
#include "solver/multiway_model_identity.hpp"
#include "solver/multiway_public_builder.hpp"
#include "solver/multiway_solver.hpp"
#include "solver/multiway_terminal_adapter.hpp"
#include "solver/dcfr_vector.hpp"
#include "solver/dcfr_vector_parallel.hpp"
#include "solver/exploit.hpp"
#include "games/hunl.hpp"
#include "games/hunl_eval.hpp"
#include "games/multiway_state.hpp"
#include "games/multiway_fixed.hpp"
#include "games/multiway_terminal.hpp"
#include "games/multiway_private.hpp"
#include "games/hunl_solver.hpp"
#include "games/hunl_tree.hpp"
#include "games/kuhn.hpp"
#include "util/layout.hpp"
#include "games/leduc.hpp"
#include "util/pcs.hpp"
#include "preflop/preflop.hpp"
#include "preflop/preflop_equity.hpp"
#include "preflop/preflop_rvr.hpp"
#include "util/simd.hpp"
#include "solver/solver.hpp"
#include "util/suit_iso.hpp"

#include <memory>
#include <unordered_map>

namespace core::lib {

/**
 * @brief Stable convenience aliases for external consumers.
 */
using ::core::ActionId;
using ::core::ChanceOutcome;
using ::core::Class169RvrOutput;
using ::core::ExploitOutput;
using ::core::HUNLConfig;
using ::core::HUNLLeafEvaluationRequest;
using ::core::HUNLLeafEvaluationResult;
using ::core::HUNLLeafEvaluationScope;
using ::core::HUNLLeafEvaluator;
using ::core::HUNLLeafValueUnits;
using ::core::HUNLLiveRootSnapshot;
using ::core::HUNLSolveOutput;
using ::core::HUNLSampledSolveResult;
using ::core::HUNLSampledSolverConfig;
using ::core::HUNLSampledRangeSession;
using ::core::HUNLStructuredRootRequest;
using ::core::HUNLQualityMetric;
using ::core::InfosetKey;
using ::core::MultiwayAction;
using ::core::MultiwayActionAbstraction;
using ::core::MultiwayActionAbstractionConfig;
using ::core::MultiwayBucketTable;
using ::core::MultiwayMemoryBudget;
using ::core::MultiwayMemoryEstimate;
using ::core::MultiwayMemoryPreflight;
using ::core::MultiwayMemoryStatus;
using ::core::preflight_multiway_memory;
using ::core::MultiwayBlueprintConfig;
using ::core::MultiwayModelIdentity;
using ::core::MultiwayPublicBuilder;
using ::core::make_multiway_model_identity;
using ::core::MultiwayActionAbstractionIdentity;
using ::core::MultiwayBettingSnapshot;
using ::core::MultiwayBoardChanceEdge;
using ::core::MultiwayBoardRunoutState;
using ::core::MultiwayCFRAlgorithm;
using ::core::MultiwayCFRConfig;
using ::core::MultiwayCFRUpdate;
using ::core::MultiwayExternalSamplingRequest;
using ::core::make_multiway_external_sampling_request;
using ::core::MultiwayGameConfig;
using ::core::MultiwayFixedActionMenu;
using ::core::MultiwayFixedSidePot;
using ::core::MultiwayFixedState;
using ::core::MultiwayFixedTerminalInput;
using ::core::MultiwayFixedTerminalResult;
using ::core::MultiwayFixedTerminalScratch;
using ::core::make_multiway_fixed_state;
using ::core::settle_multiway_terminal_fixed;
using ::core::MultiwayMetricMethod;
using ::core::MultiwayNashConv;
using ::core::MultiwayOddChipRule;
using ::core::MultiwayActionDescriptor;
using ::core::MultiwayInfosetId;
using ::core::MultiwayJointPrivateSample;
using ::core::MultiwayCompiledPrivateRanges;
using ::core::MultiwayPrivateWorkerScratch;
using ::core::MultiwayPotLayout;
using ::core::MultiwayRakeMode;
using ::core::MultiwayRakePolicy;
using ::core::MultiwayPrivateConfig;
using ::core::MultiwayPrivateRangeFeasibilityResult;
using ::core::MultiwayPrivateRangeFeasibilityStatus;
using ::core::preflight_multiway_private_range_feasibility;
using ::core::MultiwaySidePot;
using ::core::MultiwayShowdownInput;
using ::core::MultiwayState;
using ::core::MultiwayTerminalInput;
using ::core::MultiwayTerminalAdapter;
using ::core::MultiwaySamplerDealToken;
using ::core::MultiwayTerminalResult;
using ::core::MultiwayStreetTransition;
using ::core::MultiwayPublicStreetTransition;
using ::core::MultiwayQualityMetric;
using ::core::MultiwayQualityDiagnostics;
using ::core::MultiwayPublicHistoryEntry;
using ::core::MultiwayPublicBoardChanceEdge;
using ::core::MultiwayPublicParentEdge;
using ::core::MultiwayPublicParentEdgeKind;
using ::core::MultiwayPublicStateDescriptor;
using ::core::MultiwayPublicStateId;
using ::core::MultiwayRootActionProbability;
using ::core::MultiwayRootPolicy;
using ::core::MultiwayRootSnapshot;
using ::core::MultiwaySolveDiagnostics;
using ::core::MultiwaySolveRequest;
using ::core::MultiwaySolveResult;
using ::core::MultiwaySolverCoordinator;
using ::core::MultiwaySolverLimits;
using ::core::MultiwaySparseRowMetadata;
using ::core::MultiwaySparseRowShape;
using ::core::MultiwaySparseRowStorage;
using ::core::MultiwayValueUnits;
using ::core::MultiwayWorkerDelta;
using ::core::MultiwayWorkerDeltaStream;
using ::core::MultiwayWeightedHole;
using ::core::PlayerId;
using ::core::PreflopRvrOutput;
using ::core::PreflopSolveOutput;
using ::core::Probability;
using ::core::SolveOutput;
using ::core::Value;
using ::core::VectorSolveOutput;

// Production-facing range/blueprint entry point. The fixed-hand
// solve_hunl_postflop overload remains the exact small-game oracle; it does
// not accept range roots.
inline HUNLSampledSolveResult solve_hunl_postflop_sampled(
    const HUNLStructuredRootRequest& root,
    HUNLSampledSolverConfig config = {},
    std::uint32_t batches = 1,
    const HUNLLeafEvaluator* leaf_evaluator = nullptr) {
    root.validate();
    HUNLSampledSolver solver(config);
    HUNLSampledSolveRequest request;
    request.structured_root = root;
    request.leaf_evaluator = leaf_evaluator;
    return solver.run_batches(request, batches);
}

/**
 * @brief Solve Kuhn poker through the library facade.
 */
inline SolveOutput solve_kuhn(
    std::uint32_t iterations,
    double alpha,
    double beta,
    double gamma,
    std::size_t workers = 1,
    std::size_t frontier_multiplier = 8) {
    return ::core::solve_kuhn(iterations, alpha, beta, gamma, workers, frontier_multiplier);
}

/**
 * @brief Solve Leduc poker through the library facade.
 */
inline SolveOutput solve_leduc(
    std::uint32_t iterations,
    double alpha,
    double beta,
    double gamma,
    std::size_t workers = 1,
    std::size_t frontier_multiplier = 8) {
    return ::core::solve_leduc(iterations, alpha, beta, gamma, workers, frontier_multiplier);
}

inline HUNLSolveOutput solve_hunl_postflop(
    const HUNLConfig& config,
    std::uint32_t iterations,
    double alpha,
    double beta,
    double gamma,
    std::size_t workers = 1,
    std::size_t frontier_multiplier = 8,
    bool force_parallel = false) {
    return ::core::solve_hunl_postflop(
        config, iterations, alpha, beta, gamma, workers, frontier_multiplier, force_parallel);
}

inline PreflopSolveOutput solve_hunl_preflop(
    const HUNLConfig& config,
    std::uint32_t iterations,
    double alpha,
    double beta,
    double gamma) {
    return ::core::solve_hunl_preflop(config, iterations, alpha, beta, gamma);
}

inline ExploitOutput compute_exploitability(
    const HUNLConfig& config,
    const std::unordered_map<std::string, std::vector<double>>& strategy) {
    return ::core::compute_exploitability_and_value(config, strategy);
}

inline double compute_restricted_game_value(
    const HUNLConfig& config,
    const std::unordered_map<std::string, std::vector<double>>& strategy,
    const std::vector<std::array<std::uint8_t, 2>>& p0_holes,
    const std::vector<std::array<std::uint8_t, 2>>& p1_holes) {
    return ::core::compute_restricted_game_value(config, strategy, p0_holes, p1_holes);
}

inline VectorSolveOutput solve_range_vs_range_rust(
    const HUNLConfig& config,
    const std::vector<std::array<std::array<std::uint8_t, 2>, 2>>& hole_pairs,
    std::uint32_t iterations,
    double alpha,
    double beta,
    double gamma) {
    return ::core::solve_vector_dcfr(
        ::core::BettingTree::build_from(::core::HUNLState::initial(std::make_shared<const HUNLConfig>(config))),
        hole_pairs,
        iterations,
        alpha,
        beta,
        gamma);
}

inline PreflopRvrOutput solve_hunl_preflop_rvr(
    const HUNLConfig& config,
    const PreflopEquityTable& table,
    std::uint32_t iterations,
    double alpha,
    double beta,
    double gamma) {
    return ::core::solve_hunl_preflop_rvr(config, table, iterations, alpha, beta, gamma);
}

inline Class169RvrOutput solve_hunl_preflop_rvr_class169(
    const HUNLConfig& config,
    const PreflopEquityTable& table,
    std::vector<double> root_reach_p0,
    std::vector<double> root_reach_p1,
    std::uint32_t iterations,
    double alpha,
    double beta,
    double gamma) {
    return ::core::solve_hunl_preflop_rvr_class169(
        config,
        table,
        std::move(root_reach_p0),
        std::move(root_reach_p1),
        iterations,
        alpha,
        beta,
        gamma);
}

}  // namespace core::lib


