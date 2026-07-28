#pragma once

#include "solver/multiway_solver.hpp"

#include <array>
#include <cstdint>
#include <utility>
#include <vector>

namespace core {

// One canonical public chance outcome. The board is complete through this
// edge, so consumers never need to reconstruct a runout from a card label.
struct MultiwayBoardChanceEdge {
    std::uint8_t dealt_card = 0;
    std::vector<std::uint8_t> dealt_cards;
    std::vector<std::uint8_t> board;
    MultiwayBoardRunoutState board_runout{};
    Probability probability = 0.0;
};

// Result of a root-owned non-runout street transition. Board chance must have
// been applied before calling this operation.
struct MultiwayStreetTransition {
    MultiwayBettingSnapshot betting;
    std::vector<std::uint8_t> board;
    MultiwayBoardRunoutState board_runout{};
};

// Admission-ready binding for a canonical public chance successor. The caller
// assigns its coordinator id/history/action menu, while retaining this parent
// identity and typed edge unchanged.
struct MultiwayPublicBoardChanceEdge {
    MultiwayPublicStateId parent_id{};
    MultiwayPublicParentEdge incoming_edge{};
    MultiwayBoardChanceEdge chance{};
};

struct MultiwayPublicStreetTransition {
    MultiwayPublicStateId parent_id{};
    MultiwayPublicParentEdge incoming_edge{};
    MultiwayStreetTransition transition{};
};

// An opaque private deal issued only by a coordinator-bound terminal adapter.
// It cannot be constructed or inspected by traversal callers.
class MultiwaySamplerDealToken {
public:
    MultiwaySamplerDealToken() = delete;

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
class MultiwayTerminalAdapter {
public:
    explicit MultiwayTerminalAdapter(const MultiwaySolverCoordinator& coordinator);

    [[nodiscard]] MultiwaySamplerDealToken sample_private_deal(std::uint64_t seed) const;

    // Exposes one already-sampled private hand only to traversal code that is
    // bound to this adapter's coordinator. It cannot sample or mutate ranges.
    [[nodiscard]] std::array<std::uint8_t, 2> sampled_hole(
        const MultiwaySamplerDealToken& private_deal,
        PlayerId seat) const;

    [[nodiscard]] MultiwayExternalSamplingRequest make_external_sampling_request(
        const MultiwaySamplerDealToken& private_deal,
        std::vector<Probability> player_reaches,
        PlayerId traverser,
        std::vector<Probability> strategy,
        std::vector<Value> sampled_action_values) const;

    // Returns canonical public chance edges after excluding the sampled
    // private deal. Preflop-to-flop edges deal sorted three-card combinations;
    // all later edges deal one card.
    [[nodiscard]] std::vector<MultiwayBoardChanceEdge> canonical_board_chance_edges(
        MultiwayPublicStateId public_state,
        const MultiwaySamplerDealToken& private_deal) const;

    [[nodiscard]] std::vector<MultiwayPublicBoardChanceEdge> canonical_public_board_chance_edges(
        MultiwayPublicStateId parent_id,
        const MultiwaySamplerDealToken& private_deal) const;

    // Applies the root's fixed next-street seat. The supplied board must be
    // exactly complete for the immediately following street.
    [[nodiscard]] MultiwayStreetTransition apply_street_transition(
        MultiwayPublicStateId public_state) const;

    [[nodiscard]] MultiwayPublicStreetTransition apply_public_street_transition(
        MultiwayPublicStateId parent_id) const;

    // Resolves exactly a fold terminal or a completed showdown/runout by
    // delegating to the established terminal and showdown layers.
    [[nodiscard]] MultiwayTerminalResult resolve_terminal(
        MultiwayPublicStateId public_state,
        const MultiwaySamplerDealToken& private_deal) const;

private:
    [[nodiscard]] const MultiwayPublicStateDescriptor& require_public_state(
        MultiwayPublicStateId id) const;
    void validate_token(const MultiwaySamplerDealToken& token) const;

    [[nodiscard]] std::vector<MultiwayBoardChanceEdge> canonical_board_chance_edges_impl(
        const MultiwayBettingSnapshot& betting,
        const std::vector<std::uint8_t>& board,
        const MultiwayJointPrivateSample& private_deal) const;
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

}  // namespace core
