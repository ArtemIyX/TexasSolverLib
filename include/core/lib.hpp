#pragma once

#include "core/namespaces.hpp"

#include "util/abstraction.hpp"
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
#include "solver/multiway_future_bucket.hpp"
#include "solver/multiway_memory.hpp"
#include "solver/multiway_search_profile.hpp"
#include "solver/multiway_scheduler.hpp"
#include "solver/multiway_traversal.hpp"
#include "solver/multiway_leaf_evaluator.hpp"
#include "solver/multiway_continuation_policy.hpp"
#include "solver/multiway_continuation_selector.hpp"
#include "solver/multiway_rollout_leaf.hpp"
#include "solver/multiway_export.hpp"
#include "solver/multiway_blueprint_trainer.hpp"
#include "solver/multiway_blueprint_store.hpp"
#include "solver/multiway_blueprint_policy_provider.hpp"
#include "solver/multiway_range_update.hpp"
#include "solver/multiway_range_belief.hpp"
#include "solver/multiway_search_session.hpp"
#include "solver/multiway_runtime_session.hpp"
#include "solver/multiway_checkpoint.hpp"
#include "solver/multiway_artifact.hpp"
#include "solver/multiway_model_identity.hpp"
#include "solver/multiway_resolver.hpp"
#include "solver/multiway_resolver_evaluation.hpp"
#include "solver/multiway_evaluation.hpp"
#include "solver/multiway_baseline.hpp"
#include "solver/multiway_public_builder.hpp"
#include "solver/multiway_solver.hpp"
#include "solver/multiway_terminal_adapter.hpp"
#include "solver/dcfr_vector.hpp"
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

