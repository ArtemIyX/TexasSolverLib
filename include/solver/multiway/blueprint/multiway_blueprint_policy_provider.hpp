#pragma once

/** @file
 * Runtime policy lookup interfaces and bounded missing-row diagnostics.
 */

#include "solver/multiway/blueprint/multiway_blueprint_store.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <vector>

namespace texas::solver::multiway
{

	/** Outcome of looking up a policy row for a runtime action menu. */
	enum class MultiwayBlueprintLookupStatus : std::uint8_t
	{
		Hit,
		Missing,
		MissingBucket,
		IncompatibleMenu,
	};

	inline constexpr std::size_t MULTIWAY_MAX_LOOKUP_DIAGNOSTIC_SAMPLES = 16U;
	inline constexpr std::size_t MULTIWAY_MAX_LOOKUP_DIAGNOSTIC_HISTORY = 64U;
	inline constexpr std::size_t MULTIWAY_MAX_LOOKUP_DIAGNOSTIC_ACTIONS = 8U;

	// Public-only identity inputs for one bounded missing-infoset sample.
	/** Bounded public-state sample retained to diagnose a blueprint lookup miss. */
	struct MultiwayBlueprintLookupDiagnostic
	{
		MultiwayBlueprintLookupStatus status = MultiwayBlueprintLookupStatus::Missing;
		std::uint64_t trajectory_id = 0U;
		std::uint64_t replay_lookup_ordinal = 0U;
		MultiwayInfosetId infoset{};
		std::uint64_t reconstructed_public_state_id = 0U;
		std::uint32_t bucket = 0U;
		std::uint64_t action_menu_id = 0U;
		std::uint64_t canonical_history_id = 0U;
		bool identity_context_complete = false;
		std::uint8_t street = 0U;
		std::uint8_t board_count = 0U;
		std::array<std::uint8_t, 5U> board{};
		std::uint8_t seat_count = 0U;
		std::array<int, 6U> stacks{};
		std::array<int, 6U> contributions{};
		std::array<int, 6U> street_contributions{};
		std::array<int, 6U> bet_faced_when_acted{};
		std::array<std::uint8_t, 6U> folded{};
		std::array<std::uint8_t, 6U> all_in{};
		std::array<std::uint8_t, 6U> may_raise{};
		std::array<std::uint8_t, 6U> pending{};
		std::array<std::uint8_t, 6U> has_acted{};
		std::int32_t current_player = -1;
		std::int32_t last_aggressor = -1;
		int current_bet = 0;
		int last_full_raise_size = 0;
		int big_blind = 0;
		std::uint32_t history_count = 0U;
		std::uint8_t history_stored_count = 0U;
		std::array<MultiwayPublicHistoryEntry, MULTIWAY_MAX_LOOKUP_DIAGNOSTIC_HISTORY> history{};
		std::uint8_t legal_action_count = 0U;
		std::array<MultiwayActionDescriptor, MULTIWAY_MAX_LOOKUP_DIAGNOSTIC_ACTIONS> legal_actions{};
	};

	/** Aggregates lookup outcomes and deterministic diagnostic samples. */
	struct MultiwayBlueprintLookupAudit
	{
		std::uint64_t lookup_hits = 0U;
		std::uint64_t missing_infosets = 0U;
		std::uint64_t missing_buckets = 0U;
		std::uint64_t action_menu_mismatches = 0U;

		/** Sets the bounded diagnostic sample budget for the requested trajectories. */
		void set_diagnostic_sample_limit(std::size_t limit, std::uint64_t trajectory_count);
		/** Records a lookup result and optional public-state context. */
		void record(MultiwayBlueprintLookupStatus status, MultiwayInfosetId infoset,
			std::uint32_t bucket, std::uint64_t action_menu_id,
			std::uint64_t trajectory_id = 0U,
			const MultiwayPublicStateDescriptor* public_state = nullptr) noexcept;
		/** Returns a stable fingerprint of recorded lookup outcomes. */
		[[nodiscard]] std::uint64_t fingerprint() const noexcept;
		/** Returns the retained bounded diagnostic samples. */
		[[nodiscard]] const std::vector<MultiwayBlueprintLookupDiagnostic>& diagnostic_samples() const noexcept
		{
			return diagnostic_samples_;
		}

