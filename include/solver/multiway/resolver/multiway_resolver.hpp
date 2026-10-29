#pragma once

/** @file
 * Validated in-process resolver requests, runtime search, and fallback policy APIs.
 */

#include "games/multiway_private.hpp"
#include "solver/multiway/abstraction/multiway_action_abstraction.hpp"
#include "solver/multiway/abstraction/multiway_bucket_model.hpp"
#include "solver/multiway/blueprint/multiway_export.hpp"
#include "solver/multiway/continuation/multiway_leaf_evaluator.hpp"
#include "solver/multiway/abstraction/multiway_model_identity.hpp"
#include "solver/multiway/engine/multiway_memory.hpp"
#include "solver/multiway/continuation/multiway_continuation_selector.hpp"
#include "solver/multiway/abstraction/multiway_future_bucket.hpp"
#include "solver/multiway/engine/multiway_solver.hpp"
#include "solver/multiway/session/multiway_search_profile.hpp"

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <memory>
#include <vector>

namespace texas::solver::multiway
{

	struct MultiwayVerifiedBlueprintArtifact;
	class MultiwayBlueprintStore;
	class MultiwayMappedShardedBlueprintProvider;
	class MultiwayDecisionSession;

	// Single validated owner for all bounded release-search admission and
	// execution limits. Resolver budget state is derived from this snapshot.
	/** Bounded solver, memory, depth, seat, and cache limits for runtime search. */
	struct MultiwayRuntimeSearchLimits
	{
		MultiwaySolverLimits solver{ 1U, 1U, 1U, 1'024U, 1'024U, 8'192U, 8'192U, 64U };
		MultiwayMemoryBudget memory{};
		std::chrono::milliseconds deadline_reserve = std::chrono::milliseconds(1);
		std::uint32_t max_decision_depth = 1U;
		std::uint32_t max_public_chance_depth = 0U;
		std::uint8_t active_min_seats = 2U;
		std::uint8_t active_max_seats = 6U;
		std::uint32_t active_max_menu_actions = MULTIWAY_MAX_ABSTRACTED_ACTIONS;
		std::uint64_t future_bucket_cache_bytes = 0U;
		std::uint64_t continuation_scratch_bytes_per_worker = 0U;
		std::uint64_t continuation_cache_bytes = 0U;

		/** Checks all runtime search limits and nested solver settings. */
		void validate() const;
	};

	// A range supplied by a resolver caller for one non-hero seat. Seat ids are
	// explicit so the request remains valid after players have folded.
	/** Weighted private hands supplied for one non-hero seat. */
	struct MultiwayResolverSeatRange
	{
		PlayerId seat = -1;
		std::vector<MultiwayWeightedHole> hands;
	};

	// Immutable in-process resolver boundary. public_state must describe the
	// exact current public game state; generic ranges are intentionally separate
	// from the known hero hole cards.
	/** Immutable public state, private ranges, seat order, and deadline input. */
	struct MultiwayResolverRequest
	{
		MultiwayModelIdentity blueprint_identity{};
		MultiwayPublicStateDescriptor public_state{};
		PlayerId hero_seat = -1;
		std::array<std::uint8_t, 2> hero_cards = { 0, 0 };
		// Explicit hero belief. The known hero cards must be present in this
		// range, but do not implicitly create a singleton range.
		std::vector<MultiwayWeightedHole> hero_range;
		std::vector<MultiwayResolverSeatRange> opponent_ranges;
		// Canonical cyclic seat order. Empty preserves the legacy ascending order.
		std::vector<PlayerId> seat_order;
		PlayerId next_street_first_seat = -1;
		PlayerId odd_chip_first_seat = -1;
		std::chrono::steady_clock::time_point deadline{};
		std::uint64_t sampling_seed = 1;
	};

	/** Legal root action and its normalized resolver probability. */
	struct MultiwayResolverActionProbability
	{
		MultiwayActionDescriptor action{};
		double probability = 0.0;
	};

	/** Completion or rejection outcome returned by a resolver invocation. */
	enum class MultiwayResolverStatus : std::uint8_t
	{
		Solved,
		Partial,
		DeadlineFallback,
		InvalidRequest,
		ArtifactMismatch,
		BucketUnavailable,
		ResourceExhausted,
		RejectedByBudget,
	};

	/** Failure reason for an attempted runtime search path. */
	enum class MultiwayResolverSearchFailure : std::uint8_t
	{
		None,
		NoRoot,
		MemoryRejected,
		NoCleanBatch,
		NormalizationFailed,
		Exception,
	};

	// The producing policy path. This remains separate from status so a future
	// partial search cannot be mislabeled as a completed solve.
	/** Policy source used to produce the returned resolver distribution. */
	enum class MultiwayPolicyProvenance : std::uint8_t
	{
		None,
		RuntimeSearch,
		StableRootFallback,
		BlueprintFallback,
		StaticLegalFallback,
	};

	/** Runtime engine identity recorded in resolver diagnostics. */
	enum class MultiwayResolverEngine : std::uint8_t
	{
		RootExternalSamplingMCCFR,
		NoRuntimeSearch,
	};

