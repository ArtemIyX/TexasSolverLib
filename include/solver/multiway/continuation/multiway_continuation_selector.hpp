#pragma once

/** @file
 * Information-set keyed continuation-policy selection and regret updates.
 */

#include "solver/multiway/continuation/multiway_continuation_policy_kind.hpp"
#include "solver/multiway/engine/multiway_solver.hpp"

#include <cstdint>
#include <cstddef>
#include <array>
#include <mutex>
#include <shared_mutex>
#include <vector>

namespace texas::solver::multiway
{

	// This key deliberately contains only public state, the acting seat, and the
	// abstract future bucket. It must not acquire private cards or range weights.
	/** Public information-set identity for continuation-policy selection. */
	struct MultiwayContinuationSelectionKey
	{
		MultiwayPublicStateId public_state{};
		PlayerId actor = -1;
		Street street = Street::Preflop;
		std::uint32_t future_bucket = 0;
		std::uint64_t action_abstraction_version = 0;
		std::uint64_t leaf_model_version = 0;

		/** Returns whether the public identifiers form a valid selection key. */
		[[nodiscard]] bool valid() const noexcept;
	};

	/** One worker's ordered continuation regret update for an information set. */
	struct MultiwayContinuationDelta
	{
		MultiwayContinuationSelectionKey key{};
		std::array<double, MULTIWAY_FIXED_CONTINUATION_POLICIES.size()> mixture{};
		std::array<double, MULTIWAY_FIXED_CONTINUATION_POLICIES.size()> values{};
		std::uint64_t trajectory_id = 0U;
		std::uint32_t sequence = 0U;
		double importance_weight = 1.0;
	};

	/** Bounded per-worker stream of continuation-regret deltas. */
	class MultiwayContinuationDeltaStream
	{
	public:
		/** Reserves storage for capacity deltas associated with worker_index. */
		MultiwayContinuationDeltaStream(std::size_t worker_index, std::size_t capacity);

		/** Returns the worker index associated with this stream. */
		[[nodiscard]] std::size_t worker_index() const noexcept { return worker_index_; }
		/** Returns the number of currently buffered deltas. */
		[[nodiscard]] std::size_t size() const noexcept { return deltas_.size(); }
		/** Reports whether an append was rejected due to capacity. */
		[[nodiscard]] bool capacity_exhausted() const noexcept { return capacity_exhausted_; }
		/** Clears the capacity-exhausted marker. */
		void clear_capacity_exhaustion() noexcept { capacity_exhausted_ = false; }
		/** Returns the buffered deltas in their current order. */
		[[nodiscard]] const std::vector<MultiwayContinuationDelta>& deltas() const noexcept { return deltas_; }
		/** Appends a delta if capacity remains. */
		[[nodiscard]] bool try_append(const MultiwayContinuationDelta& delta) noexcept;
		/** Removes deltas back to the requested size. */
		void rewind(std::size_t size) noexcept;
		/** Orders deltas deterministically by their merge keys. */
		void sort_fixed_order() noexcept;
		/** Reports whether deltas are in deterministic merge order. */
		[[nodiscard]] bool is_fixed_order() const noexcept;
		/** Returns a memory-use snapshot for this stream. */
		[[nodiscard]] MultiwayMemoryComponent memory_snapshot() const noexcept;

	private:
		std::size_t worker_index_ = 0U;
		std::size_t capacity_ = 0U;
		bool capacity_exhausted_ = false;
		std::vector<MultiwayContinuationDelta> deltas_;
	};

	// First-phase selector: a versioned fixed policy for every matching abstract
	// information set. The structured key keeps later learned selection rows in
	// the same information-set namespace without permitting private-card leakage.
	/** Selects and learns among fixed policies using public information sets. */
	class MultiwayFixedContinuationSelector
	{
	public:
		/** Creates a selector with the configured fixed-policy prior. */
		explicit MultiwayFixedContinuationSelector(MultiwayContinuationPolicyKind policy) noexcept
			: policy_(policy) {}

		/** Returns whether the configured prior is a supported policy kind. */
		[[nodiscard]] bool valid() const noexcept;
		/** Selects the configured policy for a valid key. */
		[[nodiscard]] MultiwayContinuationPolicyKind select(
			const MultiwayContinuationSelectionKey& key) const;

		// Returns the regret-matched policy mixture for one public continuation
		// information set. Rows without positive regret use the configured prior.
		/** Returns regret matching probabilities, or the prior when regrets are zero. */
		[[nodiscard]] std::array<double, MULTIWAY_FIXED_CONTINUATION_POLICIES.size()>
		strategy(const MultiwayContinuationSelectionKey& key) const;

		// Adds one complete continuation-policy value vector to the owning row.
		// The update is serialized because traversal workers share this solver state.
		/** Updates regrets from one value vector under the supplied mixture. */
		void update_regrets(
			const MultiwayContinuationSelectionKey& key,
			const std::array<double, MULTIWAY_FIXED_CONTINUATION_POLICIES.size()>& mixture,
			const std::array<double, MULTIWAY_FIXED_CONTINUATION_POLICIES.size()>& values) const;
		/** Applies a weighted regret update for one continuation information set. */
		void update_regrets_weighted(
			const MultiwayContinuationSelectionKey& key,
			const std::array<double, MULTIWAY_FIXED_CONTINUATION_POLICIES.size()>& mixture,
			const std::array<double, MULTIWAY_FIXED_CONTINUATION_POLICIES.size()>& values,
			double importance_weight) const;

		// Installs the regret row for one abstract continuation information set.
		// Rows are request-owned and remain private-card independent.
		/** Replaces the regret values for a valid public information-set key. */
		void set_regrets(
			const MultiwayContinuationSelectionKey& key,
			const std::array<double, MULTIWAY_FIXED_CONTINUATION_POLICIES.size()>& regrets);

		/** Merges worker streams in deterministic order into selector rows. */
		void merge_worker_streams(
			const std::vector<const MultiwayContinuationDeltaStream*>& streams) const;
		/** Returns the selector's row and synchronization memory estimate. */
		[[nodiscard]] MultiwayMemoryComponent memory_snapshot() const noexcept;

	private:
		MultiwayContinuationPolicyKind policy_ = MultiwayContinuationPolicyKind::Blueprint;
		struct Row
		{
			MultiwayContinuationSelectionKey key{};
			std::array<double, MULTIWAY_FIXED_CONTINUATION_POLICIES.size()> regrets{};
		};
		mutable std::vector<Row> rows_;
		mutable std::shared_mutex mutex_;
	};

} // namespace texas::solver::multiway
