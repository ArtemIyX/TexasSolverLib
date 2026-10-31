#pragma once

/** @file
 * Bounded rollout evaluator for fixed multiway continuation policies.
 */

#include "games/multiway_fixed.hpp"
#include "solver/multiway/continuation/multiway_continuation_policy.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <vector>

namespace texas::solver::multiway
{

	struct MultiwayMemoryComponent;

	// Bounded continuation leaf evaluation. All storage is caller-owned so each
	// worker can retain one context and scratch instance without shared mutation.
	inline constexpr std::size_t MULTIWAY_ROLLOUT_MAX_ACTIONS = 8U;
	inline constexpr std::size_t MULTIWAY_ROLLOUT_MAX_BOARD_CARDS = 5U;
	inline constexpr std::size_t MULTIWAY_ROLLOUT_DECK_CARDS = 52U;

	/** Outcome class for one bounded rollout evaluation. */
	enum class MultiwayRolloutStatus : std::uint8_t
	{
		Complete,
		CappedFallback,
		InvalidContext,
	};

	/** Runout sampling mode used to evaluate a continuation profile. */
	enum class MultiwayRolloutRunoutMode : std::uint8_t
	{
		None,
		Exact,
		Seeded,
		Mixed,
	};

	/** Limits on betting expansion and exact all-in enumeration. */
#pragma pack(push, 8)
	struct MultiwayRolloutLimits
	{
		std::uint32_t max_betting_actions = 64U;
		// At most this many exact all-in runouts are enumerated. Larger runout
		// spaces use the supplied common-random-number seeds instead.
		std::uint32_t max_exact_runouts = 1'200U;
	};
#pragma pack(pop)

	/** Fixed-capacity action and blueprint-probability output from a provider. */
#pragma pack(push, 8)
	struct MultiwayRolloutActionMenu
	{
		std::array<MultiwayActionDescriptor, MULTIWAY_ROLLOUT_MAX_ACTIONS> actions{};
		std::array<Probability, MULTIWAY_ROLLOUT_MAX_ACTIONS> blueprint{};
		std::uint8_t count = 0;
	};
#pragma pack(pop)

	// The provider returns the blueprint menu at one exact betting state. It must
	// only return legal, unique actions and must not retain `board`.
	/** Callback that supplies legal actions and their blueprint probabilities. */
	using MultiwayRolloutActionProviderFn = bool (*)(
		const MultiwayFixedState& state,
		const std::uint8_t* board,
		std::uint8_t board_count,
		MultiwayRolloutActionMenu* output,
		const void* context) noexcept;

	// Converts the existing dynamic leaf request to a fixed continuation state.
	// Holes are ordered by seat and include folded seats to preserve settlement.
	/** Fixed-state cards, board, and settlement settings for one rollout. */
#pragma pack(push, 8)
	struct MultiwayRolloutInput
	{
		const MultiwayFixedState* state = nullptr;
		const std::array<std::array<std::uint8_t, 2>, kMultiwayFixedMaxSeats>* holes = nullptr;
		const std::uint8_t* board = nullptr;
		std::uint8_t board_count = 0;
		// First active seat on future streets, usually supplied from the public
		// hand state rather than inferred from a lossy betting snapshot.
		PlayerId next_street_first_player = 0;
		PlayerId odd_chip_first_seat = 0;
		MultiwayRakePolicy rake_policy = MultiwayRakePolicy::explicit_zero();
	};
#pragma pack(pop)

	/** Converts a leaf request into the fixed rollout input representation. */
	using MultiwayRolloutInputProviderFn = bool (*)(
		const MultiwayLeafEvaluationRequest& request,
		MultiwayRolloutInput* output,
		const void* context) noexcept;

	/** Caller-owned fixed-state buffers reused during a rollout evaluation. */
#pragma pack(push, 8)
	struct MultiwayRolloutScratch
	{
		MultiwayFixedState state{};
		std::array<std::array<std::uint8_t, 2>, kMultiwayFixedMaxSeats> holes{};
		std::array<std::uint8_t, MULTIWAY_ROLLOUT_MAX_BOARD_CARDS> board{};
		std::array<bool, 64U> used{};
		MultiwayFixedTerminalScratch terminal_scratch{};
		MultiwayFixedTerminalResult terminal_result{};
		std::array<Probability, MULTIWAY_ROLLOUT_MAX_ACTIONS> policy{};
	};
#pragma pack(pop)

