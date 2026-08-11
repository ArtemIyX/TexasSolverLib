#pragma once

#include "games/multiway_private.hpp"
#include "solver/multiway_action_abstraction.hpp"
#include "solver/multiway_bucket_model.hpp"
#include "solver/multiway_export.hpp"
#include "solver/multiway_leaf_evaluator.hpp"
#include "solver/multiway_model_identity.hpp"
#include "solver/multiway_solver.hpp"

#include <array>
#include <chrono>
#include <cstdint>
#include <mutex>
#include <vector>

namespace core {

struct MultiwayVerifiedBlueprintArtifact;

enum class MultiwayInferenceMode : std::uint8_t {
    AnonymousWithinHand,
    BlockersOnly,
};

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
    std::vector<MultiwayResolverSeatRange> opponent_ranges;
    std::chrono::steady_clock::time_point deadline{};
    MultiwayInferenceMode inference_mode = MultiwayInferenceMode::AnonymousWithinHand;
    std::uint64_t sampling_seed = 1;
};

struct MultiwayResolverActionProbability {
    MultiwayActionDescriptor action{};
    double probability = 0.0;
};

enum class MultiwayResolverStatus : std::uint8_t {
    Solved,
    // Reserved for the runtime-search path. The legacy resolver does not emit
    // this value until it can export a clean partial search result.
    Partial,
    DeadlineFallback,
    InvalidRequest,
    ArtifactMismatch,
    BucketUnavailable,
    ResourceExhausted,
    // Reserved for future request preflight; legacy resolver limits do not
    // currently reject a request by this status.
    RejectedByBudget,
};

// The producing policy path. This remains separate from status so a future
// partial search cannot be mislabeled as a completed solve.
enum class MultiwayPolicyProvenance : std::uint8_t {
    None,
    LegacyDeterministicAdjustment,
    RuntimeSearch,
    StableRootFallback,
    BlueprintFallback,
    StaticLegalFallback,
};

enum class MultiwayResolverEngine : std::uint8_t {
    LegacyDeterministicAdjustment,
    RootExternalSamplingMCCFR,
};

inline constexpr std::uint64_t MULTIWAY_LEGACY_RESOLVER_ENGINE_VERSION = 1U;
inline constexpr std::uint64_t MULTIWAY_ROOT_SEARCH_RESOLVER_ENGINE_VERSION = 1U;

enum class MultiwayResolverSearchMode : std::uint8_t {
    LegacyStatic,
    SearchShadow,
    SearchActive,
    ForcedFallback,
};

struct MultiwayResolverDiagnostics {
    MultiwayResolverStatus status = MultiwayResolverStatus::InvalidRequest;
    MultiwayPolicyProvenance policy_provenance = MultiwayPolicyProvenance::None;
    MultiwayResolverEngine search_engine = MultiwayResolverEngine::LegacyDeterministicAdjustment;
    std::uint64_t search_engine_version = MULTIWAY_LEGACY_RESOLVER_ENGINE_VERSION;
    MultiwayModelIdentity artifact_identity{};
    bool has_artifact_identity = false;
    std::uint64_t completed_batches = 0;
    std::uint64_t completed_trajectories = 0;
    bool deadline_expired = false;
    bool used_fallback = false;
    bool policy_normalized = false;
    bool used_latest_stable_root = false;
    bool used_blueprint_fallback = false;
    bool used_static_fallback = false;
    bool anonymous_ranges_merged = false;
    bool shadow_search_completed = false;
    std::uint64_t shadow_completed_batches = 0;
    std::uint64_t shadow_completed_trajectories = 0;
    double shadow_policy_l1_distance = 0.0;
    std::uint32_t root_bucket = 0;
    std::uint32_t root_menu_size = 0;
    std::uint32_t admitted_range_entries = 0;
    std::uint64_t resolved_public_state_id = 0;
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
    // Verified artifacts are preferred over the legacy raw snapshot pointer.
    // Passing both is rejected so resolver deployment cannot silently bypass
    // manifest verification.
    const MultiwayVerifiedBlueprintArtifact* verified_blueprint = nullptr;
    const MultiwayBlueprintSnapshot* blueprint = nullptr;
    MultiwayActionAbstractionConfig action_abstraction{};
    std::uint32_t trajectories_per_batch = 32U;
    std::uint32_t max_batches = 64U;
    std::chrono::milliseconds deadline_reserve = std::chrono::milliseconds(1);
    MultiwayResolverSearchMode search_mode = MultiwayResolverSearchMode::LegacyStatic;
    MultiwaySolverLimits search_limits{};
    const MultiwayLeafEvaluator* leaf_evaluator = nullptr;
    std::uint32_t search_max_decision_depth = 1U;
    std::uint32_t search_max_public_chance_depth = 0U;

    void validate() const;
};

// In-process deadline-safe resolver. A successful solve retains its root
// policy for a later compatible deadline fallback. No private request data is
// retained after resolve returns.
class MultiwayResolver {
public:
    explicit MultiwayResolver(MultiwayResolverConfig config = {});
    [[nodiscard]] MultiwayResolverResult resolve(const MultiwayResolverRequest& request) const;

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

}  // namespace core
