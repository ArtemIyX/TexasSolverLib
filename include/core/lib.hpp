#pragma once

#include "util/abstraction.hpp"
#include "util/api.hpp"
#include "core/canonical_combo.hpp"
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
#include "solver/multiway_bucket_artifact.hpp"
#include "solver/multiway_memory.hpp"
#include "solver/multiway_scheduler.hpp"
#include "solver/multiway_traversal.hpp"
#include "solver/multiway_leaf_evaluator.hpp"
#include "solver/multiway_continuation_policy.hpp"
#include "solver/multiway_rollout_leaf.hpp"
#include "solver/multiway_export.hpp"
#include "solver/multiway_blueprint_trainer.hpp"
#include "solver/multiway_blueprint_store.hpp"
#include "solver/multiway_blueprint_policy_provider.hpp"
#include "solver/multiway_blueprint_query.hpp"
#include "solver/multiway_range_update.hpp"
#include "solver/multiway_range_belief.hpp"
#include "solver/multiway_search_session.hpp"
#include "solver/multiway_runtime_session.hpp"
#include "solver/multiway_checkpoint.hpp"
#include "solver/multiway_artifact.hpp"
#include "solver/multiway_model_identity.hpp"
#include "solver/multiway_resolver.hpp"
#include "solver/multiway_baseline.hpp"
#include "solver/multiway_public_builder.hpp"
#include "solver/multiway_solver.hpp"
#include "solver/multiway_terminal_adapter.hpp"
#include "solver/dcfr_vector.hpp"
#include "solver/dcfr_vector_parallel.hpp"
#include "solver/exploit.hpp"
#include "games/hunl.hpp"
#include "games/hunl_eval.hpp"
#include "games/multiway_state.hpp"
#include "games/multiway_rules.hpp"
#include "games/multiway_replay.hpp"
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
using ::core::CanonicalComboCards;
using ::core::CanonicalComboId;
using ::core::CanonicalComboLegalMask;
using ::core::CanonicalComboView;
using ::core::canonical_combos;
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
using ::core::MultiwayActionAbstractionContext;
using ::core::MultiwayActionTranslation;
using ::core::MultiwayActionTranslationStatus;
using ::core::MultiwayPreflopSituation;
using ::core::MultiwayRelativePosition;
using ::core::MultiwayPostflopSizingMode;
using ::core::MultiwayBucketTable;
using ::core::MultiwayBucketRegistry;
using ::core::MultiwayBucketBaselineProfile;
using ::core::MultiwayBucketFeatures;
using ::core::MultiwayBucketBoardRequest;
using ::core::build_multiway_baseline_bucket_table;
using ::core::build_multiway_baseline_bucket_registry;
using ::core::deserialize_multiway_bucket_registry;
using ::core::serialize_multiway_bucket_registry;
using ::core::validate_multiway_bucket_coverage;
using ::core::MultiwayMemoryBudget;
using ::core::MultiwayMemoryEstimate;
using ::core::MultiwayMemoryPreflight;
using ::core::MultiwayMemoryStatus;
using ::core::preflight_multiway_memory;
using ::core::MultiwayScheduler;
using ::core::MultiwayTrajectoryRange;
using ::core::MultiwayWorkerBatch;
using ::core::MultiwayExternalSamplingTraversal;
using ::core::MultiwayRootBatchResult;
using ::core::MultiwayRootBatchRunner;
using ::core::MultiwayLeafEvaluationRequest;
using ::core::MultiwayLeafEvaluator;
using ::core::MultiwayContinuationPolicyKind;
using ::core::MultiwayFixedContinuationPolicy;
using ::core::MultiwayContinuationLeafContext;
using ::core::MultiwayContinuationLeafData;
using ::core::make_multiway_fixed_continuation_leaf_evaluator;
using ::core::MultiwayRolloutActionMenu;
using ::core::MultiwayRolloutActionProviderFn;
using ::core::MultiwayRolloutInput;
using ::core::MultiwayRolloutInputProviderFn;
using ::core::MultiwayRolloutLeafContext;
using ::core::MultiwayRolloutLimits;
using ::core::MultiwayRolloutProfileResult;
using ::core::MultiwayRolloutScratch;
using ::core::MultiwayRolloutStatus;
using ::core::evaluate_multiway_rollout_profiles;
using ::core::make_multiway_rollout_leaf_evaluator;
using ::core::MultiwayBlueprintSnapshot;
using ::core::MultiwayBlueprintPolicyKind;
using ::core::MultiwayBlueprintTrainingMetadata;
using ::core::export_multiway_root_snapshot;
using ::core::MultiwayBlueprintTrainer;
using ::core::MultiwayBlueprintTrainingConfig;
using ::core::MultiwayBlueprintTrainingSession;
using ::core::MultiwayBlueprintTrainingStatus;
using ::core::MultiwayBlueprintTrainingCheckpoint;
using ::core::MultiwayBlueprintIterationSchedule;
using ::core::MultiwayBlueprintCoverageManifest;
using ::core::MultiwayBlueprintRow;
using ::core::MultiwayBlueprintStore;
using ::core::MultiwayBlueprintPolicyProvider;
using ::core::MultiwayBlueprintLookupStatus;
using ::core::MultiwayBlueprintQuery;
using ::core::MultiwayBucketActionPolicy;
using ::core::MultiwayRangeBeliefMetadata;
using ::core::MultiwayRangeBeliefObservation;
using ::core::MultiwayRangeBeliefSeatInput;
using ::core::MultiwayRangeBeliefSource;
using ::core::MultiwayRangeBeliefSuppliedEntry;
using ::core::MultiwayRangeBeliefUpdateResult;
using ::core::MultiwayRangeBeliefView;
using ::core::MultiwayRangeBeliefs;
using ::core::MultiwaySearchSession;
using ::core::MultiwaySearchSessionCleanSnapshot;
using ::core::MultiwaySearchSessionDependencies;
using ::core::MultiwaySearchSessionRootMetadata;
using ::core::MultiwaySearchSessionRowView;
using ::core::MultiwaySearchSessionHeroRow;
using ::core::MultiwaySearchSessionHeroPolicy;
using ::core::MultiwayRuntimeSession;
using ::core::update_anonymous_multiway_range;
using ::core::MultiwayCheckpoint;
using ::core::MultiwayArtifactSource;
using ::core::MultiwayBlueprintArtifacts;
using ::core::MultiwayFullBlueprintArtifact;
using ::core::MultiwayFullBlueprintArtifacts;
using ::core::MultiwayBlueprintManifest;
using ::core::MultiwayPublicDecisionLog;
using ::core::MultiwayPublicDecisionPolicy;
using ::core::MultiwayProtectedReplayRecord;
using ::core::MultiwayVerifiedBlueprintArtifact;
using ::core::make_multiway_public_decision_log;
using ::core::MultiwayBlueprintConfig;
using ::core::MultiwayModelIdentity;
using ::core::MultiwayResolver;
using ::core::MultiwayBaselineFixtureKind;
using ::core::MultiwayBaselineMeasurements;
using ::core::MultiwayResolverBaselineFixture;
using ::core::MultiwayResolverBaselineFixtureHarness;
using ::core::MultiwayResolverBaselineReport;
using ::core::MultiwayTraversalBaselineReport;
using ::core::MultiwayResolverFallbackKind;
using ::core::observed_multiway_process_memory_bytes;
using ::core::observed_multiway_process_peak_memory_bytes;
using ::core::observed_multiway_process_cpu_nanoseconds;
using ::core::record_multiway_resolver_baseline;
using ::core::record_multiway_traversal_baseline;
using ::core::equivalent_multiway_resolver_baseline;
using ::core::equivalent_multiway_traversal_baseline;
using ::core::serialize_multiway_resolver_baseline;
using ::core::serialize_multiway_traversal_baseline;
using ::core::MultiwayResolverConfig;
using ::core::MultiwayResolverActionProbability;
using ::core::MultiwayResolverDiagnostics;
using ::core::MultiwayResolverSearchEligibility;
using ::core::MultiwayResolverSearchMode;
using ::core::MultiwayResolverRequest;
using ::core::MultiwayResolverResult;
using ::core::MultiwayResolverSeatRange;
using ::core::MultiwayResolverStatus;
using ::core::MultiwayPolicyProvenance;
using ::core::MultiwayResolverEngine;
using ::core::MULTIWAY_LEGACY_RESOLVER_ENGINE_VERSION;
using ::core::MultiwayInferenceMode;
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
using ::core::MultiwayGameRules;
using ::core::MultiwayHandHistory;
using ::core::MultiwayReplayDecision;
using ::core::MultiwayReplayEvent;
using ::core::MultiwayReplayEventKind;
using ::core::apply_multiway_replay_event;
using ::core::replay_multiway_hand;
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
using ::core::MultiwaySampledPublicBoardChance;
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


