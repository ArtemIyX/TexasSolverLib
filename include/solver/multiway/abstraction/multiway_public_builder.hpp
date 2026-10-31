#pragma once

/** @file
 * @brief Canonicalizes public multiway histories, action menus and stable identities.
 */

#include "solver/multiway/engine/multiway_solver.hpp"
#include "solver/multiway/abstraction/multiway_terminal_adapter.hpp"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace texas::solver::multiway
{

	class MultiwayResolver;

	/** @brief Identity values and validation outcome for one public descriptor. */
#pragma pack(push, 8)
	struct MultiwayPublicIdentityInspection
	{
		std::uint64_t canonical_history_id = 0U;
		MultiwayPublicStateId public_state_id{};
		bool history_valid = false;
	};
#pragma pack(pop)

	// Cold-path factory for coordinator-admitted public descriptors. It owns no
	// tree storage and never selects strategic bet sizes; Phase 2 supplies target
	// contributions for Bet and Raise actions.
	/** @brief Cold-path factory and identity verifier for admitted public states. */
	class MultiwayPublicBuilder
	{
	public:
		// Schema-v2 canonical action identity. The returned menu is sorted by
		// (action, target contribution), has contiguous indices, and has one
		// derived non-zero menu id on every entry.
		/** @brief Sorts menu entries, assigns contiguous indices and a stable menu ID. */
		[[nodiscard]] static std::vector<MultiwayActionDescriptor> canonicalize_action_menu(
			const MultiwayBettingSnapshot& betting,
			std::vector<MultiwayActionDescriptor> menu);

		/** @brief Computes the schema-v2 ID of a canonical action menu. */
		[[nodiscard]] static std::uint64_t stable_action_menu_id(
			const std::vector<MultiwayActionDescriptor>& canonical_menu) noexcept;

		/** @brief Creates legal descriptors using supplied target contributions. */
		[[nodiscard]] static std::vector<MultiwayActionDescriptor> make_legal_actions(
			const MultiwayBettingSnapshot& betting,
			const std::vector<int>& target_street_contributions);

		/** @brief Creates the root descriptor from betting, board and legal actions. */
		[[nodiscard]] static MultiwayPublicStateDescriptor make_root(
			const MultiwayBettingSnapshot& betting,
			std::vector<std::uint8_t> board,
			std::vector<MultiwayActionDescriptor> legal_actions);

		/** @brief Creates an action child and derives its betting state. */
		[[nodiscard]] static MultiwayPublicStateDescriptor make_action_child(
			const MultiwayPublicStateDescriptor& parent,
			std::uint32_t action_index,
			std::vector<MultiwayActionDescriptor> child_legal_actions);

		/** @brief Creates an action child using explicit child betting data. */
		[[nodiscard]] static MultiwayPublicStateDescriptor make_action_child(
			const MultiwayPublicStateDescriptor& parent,
			std::uint32_t action_index,
			MultiwayBettingSnapshot child_betting,
			std::vector<MultiwayActionDescriptor> child_legal_actions);

		/** @brief Creates an action child from a pointer/count action-menu view. */
		[[nodiscard]] static MultiwayPublicStateDescriptor make_action_child(
			const MultiwayPublicStateDescriptor& parent,
			std::uint32_t action_index,
			MultiwayBettingSnapshot child_betting,
			const MultiwayActionDescriptor* child_legal_actions,
			std::size_t child_action_count);

		/** @brief Creates a child descriptor for a canonical board chance edge. */
		[[nodiscard]] static MultiwayPublicStateDescriptor make_board_chance_child(
			const MultiwayPublicStateDescriptor& parent,
			const MultiwayPublicBoardChanceEdge& edge,
			std::vector<MultiwayActionDescriptor> child_legal_actions);

		/** @brief Creates a child descriptor for a directly sampled board edge. */
		[[nodiscard]] static MultiwayPublicStateDescriptor make_board_chance_child(
			const MultiwayPublicStateDescriptor& parent,
			const MultiwaySampledPublicBoardChance& edge,
			std::vector<MultiwayActionDescriptor> child_legal_actions);

		/** @brief Creates a child descriptor for a completed street transition. */
		[[nodiscard]] static MultiwayPublicStateDescriptor make_street_transition_child(
			const MultiwayPublicStateDescriptor& parent,
			const MultiwayPublicStreetTransition& transition,
			std::vector<MultiwayActionDescriptor> child_legal_actions);

		// Schema-v2 uses tagged, length-prefixed bytes. History contains ordered
		// actor/action/target/menu-id records. Public state contains the complete
		// betting snapshot, canonical sorted board, ordered history, and canonical
		// action menu. Parent links and diagnostic fields are deliberately absent.
		/** @brief Hashes the ordered schema-v2 public-history records. */
		[[nodiscard]] static std::uint64_t stable_history_id(
			const std::vector<MultiwayPublicHistoryEntry>& history) noexcept;

		/** @brief Hashes betting, canonical board, history and canonical menu. */
		[[nodiscard]] static std::uint64_t stable_public_state_id(
			const MultiwayBettingSnapshot& betting,
			const std::vector<std::uint8_t>& board,
			const std::vector<MultiwayPublicHistoryEntry>& history,
			const std::vector<MultiwayActionDescriptor>& legal_actions) noexcept;

		// Admission verifier for schema-v2 history shape and identities. It keeps
		// the reference identity functions above independent for regression checks.
		/** @brief Validates identity shape and returns derived IDs. */
		[[nodiscard]] static MultiwayPublicIdentityInspection inspect_schema_v2_identity(
			const MultiwayBettingSnapshot& betting,
			const std::vector<std::uint8_t>& board,
			const std::vector<MultiwayPublicHistoryEntry>& history,
			const std::vector<MultiwayActionDescriptor>& legal_actions) noexcept;

		/** @brief Non-throwing identity inspection that writes output on success. */
		[[nodiscard]] static bool inspect_schema_v2_identity(
			const MultiwayBettingSnapshot& betting,
			const std::vector<std::uint8_t>& board,
			const std::vector<MultiwayPublicHistoryEntry>& history,
			const std::vector<MultiwayActionDescriptor>& legal_actions,
			MultiwayPublicIdentityInspection& output) noexcept;

		// Lossless live-round key. This aliases the schema-v2 public-state
		// encoding, which already includes the full current history, exact target
		// contributions, active seats in the betting snapshot, board, and menu.
		/** @brief Computes the full-history key for the current public round. */
		[[nodiscard]] static std::uint64_t stable_lossless_current_round_key(
			const MultiwayBettingSnapshot& betting,
			const std::vector<std::uint8_t>& board,
			const std::vector<MultiwayPublicHistoryEntry>& history,
			const std::vector<MultiwayActionDescriptor>& legal_actions) noexcept;

	private:
		friend class MultiwayResolver;
	};

} // namespace texas::solver::multiway
