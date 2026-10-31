#pragma once

/** @file
 * Immutable in-memory storage and lookup views for multiway blueprint rows.
 */

#include "solver/multiway/blueprint/multiway_export.hpp"

#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

namespace texas::solver::multiway
{

	// Compact immutable runtime blueprint. Rows are sorted by public state, seat,
	// and bucket; action probabilities are quantized and action-menu bound.
	/** Owning representation of one infoset, bucket, and action menu policy row. */
#pragma pack(push, 8)
	struct MultiwayBlueprintRow
	{
		MultiwayInfosetId infoset{};
		std::uint32_t bucket = 0U;
		std::uint64_t action_menu_id = 0U;
		std::vector<MultiwayQuantizedRootAction> actions;

		/** Checks that the row has a valid identity and normalized action policy. */
		void validate() const;
	};
#pragma pack(pop)

	/** Non-owning access to a row's quantized actions. */
#pragma pack(push, 8)
	struct MultiwayBlueprintRowView
	{
		MultiwayInfosetId infoset{};
		std::uint32_t bucket = 0U;
		std::uint64_t action_menu_id = 0U;
		const MultiwayQuantizedRootAction* actions = nullptr;
		std::size_t action_count = 0U;

		/** Returns true when the view points to at least one action. */
		[[nodiscard]] bool valid() const noexcept { return actions != nullptr && action_count != 0U; }
	};
#pragma pack(pop)

	/** Sorted immutable store for runtime blueprint lookup. */
	class MultiwayBlueprintStore
	{
	public:
		/** Builds a store and validates and orders the supplied rows. */
		MultiwayBlueprintStore(MultiwayModelIdentity identity, std::vector<MultiwayBlueprintRow> rows);

		/** Returns the model identity attached to the stored rows. */
		[[nodiscard]] const MultiwayModelIdentity& identity() const noexcept { return identity_; }
		/** Returns the number of stored infoset-bucket rows. */
		[[nodiscard]] std::size_t row_count() const noexcept { return rows_.size(); }
		/** Estimates memory held by row indexes and quantized actions. */
		[[nodiscard]] std::uint64_t memory_bytes() const noexcept;
		/** Finds a row matching the infoset, bucket, and action-menu identity. */
		[[nodiscard]] MultiwayBlueprintRowView find(
			MultiwayInfosetId infoset,
			std::uint32_t bucket,
			std::uint64_t action_menu_id) const noexcept;
		/** Finds a row and reports whether the infoset and bucket exist on a miss. */
		[[nodiscard]] MultiwayBlueprintRowView find(
			MultiwayInfosetId infoset,
			std::uint32_t bucket,
			std::uint64_t action_menu_id,
			bool& infoset_present,
			bool& bucket_present) const noexcept;
		/** Tests whether any bucket row exists for the infoset. */
		[[nodiscard]] bool has_infoset(MultiwayInfosetId infoset) const noexcept;
		/** Tests whether a row exists for the infoset and bucket, regardless of menu. */
		[[nodiscard]] bool has_infoset_bucket(
			MultiwayInfosetId infoset, std::uint32_t bucket) const noexcept;

	private:
#pragma pack(push, 8)
		struct RuntimeRow
		{
			MultiwayInfosetId infoset{};
			std::uint32_t bucket = 0U;
			std::uint64_t action_menu_id = 0U;
			std::size_t action_offset = 0U;
			std::size_t action_count = 0U;
		};
#pragma pack(pop)

		MultiwayModelIdentity identity_{};
		std::vector<RuntimeRow> rows_;
		std::vector<MultiwayQuantizedRootAction> actions_;
	};

} // namespace texas::solver::multiway