	private:
		std::uint64_t fingerprint_state_ = 1469598103934665603ULL;
		std::uint64_t lookup_ordinal_ = 0U;
		std::size_t diagnostic_sample_limit_ = 0U;
		std::array<std::uint64_t, MULTIWAY_MAX_LOOKUP_DIAGNOSTIC_SAMPLES> diagnostic_sample_priorities_{};
		std::vector<MultiwayBlueprintLookupDiagnostic> diagnostic_samples_;
	};

	// Verified mapped shards keep serialized rows in the OS page cache. Row
	// offsets are sparse and bounded; strategy lookup decodes into caller scratch.
	/** Policy provider that looks up verified rows directly from mapped shards. */
	class MultiwayMappedShardedBlueprintProvider
	{
	public:
		/** Loads and verifies a sharded artifact for exact model identity. */
		[[nodiscard]] static std::unique_ptr<MultiwayMappedShardedBlueprintProvider> load_verified(
			const std::filesystem::path& manifest_path,
			const MultiwayModelIdentity& expected_identity);
		/** Loads and verifies a sharded artifact for runtime policy lookup. */
		[[nodiscard]] static std::unique_ptr<MultiwayMappedShardedBlueprintProvider> load_verified_for_policy_lookup(
			const std::filesystem::path& manifest_path,
			const MultiwayModelIdentity& expected_identity);
		/** Loads and verifies a sharded artifact with optional runtime-version tolerance. */
		[[nodiscard]] static std::unique_ptr<MultiwayMappedShardedBlueprintProvider> load_verified(
			const std::filesystem::path& manifest_path,
			const MultiwayModelIdentity& expected_identity,
			bool allow_runtime_identity_difference);
		~MultiwayMappedShardedBlueprintProvider();
		MultiwayMappedShardedBlueprintProvider(const MultiwayMappedShardedBlueprintProvider&) = delete;
		MultiwayMappedShardedBlueprintProvider& operator=(const MultiwayMappedShardedBlueprintProvider&) = delete;

		/** Looks up action probabilities into caller-provided output storage. */
		[[nodiscard]] MultiwayBlueprintLookupStatus strategy_into(
			MultiwayInfosetId infoset,
			std::uint32_t bucket,
			const MultiwayActionDescriptor* legal_actions,
			std::size_t action_count,
			Probability* output) const noexcept;
		/** Returns mapped virtual bytes, including mapped artifact pages. */
		[[nodiscard]] std::uint64_t mapped_virtual_bytes() const noexcept;
		/** Returns memory used by the sparse row index. */
		[[nodiscard]] std::uint64_t sparse_index_bytes() const noexcept;
		/** Returns the verified model identity. */
		[[nodiscard]] const MultiwayModelIdentity& identity() const noexcept;
		/** Returns the verified policy-content hash. */
		[[nodiscard]] std::uint64_t policy_content_hash() const noexcept;

	private:
		struct Impl;
		explicit MultiwayMappedShardedBlueprintProvider(std::unique_ptr<Impl> impl) noexcept;
		std::unique_ptr<Impl> impl_;
	};

	// Read-only, lock-free policy lookup for traversal. A miss is explicit so
	// callers can select their configured local or static fallback.
	/** Dispatches policy lookups to an in-memory or mapped immutable store. */
	class MultiwayBlueprintPolicyProvider
	{
	public:
		/** Binds lookup to an in-memory immutable blueprint store. */
		explicit MultiwayBlueprintPolicyProvider(const MultiwayBlueprintStore& store) noexcept : store_(&store) {}
		/** Binds lookup to a mapped sharded blueprint provider. */
		explicit MultiwayBlueprintPolicyProvider(const MultiwayMappedShardedBlueprintProvider& store) noexcept
			: mapped_store_(&store)
		{
		}

		/** Writes the requested action probabilities and returns the lookup outcome. */
		[[nodiscard]] MultiwayBlueprintLookupStatus strategy_into(
			MultiwayInfosetId infoset,
			std::uint32_t bucket,
			const MultiwayActionDescriptor* legal_actions,
			std::size_t action_count,
			Probability* output) const noexcept;

	private:
		const MultiwayBlueprintStore* store_ = nullptr;
		const MultiwayMappedShardedBlueprintProvider* mapped_store_ = nullptr;
	};

} // namespace texas::solver::multiway
