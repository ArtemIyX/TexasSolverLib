#pragma once

#include "solver/multiway/blueprint/multiway_blueprint_store.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>

namespace texas::solver::multiway
{

	enum class MultiwayBlueprintLookupStatus : std::uint8_t
	{
		Hit,
		Missing,
		MissingBucket,
		IncompatibleMenu,
	};

	struct MultiwayBlueprintLookupAudit
	{
		std::uint64_t lookup_hits = 0U;
		std::uint64_t missing_infosets = 0U;
		std::uint64_t missing_buckets = 0U;
		std::uint64_t action_menu_mismatches = 0U;

		void record(MultiwayBlueprintLookupStatus status, MultiwayInfosetId infoset,
			std::uint32_t bucket, std::uint64_t action_menu_id) noexcept;
		[[nodiscard]] std::uint64_t fingerprint() const noexcept;

	private:
		std::uint64_t fingerprint_state_ = 1469598103934665603ULL;
	};

	// Verified mapped shards keep serialized rows in the OS page cache. Row
	// offsets are sparse and bounded; strategy lookup decodes into caller scratch.
	class MultiwayMappedShardedBlueprintProvider
	{
	public:
		[[nodiscard]] static std::unique_ptr<MultiwayMappedShardedBlueprintProvider> load_verified(
			const std::filesystem::path& manifest_path,
			const MultiwayModelIdentity& expected_identity);
		[[nodiscard]] static std::unique_ptr<MultiwayMappedShardedBlueprintProvider> load_verified_for_policy_lookup(
			const std::filesystem::path& manifest_path,
			const MultiwayModelIdentity& expected_identity);
		[[nodiscard]] static std::unique_ptr<MultiwayMappedShardedBlueprintProvider> load_verified(
			const std::filesystem::path& manifest_path,
			const MultiwayModelIdentity& expected_identity,
			bool allow_runtime_identity_difference);
		~MultiwayMappedShardedBlueprintProvider();
		MultiwayMappedShardedBlueprintProvider(const MultiwayMappedShardedBlueprintProvider&) = delete;
		MultiwayMappedShardedBlueprintProvider& operator=(const MultiwayMappedShardedBlueprintProvider&) = delete;

		[[nodiscard]] MultiwayBlueprintLookupStatus strategy_into(
			MultiwayInfosetId infoset,
			std::uint32_t bucket,
			const MultiwayActionDescriptor* legal_actions,
			std::size_t action_count,
			Probability* output) const noexcept;
		[[nodiscard]] std::uint64_t mapped_virtual_bytes() const noexcept;
		[[nodiscard]] std::uint64_t sparse_index_bytes() const noexcept;
		[[nodiscard]] const MultiwayModelIdentity& identity() const noexcept;
		[[nodiscard]] std::uint64_t policy_content_hash() const noexcept;

	private:
		struct Impl;
		explicit MultiwayMappedShardedBlueprintProvider(std::unique_ptr<Impl> impl) noexcept;
		std::unique_ptr<Impl> impl_;
	};

	// Read-only, lock-free policy lookup for traversal. A miss is explicit so
	// callers can select their configured local or static fallback.
	class MultiwayBlueprintPolicyProvider
	{
	public:
		explicit MultiwayBlueprintPolicyProvider(const MultiwayBlueprintStore& store) noexcept : store_(&store) {}
		explicit MultiwayBlueprintPolicyProvider(const MultiwayMappedShardedBlueprintProvider& store) noexcept
			: mapped_store_(&store)
		{
		}

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
