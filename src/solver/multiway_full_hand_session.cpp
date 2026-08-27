#include "solver/multiway_full_hand_session.hpp"
#include "solver/multiway_public_builder.hpp"
#include "solver/multiway_artifact.hpp"
#include "core/canonical_combo.hpp"

#include <array>
#include <algorithm>
#include <stdexcept>
#include <chrono>
#include "games/hunl_eval.hpp"

namespace texas::solver::multiway {

MultiwayFullHandSession::MultiwayFullHandSession(
    games::multiway::MultiwayGameRules rules,
    core::PlayerId first_player,
    std::uint64_t hand_seed)
    : history_(games::multiway::MultiwayHandHistory::from_rules(rules, first_player, hand_seed)),
      state_(games::multiway::MultiwayState::initial(rules, first_player)) {
    std::array<MultiwayRangeBeliefSeatInput, MULTIWAY_RANGE_BELIEF_MAX_SEATS> inputs{};
    beliefs_.reset_uniform(rules.player_count, inputs.data());
}

const games::multiway::MultiwayState& MultiwayFullHandSession::observe(
    const games::multiway::MultiwayReplayEvent& event,
    const MultiwayRangeBeliefObservation* observation) {
    if (event.kind == games::multiway::MultiwayReplayEventKind::Decision && observation != nullptr) {
        if (beliefs_.apply_observation(
                static_cast<std::size_t>(event.decision.acting_seat), *observation) ==
            MultiwayRangeBeliefUpdateResult::NoPosteriorMass) {
            throw std::invalid_argument("full-hand observation has no posterior mass");
        }
    }
    state_ = games::multiway::apply_multiway_replay_event(state_, event);
    history_.events.push_back(event);
    if (event.kind == games::multiway::MultiwayReplayEventKind::StreetTransition) {
        board_ = event.board;
        clear_actual_hand_policy();
    }
    return state_;
}

MultiwayResolverResult MultiwayFullHandSession::decide(
    PlayerId hero_seat, std::array<std::uint8_t, 2> hero_cards,
    const MultiwayResolverConfig& config,
    std::chrono::steady_clock::time_point deadline) const {
    if (frozen_policy_.has_value()) return *frozen_policy_;
    if (hero_seat < 0 || static_cast<std::size_t>(hero_seat) >= beliefs_.seat_count()) {
        throw std::invalid_argument("full-hand hero seat is invalid");
    }
    MultiwayResolverRequest request;
    request.hero_seat = hero_seat;
    request.hero_cards = hero_cards;
    request.deadline = deadline;
    request.blueprint_identity = config.verified_blueprint == nullptr
        ? MultiwayModelIdentity{} : config.verified_blueprint->snapshot.identity;
    request.public_state = MultiwayPublicBuilder::make_root(
        state_.snapshot(), board_, MultiwayPublicBuilder::make_legal_actions(state_.snapshot(), {}));
    request.hero_range.reserve(CANONICAL_HOLE_COMBINATION_COUNT);
    for (std::size_t id = 0U; id < CANONICAL_HOLE_COMBINATION_COUNT; ++id) {
        const auto view = beliefs_.view(static_cast<std::size_t>(hero_seat));
        if (view.legal(static_cast<CanonicalComboId>(id)) && view.weight(static_cast<CanonicalComboId>(id)) > 0.0) {
            request.hero_range.push_back({canonical_combos().cards(static_cast<CanonicalComboId>(id)), view.weight(static_cast<CanonicalComboId>(id))});
        }
    }
    for (std::size_t seat = 0U; seat < beliefs_.seat_count(); ++seat) {
        if (static_cast<PlayerId>(seat) == hero_seat) continue;
        MultiwayResolverSeatRange range;
        range.seat = static_cast<PlayerId>(seat);
        const auto view = beliefs_.view(seat);
        for (std::size_t id = 0U; id < CANONICAL_HOLE_COMBINATION_COUNT; ++id) {
            const auto combo = static_cast<CanonicalComboId>(id);
            if (view.legal(combo) && view.weight(combo) > 0.0) range.hands.push_back({canonical_combos().cards(combo), view.weight(combo)});
        }
        request.opponent_ranges.push_back(std::move(range));
    }
    auto result = MultiwayResolver(config).resolve(request);
    if (actual_hand_frozen_) frozen_policy_ = result;
    return result;
}

games::multiway::MultiwayTerminalResult MultiwayFullHandSession::settle(
    const std::vector<std::array<std::uint8_t, 2>>& holes) const {
    if (board_.size() != 5U || holes.size() != state_.stacks().size()) {
        throw std::invalid_argument("full-hand settlement requires a five-card board and every hole hand");
    }
    games::multiway::MultiwayTerminalInput input;
    input.contributions = state_.contributions();
    input.folded = state_.folded();
    input.odd_chip_first_seat = 0;
    input.strengths.reserve(holes.size());
    for (const auto& hole : holes) {
        std::array<std::uint8_t, 7> cards{};
        std::copy(board_.begin(), board_.end(), cards.begin());
        cards[5] = hole[0]; cards[6] = hole[1];
        input.strengths.push_back(Strength::evaluate_7(cards));
    }
    return games::multiway::settle_multiway_terminal(input);
}

}  // namespace texas::solver::multiway