namespace texas::core::lib {

/**
 * @brief Stable convenience aliases for external consumers.
 */
using ::texas::ActionId;
using ::texas::CanonicalComboCards;
using ::texas::CanonicalComboId;
using ::texas::CanonicalComboLegalMask;
using ::texas::CanonicalComboView;
using ::texas::canonical_combos;
using ::texas::ChanceOutcome;
using ::texas::Class169RvrOutput;
using ::texas::ExploitOutput;
using ::texas::HUNLConfig;
using ::texas::HUNLLeafEvaluationRequest;
using ::texas::HUNLLeafEvaluationResult;
using ::texas::HUNLLeafEvaluationScope;
using ::texas::HUNLLeafEvaluator;
using ::texas::HUNLLeafValueUnits;
using ::texas::HUNLLiveRootSnapshot;
using ::texas::HUNLSolveOutput;
using ::texas::HUNLSampledSolveResult;
using ::texas::HUNLSampledSolverConfig;
using ::texas::HUNLSampledRangeSession;
using ::texas::HUNLStructuredRootRequest;
using ::texas::HUNLQualityMetric;
using ::texas::InfosetKey;
using ::texas::MultiwayAction;
using ::texas::MultiwayActionAbstraction;
using ::texas::MultiwayActionAbstractionConfig;
using ::texas::MultiwayActionAbstractionContext;
using ::texas::MultiwayActionTranslation;
using ::texas::MultiwayActionTranslationStatus;
using ::texas::MultiwayPreflopSituation;
using ::texas::MultiwayRelativePosition;
using ::texas::MultiwayPostflopSizingMode;
using ::texas::MultiwayBucketTable;
using ::texas::MultiwayBucketRegistry;
using ::texas::MultiwayBucketBaselineProfile;
using ::texas::MultiwayBucketFeatures;
using ::texas::MultiwayBucketBoardRequest;
using ::texas::MultiwayDeviationDisposition;
using ::texas::MultiwayDeviationExpansionConfig;
using ::texas::MultiwayFutureBucketArtifact;
using ::texas::MultiwayFutureBucketFeatures;
using ::texas::MultiwayFutureBucketProfile;
using ::texas::build_multiway_baseline_bucket_table;
using ::texas::build_multiway_baseline_bucket_registry;
using ::texas::build_multiway_future_bucket_artifact;
using ::texas::deserialize_multiway_future_bucket_artifact;
using ::texas::deserialize_multiway_bucket_registry;
using ::texas::serialize_multiway_bucket_registry;
using ::texas::serialize_multiway_future_bucket_artifact;
using ::texas::validate_multiway_bucket_coverage;
using ::texas::MultiwayMemoryBudget;
using ::texas::MultiwayMemoryAdmissionStage;
using ::texas::MultiwayMemoryEstimate;
using ::texas::MultiwayMemoryInputs;
using ::texas::MultiwayMemoryPreflight;
using ::texas::MultiwayMemoryStatus;
using ::texas::preflight_multiway_memory;
using ::texas::MultiwaySearchProfileCheckpoint;
using ::texas::MultiwaySearchProfileMode;
using ::texas::MultiwaySearchProfileRankingEntry;
using ::texas::MultiwaySearchProfileSnapshot;
using ::texas::MultiwaySearchProfileStage;
using ::texas::rank_multiway_search_profile;
using ::texas::MultiwayScheduler;
using ::texas::MultiwayRunMetadata;
using ::texas::MultiwayRunMode;
using ::texas::MultiwayTrajectoryRange;
using ::texas::MultiwayWorkerBatch;
using ::texas::multiway_deterministic_schedule_fingerprint;
using ::texas::multiway_deterministic_trajectory_seed;
using ::texas::MultiwayExternalSamplingTraversal;
using ::texas::MultiwayRootBatchResult;
using ::texas::MultiwayRootBatchRunner;
using ::texas::MultiwayLeafEvaluationRequest;
using ::texas::MultiwayLeafEvaluator;
using ::texas::MultiwayContinuationPolicyKind;
using ::texas::MultiwayFixedContinuationPolicy;
using ::texas::MultiwayFixedContinuationSelector;
using ::texas::MultiwayContinuationSelectionKey;
using ::texas::MultiwayContinuationLeafContext;
using ::texas::MultiwayContinuationLeafData;
using ::texas::make_multiway_fixed_continuation_leaf_evaluator;
using ::texas::MultiwayRolloutActionMenu;
using ::texas::MultiwayRolloutActionProviderFn;
using ::texas::MultiwayRolloutInput;
using ::texas::MultiwayRolloutInputProviderFn;
using ::texas::MultiwayRolloutLeafContext;
using ::texas::MultiwayRolloutLimits;
using ::texas::MultiwayRolloutProfileResult;
using ::texas::MultiwayRolloutRunoutMode;
using ::texas::MultiwayRolloutScratch;
using ::texas::MultiwayRolloutStatus;
using ::texas::MultiwayContinuationCache;
using ::texas::MultiwayContinuationCacheKey;
using ::texas::MultiwayContinuationDiagnostics;
using ::texas::evaluate_multiway_rollout_profiles;
using ::texas::make_multiway_rollout_leaf_evaluator;
using ::texas::MultiwayBlueprintSnapshot;
using ::texas::MultiwayBlueprintPolicyKind;
using ::texas::MultiwayBlueprintTrainingMetadata;
using ::texas::export_multiway_root_snapshot;
using ::texas::MultiwayBlueprintTrainer;
using ::texas::MultiwayBlueprintTrainingConfig;
using ::texas::MultiwayBlueprintTrainingSession;
using ::texas::MultiwayBlueprintTrainingStatus;
using ::texas::MultiwayBlueprintTrainingCheckpoint;
using ::texas::MultiwayBlueprintIterationSchedule;
using ::texas::MultiwayBlueprintCoverageManifest;
using ::texas::MultiwayBlueprintRow;
using ::texas::MultiwayBlueprintStore;
using ::texas::MultiwayBlueprintPolicyProvider;
using ::texas::MultiwayBlueprintLookupStatus;
using ::texas::MultiwayBucketActionPolicy;
using ::texas::MultiwayRangeBeliefMetadata;
using ::texas::MultiwayRangeBeliefObservation;
using ::texas::MultiwayRangeBeliefSeatInput;
using ::texas::MultiwayRangeBeliefSource;
using ::texas::MultiwayRangeBeliefSuppliedEntry;
using ::texas::MultiwayRangeBeliefUpdateResult;
using ::texas::MultiwayRangeBeliefView;
using ::texas::MultiwayRangeBeliefs;
using ::texas::MultiwaySearchSession;
using ::texas::MultiwaySearchSessionCleanSnapshot;
using ::texas::MultiwaySearchSessionDependencies;
using ::texas::MultiwaySearchSessionRootMetadata;
using ::texas::MultiwaySearchSessionRowView;
using ::texas::MultiwaySearchSessionHeroRow;
using ::texas::MultiwaySearchSessionHeroPolicy;
using ::texas::MultiwayRuntimeSession;
using ::texas::MultiwayCheckpoint;
using ::texas::MultiwayArtifactSource;
using ::texas::MultiwayBlueprintArtifacts;
using ::texas::MultiwayFullBlueprintArtifact;
using ::texas::MultiwayFullBlueprintArtifacts;
using ::texas::MultiwayBlueprintManifest;
using ::texas::MultiwayPublicDecisionLog;
using ::texas::MultiwayPublicDecisionPolicy;
using ::texas::MultiwayProtectedReplayRecord;
using ::texas::MultiwayAivatActionValue;
using ::texas::MultiwayAivatDecisionRecord;
using ::texas::MultiwayAivatEvaluationRecord;
using ::texas::MultiwayAivatEvaluationRecordSinkFn;
using ::texas::MultiwayVerifiedBlueprintArtifact;
using ::texas::make_multiway_public_decision_log;
using ::texas::publish_multiway_aivat_evaluation_record;
using ::texas::MultiwayBlueprintConfig;
using ::texas::MultiwayModelIdentity;
using ::texas::MultiwayResolver;
using ::texas::MultiwayResolverEvaluationAdapter;
using ::texas::MultiwayResolverEvaluationAdapterConfig;
using ::texas::MultiwayResolverEvaluationCandidate;
using ::texas::MultiwayResolverEvaluationCandidateKind;
using ::texas::MultiwayResolverEvaluationDecision;
using ::texas::MultiwayBaselineFixtureKind;
using ::texas::MultiwayBaselineMeasurements;
using ::texas::MultiwayResolverBaselineFixture;
using ::texas::MultiwayResolverBaselineFixtureHarness;
using ::texas::MultiwayResolverBaselineReport;
using ::texas::MultiwayTraversalBaselineReport;
using ::texas::MultiwayResolverFallbackKind;
using ::texas::observed_multiway_process_memory_bytes;
using ::texas::observed_multiway_process_peak_memory_bytes;
using ::texas::observed_multiway_process_cpu_nanoseconds;
using ::texas::record_multiway_resolver_baseline;
using ::texas::record_multiway_traversal_baseline;
using ::texas::equivalent_multiway_resolver_baseline;
using ::texas::equivalent_multiway_traversal_baseline;
using ::texas::serialize_multiway_resolver_baseline;
using ::texas::serialize_multiway_traversal_baseline;
using ::texas::MultiwayResolverConfig;
using ::texas::MultiwayResolverActionProbability;
using ::texas::MultiwayResolverDiagnostics;
using ::texas::MultiwayResolverSearchEligibility;
using ::texas::MultiwayResolverSearchMode;
using ::texas::MultiwayResolverRequest;
using ::texas::MultiwayResolverResult;
using ::texas::MultiwayResolverSeatRange;
using ::texas::MultiwayResolverStatus;
using ::texas::MultiwayPolicyProvenance;
using ::texas::MultiwayResolverEngine;
using ::texas::MultiwayPublicBuilder;
using ::texas::make_multiway_model_identity;
using ::texas::MultiwayActionAbstractionIdentity;
using ::texas::MultiwayBettingSnapshot;
using ::texas::MultiwayBoardChanceEdge;
using ::texas::MultiwayBoardRunoutState;
using ::texas::MultiwayCFRConfig;
using ::texas::MultiwayCFRUpdate;
using ::texas::MultiwayExternalSamplingRequest;
using ::texas::make_multiway_external_sampling_request;
using ::texas::MultiwayGameConfig;
using ::texas::MultiwayGameRules;
using ::texas::MultiwayHandHistory;
using ::texas::MultiwayReplayDecision;
using ::texas::MultiwayReplayEvent;
using ::texas::MultiwayReplayEventKind;
using ::texas::apply_multiway_replay_event;
using ::texas::replay_multiway_hand;
using ::texas::MultiwayFixedActionMenu;
using ::texas::MultiwayFixedSidePot;
using ::texas::MultiwayFixedState;
using ::texas::MultiwayFixedTerminalInput;
using ::texas::MultiwayFixedTerminalResult;
using ::texas::MultiwayFixedTerminalScratch;
using ::texas::make_multiway_fixed_state;
using ::texas::settle_multiway_terminal_fixed;
using ::texas::MultiwayMetricMethod;
using ::texas::MultiwayNashConv;
using ::texas::MultiwayOddChipRule;
using ::texas::MultiwayActionDescriptor;
using ::texas::MultiwayInfosetId;
using ::texas::MultiwayJointPrivateSample;
using ::texas::MultiwayCompiledPrivateRanges;
using ::texas::MultiwayPrivateWorkerScratch;
using ::texas::MultiwayPotLayout;
using ::texas::MultiwayRakeMode;
using ::texas::MultiwayRakePolicy;
using ::texas::MultiwayPrivateConfig;
using ::texas::MultiwayPrivateRangeFeasibilityResult;
using ::texas::MultiwayPrivateRangeFeasibilityStatus;
using ::texas::preflight_multiway_private_range_feasibility;
using ::texas::MultiwaySidePot;
using ::texas::MultiwayShowdownInput;
using ::texas::MultiwayState;
using ::texas::MultiwayTerminalInput;
using ::texas::MultiwayTerminalAdapter;
using ::texas::MultiwaySamplerDealToken;
using ::texas::MultiwayTerminalResult;
using ::texas::MultiwayStreetTransition;
using ::texas::MultiwayPublicStreetTransition;
using ::texas::MultiwayQualityDiagnostics;
using ::texas::MultiwayPublicHistoryEntry;
using ::texas::MultiwayPublicBoardChanceEdge;
using ::texas::MultiwaySampledPublicBoardChance;
using ::texas::MultiwayPublicParentEdge;
using ::texas::MultiwayPublicParentEdgeKind;
using ::texas::MultiwayPublicStateDescriptor;
using ::texas::MultiwayPublicStateId;
using ::texas::MultiwayRootActionProbability;
using ::texas::MultiwayRootPolicy;
using ::texas::MultiwayRootSnapshot;
using ::texas::MultiwaySolveDiagnostics;
using ::texas::MultiwaySolveRequest;
using ::texas::MultiwaySolveResult;
using ::texas::MultiwaySolverCoordinator;
using ::texas::MultiwaySolverLimits;
using ::texas::MultiwaySparseRowMetadata;
using ::texas::MultiwaySparseRowShape;
using ::texas::MultiwaySparseRowStorage;
using ::texas::solver::multiway::MultiwayValueUnits;
using ::texas::MultiwayWorkerDelta;
using ::texas::MultiwayWorkerDeltaStream;
using ::texas::MultiwayWeightedHole;
using ::texas::PlayerId;
using ::texas::PreflopSolveOutput;
using ::texas::Probability;
using ::texas::SolveOutput;
using ::texas::Value;
using ::texas::VectorSolveOutput;

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
    return ::texas::solve_kuhn(iterations, alpha, beta, gamma, workers, frontier_multiplier);
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
    return ::texas::solve_leduc(iterations, alpha, beta, gamma, workers, frontier_multiplier);
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
    return ::texas::solve_hunl_postflop(
        config, iterations, alpha, beta, gamma, workers, frontier_multiplier, force_parallel);
}

