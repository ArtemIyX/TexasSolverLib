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

MultiwayFullHandSession::MultiwayFullHandSession(
    games::multiway::MultiwayGameConfig config,
    std::vector<std::uint8_t> board,
    std::uint64_t hand_seed)
    : history_{1U, hand_seed, config, {}},
      state_(games::multiway::MultiwayState::initial(config)),
      board_(std::move(board)) {
    config.validate();
    std::array<MultiwayRangeBeliefSeatInput, MULTIWAY_RANGE_BELIEF_MAX_SEATS> inputs{};
    beliefs_.reset_uniform(config.starting_stacks.size(), inputs.data());
}

const games::multiway::MultiwayState& MultiwayFullHandSession::observe(
    const games::multiway::MultiwayReplayEvent& event,
    const MultiwayRangeBeliefObservation* observation) {
    const auto next_state = games::multiway::apply_multiway_replay_event(state_, event);
    if (event.kind == games::multiway::MultiwayReplayEventKind::Decision && observation != nullptr) {
        if (beliefs_.apply_observation(
                static_cast<std::size_t>(event.decision.acting_seat), *observation) ==
            MultiwayRangeBeliefUpdateResult::NoPosteriorMass) {
            throw std::invalid_argument("full-hand observation has no posterior mass");
        }
    }
    state_ = next_state;
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
    if (hero_seat < 0 || static_cast<std::size_t>(hero_seat) >= beliefs_.seat_count()) {
        throw std::invalid_argument("full-hand hero seat is invalid");
    }
    if (frozen_policy_.has_value()) return *frozen_policy_;
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

MultiwayActionTranslation MultiwayFullHandSession::translate_preflop_action(
    const MultiwayActionDescriptor& observed,
    const std::vector<MultiwayActionDescriptor>& menu,
    const MultiwayActionAbstractionConfig& abstraction) const {
    if (state_.street() != core::Street::Preflop) {
        throw std::invalid_argument("preflop translation requested outside preflop");
    }
    return MultiwayActionAbstraction(abstraction).translate_observed_action(
        state_.snapshot(), menu, observed.action, observed.target_street_contribution);
}

games::multiway::MultiwayTerminalResult MultiwayFullHandSession::settle(
    const std::vector<std::array<std::uint8_t, 2>>& holes) const {
    if (board_.size() != 5U || holes.size() != state_.stacks().size()) {
        throw std::invalid_argument("full-hand settlement requires a five-card board and every hole hand");
    }
    if (!std::is_sorted(board_.begin(), board_.end()) ||
        std::adjacent_find(board_.begin(), board_.end()) != board_.end()) {
        throw std::invalid_argument("full-hand settlement requires a canonical board");
    }
    std::array<bool, 52U> used{};
    for (const auto card : board_) {
        if (card >= 52U || used[card]) throw std::invalid_argument("full-hand settlement has duplicate board cards");
        used[card] = true;
    }
    games::multiway::MultiwayTerminalInput input;
    input.contributions = state_.contributions();
    input.folded = state_.folded();
    input.odd_chip_first_seat = 0;
    input.rake_policy = history_.initial_config.rake_policy;
    input.strengths.reserve(holes.size());
    for (const auto& hole : holes) {
        if (hole[0] >= 52U || hole[1] >= 52U || hole[0] == hole[1] || used[hole[0]] || used[hole[1]]) {
            throw std::invalid_argument("full-hand settlement has invalid or blocked hole cards");
        }
        used[hole[0]] = true;
        used[hole[1]] = true;
        std::array<std::uint8_t, 7> cards{};
        std::copy(board_.begin(), board_.end(), cards.begin());
        cards[5] = hole[0]; cards[6] = hole[1];
        input.strengths.push_back(Strength::evaluate_7(cards));
    }
    return games::multiway::settle_multiway_terminal(input);
}

}  // namespace texas::solver::multiway
