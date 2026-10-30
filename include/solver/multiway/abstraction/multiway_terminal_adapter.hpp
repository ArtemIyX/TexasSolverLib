#pragma once

/** @file
 * @brief Coordinator-bound multiway chance, street-transition and terminal adapter.
 */

#include "solver/multiway/engine/multiway_solver.hpp"

#include <array>
#include <cstdint>
#include <utility>
#include <vector>

namespace texas::solver::multiway
{

	// One canonical public chance outcome. The board is complete through this
	// edge, so consumers never need to reconstruct a runout from a card label.
	/** @brief Canonical public chance outcome with its complete resulting board. */
	struct MultiwayBoardChanceEdge
	{
		std::uint8_t dealt_card = 0;
		std::vector<std::uint8_t> dealt_cards;
		std::vector<std::uint8_t> board;
		MultiwayBoardRunoutState board_runout{};
		Probability probability = 0.0;
	};

	// Result of a root-owned non-runout street transition. Board chance must have
	// been applied before calling this operation.
	/** @brief Betting and board state after a non-runout street transition. */
	struct MultiwayStreetTransition
	{
		MultiwayBettingSnapshot betting;
		std::vector<std::uint8_t> board;
		MultiwayBoardRunoutState board_runout{};
	};

	// Admission-ready binding for a canonical public chance successor. The caller
	// assigns its coordinator id/history/action menu, while retaining this parent
	// identity and typed edge unchanged.
	/** @brief Chance successor bound to its public parent identity and typed edge. */
	struct MultiwayPublicBoardChanceEdge
	{
		MultiwayPublicStateId parent_id{};
		MultiwayPublicParentEdge incoming_edge{};
		MultiwayBoardChanceEdge chance{};
	};

	/** @brief Street-transition successor bound to parent identity and edge. */
	struct MultiwayPublicStreetTransition
	{
		MultiwayPublicStateId parent_id{};
		MultiwayPublicParentEdge incoming_edge{};
		MultiwayStreetTransition transition{};
	};

	// One directly sampled public chance edge. Fixed storage avoids materializing
	// the complete chance menu in traversal. Flop cards are canonical sorted.
	/** @brief Fixed-capacity sampled public chance outcome for traversal. */
	struct MultiwaySampledPublicBoardChance
	{
		MultiwayPublicStateId parent_id{};
		std::array<std::uint8_t, 3> dealt_cards{};
		std::uint8_t dealt_card_count = 0;
		std::array<std::uint8_t, 5> board{};
		std::uint8_t board_count = 0;
		MultiwayBoardRunoutState board_runout{};
		Probability probability = 0.0;
	};

	/** @brief Chance and proposal reach probabilities for one private deal. */
	struct MultiwayPrivateSamplingReach
	{
		Probability chance_reach = 0.0;
		Probability proposal_reach = 0.0;
	};

	// An opaque private deal issued only by a coordinator-bound terminal adapter.
	// It cannot be constructed or inspected by traversal callers.
	/** @brief Opaque private deal token bound to a solver coordinator. */
	class MultiwaySamplerDealToken
	{
	public:
		/** @brief Prevents creation of an unbound token. */
		MultiwaySamplerDealToken() = delete;
		/** @brief Reports whether a private deal is bound to this token. */
		[[nodiscard]] bool valid() const noexcept { return !deal_.holes.empty(); }

	private:
		friend class MultiwayTerminalAdapter;
		MultiwaySamplerDealToken(
			const MultiwaySolverCoordinator& coordinator,
			MultiwayJointPrivateSample deal)
			: coordinator_(&coordinator), deal_(std::move(deal)) {}
		const MultiwaySolverCoordinator* coordinator_ = nullptr;
		MultiwayJointPrivateSample deal_{};
	};

	// Cold integrated boundary for public chance, betting transitions, and
	// terminal settlement. It owns no traversal or policy state.
	/** @brief Cold-path adapter for public chance, betting transitions and settlement. */
	class MultiwayTerminalAdapter
	{
	public:
		/** @brief Binds the adapter to an admitted solver coordinator. */
		explicit MultiwayTerminalAdapter(const MultiwaySolverCoordinator& coordinator);

		/** @brief Samples a private deal and issues an opaque bound token. */
		[[nodiscard]] MultiwaySamplerDealToken sample_private_deal(std::uint64_t seed) const;

		// Exposes one already-sampled private hand only to traversal code that is
		// bound to this adapter's coordinator. It cannot sample or mutate ranges.
		/** @brief Returns one seat's sampled private hand after coordinator validation. */
		[[nodiscard]] std::array<std::uint8_t, 2> sampled_hole(
			const MultiwaySamplerDealToken& private_deal,
			PlayerId seat) const;

		/** @brief Returns the private deal's chance and proposal reach. */
		[[nodiscard]] MultiwayPrivateSamplingReach sampled_reach(
			const MultiwaySamplerDealToken& private_deal) const;