inline PreflopSolveOutput solve_hunl_preflop(
    const HUNLConfig& config,
    std::uint32_t iterations,
    double alpha,
    double beta,
    double gamma) {
    return ::texas::solve_hunl_preflop(config, iterations, alpha, beta, gamma);
}

inline ExploitOutput compute_exploitability(
    const HUNLConfig& config,
    const std::unordered_map<std::string, std::vector<double>>& strategy) {
    return ::texas::compute_exploitability_and_value(config, strategy);
}

inline double compute_restricted_game_value(
    const HUNLConfig& config,
    const std::unordered_map<std::string, std::vector<double>>& strategy,
    const std::vector<std::array<std::uint8_t, 2>>& p0_holes,
    const std::vector<std::array<std::uint8_t, 2>>& p1_holes) {
    return ::texas::compute_restricted_game_value(config, strategy, p0_holes, p1_holes);
}

inline VectorSolveOutput solve_range_vs_range_rust(
    const HUNLConfig& config,
    const std::vector<std::array<std::array<std::uint8_t, 2>, 2>>& hole_pairs,
    std::uint32_t iterations,
    double alpha,
    double beta,
    double gamma) {
    return ::texas::solve_vector_dcfr(
        ::texas::BettingTree::build_from(::texas::HUNLState::initial(std::make_shared<const HUNLConfig>(config))),
        hole_pairs,
        iterations,
        alpha,
        beta,
        gamma);
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
    return ::texas::solve_hunl_preflop_rvr_class169(
        config,
        table,
        std::move(root_reach_p0),
        std::move(root_reach_p1),
        iterations,
        alpha,
        beta,
        gamma);
}

}  // namespace texas::core::lib