	/** Values and runout counters for every fixed continuation policy. */
#pragma pack(push, 8)
	struct MultiwayRolloutProfileResult
	{
		std::array<Value, MULTIWAY_FIXED_CONTINUATION_POLICIES.size()> values{};
		MultiwayRolloutStatus status = MultiwayRolloutStatus::InvalidContext;
		MultiwayRolloutRunoutMode runout_mode = MultiwayRolloutRunoutMode::None;
		std::uint32_t seed_count = 0;
		std::uint32_t exact_runouts = 0;
		std::uint32_t betting_actions = 0;
	};
#pragma pack(pop)

	// Internal cache keys remain request-local and contain opaque identities, not
	// private cards or range rows. Public-state identity already binds the exact
	// betting history and legal action menu.
	/** Request identity for safely reusing one rollout profile from a local cache. */
#pragma pack(push, 8)
	struct MultiwayContinuationCacheKey
	{
		MultiwayPublicStateId public_state{};
		PlayerId traverser = -1;
		PlayerId actor = -1;
		std::uint32_t future_bucket = 0;
		std::uint64_t action_abstraction_version = 0;
		std::uint64_t leaf_model_version = 0;
		std::uint64_t range_context_identity = 0;
		std::uint64_t private_context_identity = 0;
		std::uint64_t seed_batch_identity = 0;
		std::uint64_t bias_factor_bits = 0;
		std::uint32_t seed_count = 0;
		std::uint32_t max_betting_actions = 0;
		std::uint32_t max_exact_runouts = 0;

		/** Returns whether the key contains all required identities and limits. */
		[[nodiscard]] bool valid() const noexcept;
		/** Tests equality of model and request context, excluding result state. */
		[[nodiscard]] bool same_context(const MultiwayContinuationCacheKey& other) const noexcept;
		/** Orders keys deterministically for sorted cache storage. */
		[[nodiscard]] bool operator<(const MultiwayContinuationCacheKey& other) const noexcept;
	};
#pragma pack(pop)

	/** Counters and repeated-seed measurements collected for rollout calls. */
#pragma pack(push, 8)
	struct MultiwayContinuationDiagnostics
	{
		std::uint64_t seed = 0;
		MultiwayRolloutRunoutMode runout_mode = MultiwayRolloutRunoutMode::None;
		std::uint64_t sample_count = 0;
		MultiwayContinuationPolicyKind policy_mode = MultiwayContinuationPolicyKind::Blueprint;
		std::uint64_t leaf_count = 0;
		std::uint64_t invalid_count = 0;
		std::uint64_t capped_count = 0;
		std::uint64_t cache_hits = 0;
		std::uint64_t cache_misses = 0;
		std::uint64_t cache_bypasses = 0;
		std::uint64_t cache_admission_rejections = 0;
		std::uint64_t repeated_seed_pairs = 0;
		std::array<Value, MULTIWAY_FIXED_CONTINUATION_POLICIES.size()> mean_policy_values{};
		std::array<double, MULTIWAY_FIXED_CONTINUATION_POLICIES.size()> repeated_seed_variance{};
	};
#pragma pack(pop)

	// Fixed-capacity, request-local cache. Construction performs the only reserve;
	// lookup and admission never grow storage beyond the configured byte cap.
	/** Bounded cache for rollout profiles keyed by complete request identity. */
	class MultiwayContinuationCache
	{
	public:
		/** Reserves up to max_entries within the configured byte limit. */
		explicit MultiwayContinuationCache(
			std::size_t max_entries,
			std::uint64_t max_bytes = std::numeric_limits<std::uint64_t>::max());