		/** @brief Constructs an external-sampling value request for an admitted deal. */
		[[nodiscard]] MultiwayExternalSamplingRequest make_external_sampling_request(
			const MultiwaySamplerDealToken& private_deal,
			std::vector<Probability> player_reaches,
			PlayerId traverser,
			std::vector<Probability> strategy,
			std::vector<Value> sampled_action_values) const;

		// Returns canonical public chance edges after excluding the sampled
		// private deal. Preflop-to-flop edges deal sorted three-card combinations;
		// all later edges deal one card.
		/** @brief Enumerates canonical chance successors after excluding private cards. */
		[[nodiscard]] std::vector<MultiwayBoardChanceEdge> canonical_board_chance_edges(
			MultiwayPublicStateId public_state,
			const MultiwaySamplerDealToken& private_deal) const;

		/** @brief Enumerates canonical chance successors with parent-edge metadata. */
		[[nodiscard]] std::vector<MultiwayPublicBoardChanceEdge> canonical_public_board_chance_edges(
			MultiwayPublicStateId parent_id,
			const MultiwaySamplerDealToken& private_deal) const;

		// Samples one canonical outcome without building the full edge list.
		// random_state is caller-owned trajectory state and advances only here.
		/** @brief Samples one canonical public chance successor and advances random_state. */
		[[nodiscard]] MultiwaySampledPublicBoardChance sample_public_board_chance(
			MultiwayPublicStateId parent_id,
			const MultiwaySamplerDealToken& private_deal,
			std::uint64_t& random_state) const;

		// Applies the root's fixed next-street seat. The supplied board must be
		// exactly complete for the immediately following street.
		/** @brief Applies the root's fixed next-street transition. */
		[[nodiscard]] MultiwayStreetTransition apply_street_transition(
			MultiwayPublicStateId public_state) const;

		/** @brief Applies the transition and attaches its public parent-edge identity. */
		[[nodiscard]] MultiwayPublicStreetTransition apply_public_street_transition(
			MultiwayPublicStateId parent_id) const;

		// Resolves exactly a fold terminal or a completed showdown/runout by
		// delegating to the established terminal and showdown layers.
		/** @brief Settles an admitted fold or completed showdown state. */
		[[nodiscard]] MultiwayTerminalResult resolve_terminal(
			MultiwayPublicStateId public_state,
			const MultiwaySamplerDealToken& private_deal) const;

	private:
		friend class MultiwayRootExternalSamplingTraversal;

		[[nodiscard]] MultiwaySampledPublicBoardChance sample_admitted_public_board_chance(
			const MultiwayPublicStateDescriptor& parent,
			const MultiwaySamplerDealToken& private_deal,
			std::uint64_t& random_state) const;
		[[nodiscard]] MultiwayPublicStreetTransition apply_admitted_public_street_transition(
			const MultiwayPublicStateDescriptor& parent) const;
		[[nodiscard]] MultiwayTerminalResult resolve_admitted_terminal(
			const MultiwayPublicStateDescriptor& state,
			const MultiwaySamplerDealToken& private_deal) const;
		[[nodiscard]] Value resolve_admitted_terminal_value(
			const MultiwayPublicStateDescriptor& state,
			const MultiwaySamplerDealToken& private_deal,
			PlayerId seat) const;
		[[nodiscard]] const MultiwayPublicStateDescriptor& require_public_state(
			MultiwayPublicStateId id) const;
		void validate_token(const MultiwaySamplerDealToken& token) const;

		[[nodiscard]] std::vector<MultiwayBoardChanceEdge> canonical_board_chance_edges_impl(
			const MultiwayBettingSnapshot& betting,
			const std::vector<std::uint8_t>& board,
			const MultiwayJointPrivateSample& private_deal) const;
		[[nodiscard]] MultiwaySampledPublicBoardChance sample_public_board_chance_impl(
			MultiwayPublicStateId parent_id,
			const MultiwayBettingSnapshot& betting,
			const std::vector<std::uint8_t>& board,
			const MultiwayJointPrivateSample& private_deal,
			std::uint64_t& random_state) const;
		[[nodiscard]] MultiwaySampledPublicBoardChance sample_validated_public_board_chance(
			MultiwayPublicStateId parent_id,
			Street street,
			MultiwayNextNodeKind kind,
			const std::vector<std::uint8_t>& board,
			const MultiwayJointPrivateSample& private_deal,
			std::uint64_t& random_state) const;
		[[nodiscard]] MultiwayStreetTransition apply_street_transition_impl(
			const MultiwayBettingSnapshot& betting,
			const std::vector<std::uint8_t>& board) const;
		[[nodiscard]] MultiwayTerminalResult resolve_terminal_impl(
			const MultiwayBettingSnapshot& betting,
			const std::vector<std::uint8_t>& board,
			const MultiwayJointPrivateSample& private_deal) const;

		const MultiwaySolverCoordinator* coordinator_ = nullptr;
		MultiwayRootSnapshot root_;
	};

} // namespace texas::solver::multiway