	inline constexpr std::uint64_t MULTIWAY_ROOT_SEARCH_RESOLVER_ENGINE_VERSION = 1U;
	inline constexpr std::uint64_t MULTIWAY_NO_RUNTIME_SEARCH_ENGINE_VERSION = 1U;

	/** Selects fallback-only, shadow, active, or release-default search behavior. */
	enum class MultiwayResolverSearchMode : std::uint8_t
	{
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
	/** Diagnostic reason a request can or cannot enter runtime search. */
	enum class MultiwayResolverSearchEligibility : std::uint8_t
	{
		NotRequested,
		Eligible,
		UnsupportedStreet,
		SeatCount,
		FoldedSeat,
		IncompleteRanges,
		MenuTooLarge,
	};

	/** Public model, search, memory, fallback, and deadline diagnostics. */
	struct MultiwayResolverDiagnostics
	{
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

	/** Sampled root action, normalized policy, and resolver diagnostics. */
	struct MultiwayResolverResult
	{
		MultiwayActionDescriptor sampled_action{};
		bool has_sampled_action = false;
		std::vector<MultiwayResolverActionProbability> policy;
		MultiwayResolverDiagnostics diagnostics{};
	};

	// Host-owned fallback storage. The resolver never keeps a private cache, so
	// cache lifetime and sharing policy remain explicit at the service boundary.
	/** Host-owned cache of compatible stable root policies. */
	class MultiwayStableRootPolicyCache
	{
	public:
		/** Finds a cached policy matching model, state, hero, and exact hand. */
		[[nodiscard]] bool find(
			const MultiwayModelIdentity& identity,
			std::uint64_t public_state_id,
			PlayerId hero_seat,
			std::uint32_t exact_hand,
			const std::vector<MultiwayActionDescriptor>& menu,
			std::vector<MultiwayResolverActionProbability>* policy) const;

		/** Replaces the cached stable policy entry. */
		void store(
			const MultiwayModelIdentity& identity,
			std::uint64_t public_state_id,
			PlayerId hero_seat,
			std::uint32_t exact_hand,
			std::vector<MultiwayResolverActionProbability> policy);

	private:
		struct Entry
		{
			MultiwayModelIdentity identity{};
			std::uint64_t public_state_id = 0U;
			PlayerId hero_seat = -1;
			std::uint32_t exact_hand = 0U;
			std::vector<MultiwayResolverActionProbability> policy;
		};

		mutable std::mutex mutex_;
		Entry entry_;
	};

	// Artifact dependencies are borrowed and must outlive the resolver. A null
	// bucket registry is valid only for preflop roots; postflop roots then return
	// a validated static fallback with BucketUnavailable diagnostics.
	/** Artifact, search, bucket, and fallback dependencies for a resolver. */
	struct MultiwayResolverConfig
	{
		const MultiwayBucketRegistry* buckets = nullptr;
		// Artifacts are owned by the resolver configuration. Unverified raw
		// snapshots cannot bypass manifest validation.
		std::shared_ptr<const MultiwayVerifiedBlueprintArtifact> verified_blueprint;
		// Immutable arbitrary-state prior used only by request-local traversal.
		std::shared_ptr<const MultiwayBlueprintStore> full_blueprint;
		std::shared_ptr<const MultiwayMappedShardedBlueprintProvider> mapped_full_blueprint;
		// Optional future-bucket artifact used by continuation leaves. Its
		// registry identity must match the request model identity.
		std::shared_ptr<const MultiwayFutureBucketArtifact> future_bucket_artifact;
		MultiwayActionAbstractionConfig action_abstraction{};
		MultiwayResolverSearchMode search_mode = MultiwayResolverSearchMode::ReleaseDefault;
		// Stable-root reuse is explicit host-owned state, not resolver state.
		std::shared_ptr<MultiwayStableRootPolicyCache> stable_root_cache;
		std::shared_ptr<const MultiwayFixedContinuationSelector> continuation_selector;
		MultiwayRuntimeSearchLimits runtime_limits{};
		const MultiwayLeafEvaluator* leaf_evaluator = nullptr;
		MultiwaySearchProfileMode search_profile_mode = MultiwaySearchProfileMode::Disabled;

		/** Checks dependencies, artifacts, limits, and compatibility identities. */
		void validate() const;
	};

	// In-process deadline-safe resolver. A successful solve retains its root
	// policy for a later compatible deadline fallback. No private request data is
	// retained after resolve returns.
	/** Resolves one request through search or the configured safe fallback chain. */
	class MultiwayResolver
	{
	public:
		/** Creates a resolver with validated configuration and borrowed inputs. */
		explicit MultiwayResolver(MultiwayResolverConfig config = {});
		/** Resolves a request and returns a normalized policy with diagnostics. */
		[[nodiscard]] MultiwayResolverResult resolve(const MultiwayResolverRequest& request) const;
		/** Creates a stateful multi-decision session from a compatible request. */
		[[nodiscard]] std::unique_ptr<MultiwayDecisionSession> begin_decision_session(
			const MultiwayResolverRequest& request) const;

	private:
		MultiwayResolverConfig config_{};
	};

} // namespace texas::solver::multiway