		/** Copies a cached profile to output when the complete key matches. */
		[[nodiscard]] bool find(
			const MultiwayContinuationCacheKey& key,
			MultiwayRolloutProfileResult* output) const noexcept;
		/** Inserts a profile if the key is valid and capacity remains. */
		[[nodiscard]] bool try_insert(
			const MultiwayContinuationCacheKey& key,
			const MultiwayRolloutProfileResult& value) noexcept;
		/** Updates repeated-seed variance diagnostics for a matching entry. */
		void record_repeated_seed_variance(
			const MultiwayContinuationCacheKey& key,
			const MultiwayRolloutProfileResult& value,
			MultiwayContinuationDiagnostics* diagnostics) const noexcept;

		/** Returns the number of retained cache entries. */
		[[nodiscard]] std::size_t size() const noexcept { return entries_.size(); }
		/** Returns the fixed maximum entry count. */
		[[nodiscard]] std::size_t capacity() const noexcept { return capacity_; }
		/** Returns the cache's allocated memory estimate. */
		[[nodiscard]] std::uint64_t memory_bytes() const noexcept;
		/** Returns a categorized memory-use snapshot. */
		[[nodiscard]] MultiwayMemoryComponent memory_snapshot() const noexcept;
		/** Returns the storage size of one cache entry. */
		[[nodiscard]] static constexpr std::uint64_t entry_bytes() noexcept;

	private:
#pragma pack(push, 8)
		struct Entry
		{
			MultiwayContinuationCacheKey key{};
			MultiwayRolloutProfileResult value{};
		};
#pragma pack(pop)

		std::size_t capacity_ = 0;
		std::vector<Entry> entries_;
	};

	constexpr std::uint64_t MultiwayContinuationCache::entry_bytes() noexcept
	{
		return sizeof(Entry);
	}

	/** Providers, seed batch, bounds, cache, diagnostics, and scratch for rollout. */
#pragma pack(push, 8)
	struct MultiwayRolloutLeafContext
	{
		MultiwayRolloutInputProviderFn provide_input = nullptr;
		const void* input_context = nullptr;
		MultiwayRolloutActionProviderFn provide_actions = nullptr;
		const void* action_context = nullptr;
		const std::uint64_t* seeds = nullptr;
		std::size_t seed_count = 0;
		MultiwayRolloutLimits limits{};
		Probability bias_factor = 5.0;
		// Used by direct callers without traversal provenance. Traversal supplies
		// the information-set-selected mode in MultiwayLeafEvaluationRequest.
		MultiwayContinuationPolicyKind selected_policy = MultiwayContinuationPolicyKind::Blueprint;
		// Cache and diagnostics are caller-owned and request-local. Use one pair
		// per concurrent caller; neither object performs internal synchronization.
		MultiwayContinuationCache* cache = nullptr;
		MultiwayContinuationDiagnostics* diagnostics = nullptr;
		// One scratch instance per concurrent caller. It is overwritten during
		// evaluation and must outlive the synchronous evaluator invocation.
		MultiwayRolloutScratch* scratch = nullptr;
	};
#pragma pack(pop)

	// Evaluates every fixed policy with the same seed batch. Complete all-in
	// runouts are exactly enumerated whenever their bounded space fits the cap.
	/** Evaluates all fixed policies using shared seeds or bounded exact runouts. */
	[[nodiscard]] bool evaluate_multiway_rollout_profiles(
		const MultiwayLeafEvaluationRequest& request,
		const MultiwayRolloutLeafContext& context,
		MultiwayRolloutProfileResult* output) noexcept;

	// Adapter retaining the existing MultiwayLeafEvaluator callback boundary.
	// It returns NaN for invalid contexts and otherwise selects the request's
	// information-set-consistent policy from the common-random-number profile.
	/** Adapts rollout profiles to the traversal leaf-evaluator callback. */
	[[nodiscard]] Value evaluate_multiway_rollout_leaf(
		const MultiwayLeafEvaluationRequest& request,
		const void* context) noexcept;
	/** Creates a leaf-evaluator callback over caller-owned rollout context. */
	[[nodiscard]] MultiwayLeafEvaluator make_multiway_rollout_leaf_evaluator(
		const MultiwayRolloutLeafContext* context) noexcept;

} // namespace texas::solver::multiway
