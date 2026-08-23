#pragma once

#include "core/namespaces.hpp"

#include "games/multiway_private.hpp"
#include "solver/multiway_action_abstraction.hpp"
#include "solver/multiway_bucket_model.hpp"
#include "solver/multiway_export.hpp"
#include "solver/multiway_leaf_evaluator.hpp"
#include "solver/multiway_model_identity.hpp"
#include "solver/multiway_memory.hpp"
#include "solver/multiway_continuation_selector.hpp"
#include "solver/multiway_solver.hpp"
#include "solver/multiway_search_profile.hpp"

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <memory>
#include <vector>

namespace texas::solver::multiway {

struct MultiwayVerifiedBlueprintArtifact;
class MultiwayBlueprintStore;
class MultiwayRuntimeSession;

// A range supplied by a resolver caller for one non-hero seat. Seat ids are
// explicit so the request remains valid after players have folded.
struct MultiwayResolverSeatRange {
    PlayerId seat = -1;
    std::vector<MultiwayWeightedHole> hands;
};

// Immutable in-process resolver boundary. public_state must describe the
// exact current public game state; generic ranges are intentionally separate
// from the known hero hole cards.
struct MultiwayResolverRequest {
    MultiwayModelIdentity blueprint_identity{};
    MultiwayPublicStateDescriptor public_state{};
    PlayerId hero_seat = -1;
    std::array<std::uint8_t, 2> hero_cards = {0, 0};
    // Explicit hero belief. The known hero cards must be present in this
    // range, but do not implicitly create a singleton range.
    std::vector<MultiwayWeightedHole> hero_range;
    std::vector<MultiwayResolverSeatRange> opponent_ranges;
    std::chrono::steady_clock::time_point deadline{};
    std::uint64_t sampling_seed = 1;
};

struct MultiwayResolverActionProbability {
    MultiwayActionDescriptor action{};
    double probability = 0.0;
};

enum class MultiwayResolverStatus : std::uint8_t {
    Solved,
    Partial,
    DeadlineFallback,
    InvalidRequest,
    ArtifactMismatch,
    BucketUnavailable,
    ResourceExhausted,
    RejectedByBudget,
};

enum class MultiwayResolverSearchFailure : std::uint8_t {
    None,
    NoRoot,
    MemoryRejected,
    NoCleanBatch,
    NormalizationFailed,
    Exception,
};

// The producing policy path. This remains separate from status so a future
// partial search cannot be mislabeled as a completed solve.
enum class MultiwayPolicyProvenance : std::uint8_t {
    None,
    RuntimeSearch,
    StableRootFallback,
    BlueprintFallback,
    StaticLegalFallback,
};

enum class MultiwayResolverEngine : std::uint8_t {
    RootExternalSamplingMCCFR,
    NoRuntimeSearch,
};

inline constexpr std::uint64_t MULTIWAY_ROOT_SEARCH_RESOLVER_ENGINE_VERSION = 1U;
inline constexpr std::uint64_t MULTIWAY_NO_RUNTIME_SEARCH_ENGINE_VERSION = 1U;

enum class MultiwayResolverSearchMode : std::uint8_t {
    // Delivers only the stable-root, blueprint, or static-legal fallback chain.
    FallbackOnly,
    SearchShadow,
    SearchActive,
    ForcedFallback,
    // Prefer runtime search when a complete release search configuration is
    // supplied. Otherwise use the normal safe fallback chain.
    ReleaseDefault,
};

// Explains whether the request may enter runtime search. It is diagnostic-only
// and never contains private range or card data.
enum class MultiwayResolverSearchEligibility : std::uint8_t {
    NotRequested,
    Eligible,
    UnsupportedStreet,
    SeatCount,
    FoldedSeat,
    IncompleteRanges,
    MenuTooLarge,
};

struct MultiwayResolverDiagnostics {
    MultiwayResolverStatus status = MultiwayResolverStatus::InvalidRequest;
    MultiwayPolicyProvenance policy_provenance = MultiwayPolicyProvenance::None;
    MultiwayResolverEngine search_engine = MultiwayResolverEngine::NoRuntimeSearch;
    std::uint64_t search_engine_version = MULTIWAY_NO_RUNTIME_SEARCH_ENGINE_VERSION;
    MultiwayModelIdentity artifact_identity{};
    bool has_artifact_identity = false;
    std::uint64_t completed_batches = 0;
    std::uint64_t completed_trajectories = 0;
    std::uint64_t search_merged_delta_entries = 0;
    std::uint64_t search_first_trajectory_id = 0;
    std::uint64_t search_trajectory_count = 0;
    std::uint64_t search_root_revision = 0;
    std::uint64_t search_elapsed_nanoseconds = 0;
    std::uint64_t search_observed_memory_bytes = 0;
    std::uint32_t search_worker_count = 0;
    std::size_t search_admitted_rows = 0U;
    std::size_t search_admitted_values = 0U;
    MultiwaySearchProfileSnapshot search_profile{};
    MultiwayMemoryStatus search_memory_status = MultiwayMemoryStatus::Ok;
    MultiwayMemoryAdmissionStage search_memory_stage = MultiwayMemoryAdmissionStage::None;
    MultiwayResolverSearchFailure search_failure = MultiwayResolverSearchFailure::None;
    std::uint64_t search_estimated_memory_bytes = 0U;
    std::uint64_t search_admitted_memory_bytes = 0U;
    bool search_memory_degraded = false;
    std::uint64_t search_schedule_fingerprint = 0U;
    std::uint64_t search_merged_stream_fingerprint = 0U;
    bool search_bitwise_deterministic = false;
    bool deadline_expired = false;
    bool used_fallback = false;
    bool policy_normalized = false;
    bool used_latest_stable_root = false;
    bool used_blueprint_fallback = false;
    bool used_static_fallback = false;
    bool shadow_search_completed = false;
    std::uint64_t shadow_completed_batches = 0;
    std::uint64_t shadow_completed_trajectories = 0;
    std::uint64_t shadow_search_merged_delta_entries = 0;
    std::uint64_t shadow_search_elapsed_nanoseconds = 0;
    std::uint64_t shadow_search_observed_memory_bytes = 0;
    double shadow_policy_l1_distance = 0.0;
    std::uint32_t root_bucket = 0;
    std::uint32_t root_menu_size = 0;
    std::uint32_t admitted_range_entries = 0;
    std::uint64_t resolved_public_state_id = 0;
    MultiwayResolverSearchEligibility search_eligibility =
        MultiwayResolverSearchEligibility::NotRequested;
};

struct MultiwayResolverResult {
    MultiwayActionDescriptor sampled_action{};
    bool has_sampled_action = false;
    std::vector<MultiwayResolverActionProbability> policy;
    MultiwayResolverDiagnostics diagnostics{};
};

// Artifact dependencies are borrowed and must outlive the resolver. A null
// bucket registry is valid only for preflop roots; postflop roots then return
// a validated static fallback with BucketUnavailable diagnostics.
struct MultiwayResolverConfig {
    const MultiwayBucketRegistry* buckets = nullptr;
    // Artifacts are owned by the resolver configuration. Unverified raw
    // snapshots cannot bypass manifest validation.
    std::shared_ptr<const MultiwayVerifiedBlueprintArtifact> verified_blueprint;
    // Immutable arbitrary-state prior used only by request-local traversal.
    std::shared_ptr<const MultiwayBlueprintStore> full_blueprint;
    MultiwayActionAbstractionConfig action_abstraction{};
    std::uint32_t trajectories_per_batch = 32U;
    std::chrono::milliseconds deadline_reserve = std::chrono::milliseconds(1);
    MultiwayResolverSearchMode search_mode = MultiwayResolverSearchMode::ReleaseDefault;
    // Stable-root reuse is an explicit deployment policy, not hidden resolver state.
    bool retain_stable_root_fallback = true;
    std::shared_ptr<const MultiwayFixedContinuationSelector> continuation_selector;
    MultiwaySolverLimits search_limits{};
    const MultiwayLeafEvaluator* leaf_evaluator = nullptr;
    std::uint32_t search_max_decision_depth = 1U;
    std::uint32_t search_max_public_chance_depth = 0U;
    std::uint8_t active_search_min_seats = 2U;
    std::uint8_t active_search_max_seats = 6U;
    std::uint32_t active_search_max_menu_actions = MULTIWAY_MAX_ABSTRACTED_ACTIONS;
    MultiwaySearchProfileMode search_profile_mode = MultiwaySearchProfileMode::Disabled;
    MultiwayMemoryBudget search_memory_budget{};
    // Optional request-local capacities owned by a typed or host leaf path.
    // A typed rollout cache is measured automatically when these remain zero.
    std::uint64_t search_future_bucket_cache_bytes = 0U;
    std::uint64_t search_continuation_scratch_bytes_per_worker = 0U;
    std::uint64_t search_continuation_cache_bytes = 0U;

    void validate() const;
};

// In-process deadline-safe resolver. A successful solve retains its root
// policy for a later compatible deadline fallback. No private request data is
// retained after resolve returns.
class MultiwayResolver {
public:
    explicit MultiwayResolver(MultiwayResolverConfig config = {});
    [[nodiscard]] MultiwayResolverResult resolve(const MultiwayResolverRequest& request) const;
    [[nodiscard]] std::unique_ptr<MultiwayRuntimeSession> begin_runtime_session(
        const MultiwayResolverRequest& request) const;

private:
    struct StableRootPolicy {
        MultiwayModelIdentity identity{};
        std::uint64_t public_state_id = 0;
        std::vector<MultiwayResolverActionProbability> policy;
    };

    MultiwayResolverConfig config_{};
    mutable std::mutex stable_policy_mutex_;
    mutable StableRootPolicy stable_policy_{};
};

}  // namespace texas::solver::multiway
