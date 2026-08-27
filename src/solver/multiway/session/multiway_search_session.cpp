#include "solver/multiway/session/multiway_search_session.hpp"
#include "solver/multiway/abstraction/multiway_public_builder.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cmath>
#include <stdexcept>
#include <utility>
#include <vector>

namespace texas::solver::multiway {

MultiwaySearchSession::MultiwaySearchSession(
    const MultiwaySolveRequest& request,
    MultiwaySearchSessionDependencies dependencies,
    std::uint64_t public_root_revision)
    : coordinator_(request),
      dependencies_(dependencies),
      buckets_(dependencies.buckets),
      action_menu_(coordinator_.root().public_state.legal_actions),
      root_metadata_({
          coordinator_.root().public_state.id,
          coordinator_.root().action_abstraction_identity(),
          public_root_revision,
      }) {
    validate_dependencies();
    initialize_beliefs();
}

void MultiwaySearchSession::validate_dependencies() const {
    const auto& root = coordinator_.root();
    if (root_metadata_.revision == 0U || root_metadata_.public_state != root.public_state.id || action_menu_.empty() ||
        root_metadata_.action_abstraction.menu_id == 0U ||
        root_metadata_.action_abstraction.version == 0U) {
        throw std::invalid_argument("multiway search session has invalid root or action dependencies");
    }
    if (dependencies_.lossless_current_round_keys && root.public_state.id.value !=
        MultiwayPublicBuilder::stable_lossless_current_round_key(
            root.public_state.betting, root.public_state.board, root.public_state.history,
            root.public_state.legal_actions)) {
        throw std::invalid_argument("multiway search session requires a lossless current-round public key");
    }

    if (root.public_state.betting.street == Street::Preflop) return;
    if (buckets_ == nullptr) {
        throw std::invalid_argument("multiway postflop search session requires a bucket registry");
    }
    const auto& table = buckets_->table(
        root.public_state.betting.street, root.public_state.board);
    if ((!root.root_uses_exact_private_hand && root.root_bucket >= table.bucket_count()) ||
        (root.root_uses_exact_private_hand && root.root_bucket >= MULTIWAY_HOLE_COMBINATION_COUNT)) {
        throw std::invalid_argument("multiway search session root bucket is unavailable");
    }
}

MultiwayRangeBeliefView MultiwaySearchSession::belief(PlayerId seat) const {
    if (seat < 0 || static_cast<std::size_t>(seat) >= beliefs_.seat_count()) {
        throw std::out_of_range("multiway search session belief seat is unavailable");
    }
    return beliefs_.view(static_cast<std::size_t>(seat));
}

MultiwayRangeBeliefUpdateResult MultiwaySearchSession::apply_observation(
    PlayerId seat,
    const MultiwayRangeBeliefObservation& observation) {
    if (seat < 0 || static_cast<std::size_t>(seat) >= beliefs_.seat_count()) {
        throw std::out_of_range("multiway search session belief seat is unavailable");
    }
    return beliefs_.apply_observation(static_cast<std::size_t>(seat), observation);
}

void MultiwaySearchSession::record_action_translation(MultiwayActionTranslation translation) {
    if (translation.menu_profile_identity == 0U || translation.policy_version == 0U ||
        translation.policy_identity == 0U ||
        translation.status == MultiwayActionTranslationStatus::DeviationTooLarge ||
        translation.observed_action.action_menu_id != 0U ||
        translation.translated_action.action_menu_id == 0U) {
        throw std::invalid_argument("multiway search session has an invalid action translation");
    }
    if (std::find(action_menu_.begin(), action_menu_.end(), translation.translated_action) == action_menu_.end()) {
        throw std::invalid_argument("multiway search session translation is not bound to its action menu");
    }
    const auto state = MultiwayState::from_snapshot(coordinator_.root().public_state.betting);
    const auto successor = state.apply(
        translation.observed_action.action, translation.observed_action.target_street_contribution);
    const auto actor = static_cast<std::size_t>(state.current_player());
    if (successor.street_contributions()[actor] != translation.observed_action.target_street_contribution) {
        throw std::invalid_argument("multiway search session translation has a non-exact observed target");
    }
    action_translation_ = std::move(translation);
}

const MultiwayActionTranslation* MultiwaySearchSession::action_translation() const noexcept {
    return action_translation_ ? &*action_translation_ : nullptr;
}

MultiwayLocalExpansion MultiwaySearchSession::plan_local_expansion(
    const MultiwayActionAbstraction& abstraction,
    MultiwayAction observed_action,
    int target_street_contribution,
    const MultiwayDeviationExpansionConfig& expansion,
    MultiwayActionAbstractionContext context) const {
    const auto& current = coordinator_.root();
    if (abstraction.classify_observed_action(
            current.public_state.betting, action_menu_, observed_action,
            target_street_contribution, expansion, context) != MultiwayDeviationDisposition::Expand) {
        throw std::invalid_argument("multiway local expansion was not admitted by policy");
    }
    const auto menu = MultiwayActionAbstraction::insert_exact_observed_action(
        current.public_state.betting, action_menu_, observed_action, target_street_contribution);
    MultiwayLocalExpansion result;
    result.root = current;
    result.root.public_state.legal_actions = menu;
    result.root.public_state.canonical_history_id =
        MultiwayPublicBuilder::stable_history_id(result.root.public_state.history);
    result.root.public_state.id = {MultiwayPublicBuilder::stable_lossless_current_round_key(
        result.root.public_state.betting, result.root.public_state.board,
        result.root.public_state.history, result.root.public_state.legal_actions)};
    result.root.root_infoset.public_state = result.root.public_state.id;
    const auto state = MultiwayState::from_snapshot(current.public_state.betting);
    const auto successor = state.apply(observed_action, target_street_contribution);
    result.observed_action = {observed_action, 0U,
        successor.street_contributions()[static_cast<std::size_t>(state.current_player())], 0U};
    return result;
}

MultiwaySearchSessionRowView MultiwaySearchSession::row_view() const noexcept {
    const auto& storage = coordinator_.storage();
    return {
        storage.row_count(),
        storage.value_count(),
        storage.has_row(coordinator_.root().root_infoset),
    };
}

bool MultiwaySearchSession::capture_clean_snapshot(
    bool clean,
    std::uint64_t batch_index,
    std::uint64_t first_trajectory_id,
    std::uint64_t trajectory_count,
    std::uint64_t accepted_trajectories,
    std::uint64_t merged_delta_entries,
    std::uint32_t worker_count) {
    const auto rows = row_view();
    if (!clean || batch_index == 0U || trajectory_count == 0U || accepted_trajectories == 0U ||
        merged_delta_entries == 0U || worker_count != coordinator_.limits().worker_count || !rows.has_root_row) {
        return false;
    }
    clean_snapshot_ = MultiwaySearchSessionCleanSnapshot{
        coordinator_.export_root_policy(),
        rows,
        root_metadata_.revision,
        batch_index,
        first_trajectory_id,
        trajectory_count,
        accepted_trajectories,
        merged_delta_entries,
        worker_count,
    };
    return true;
}

const MultiwaySearchSessionCleanSnapshot* MultiwaySearchSession::clean_snapshot() const noexcept {
    return clean_snapshot_ ? &*clean_snapshot_ : nullptr;
}

MultiwaySearchSessionHeroPolicy MultiwaySearchSession::export_hero_policy(
    PlayerId hero_seat,
    CanonicalComboId actual_hand,
    const MultiwayBlueprintPolicyProvider* blueprint_policy) const {
    if (hero_seat < 0 || static_cast<std::size_t>(hero_seat) >= beliefs_.seat_count() ||
        root_metadata_.public_state != coordinator_.root().public_state.id) {
        throw std::invalid_argument("multiway hero policy has an unavailable seat or root");
    }
    const auto& root = coordinator_.root();
    const MultiwayInfosetId infoset = {root.public_state.id, hero_seat};
    const auto& legal_actions = root.public_state.legal_actions;
    const auto actual_view = belief(hero_seat);
    if (!actual_view.legal(actual_hand) || actual_view.weight(actual_hand) <= 0.0) {
        throw std::invalid_argument("multiway hero actual hand is absent from the current belief");
    }

    MultiwaySearchSessionHeroPolicy result;
    result.hero_seat = hero_seat;
    result.infoset = infoset;
    result.root_revision = root_metadata_.revision;
    const auto& combos = canonical_combos();
    for (std::uint32_t value = 0U; value < MULTIWAY_HOLE_COMBINATION_COUNT; ++value) {
        const auto combo = static_cast<CanonicalComboId>(value);
        if (!actual_view.legal(combo) || actual_view.weight(combo) <= 0.0) continue;
        std::uint32_t bucket = root.root_bucket;
        if (root.root_uses_exact_private_hand) {
            bucket = static_cast<std::uint32_t>(MultiwayBucketTable::hole_index(combos.cards(combo)));
        } else if (root.public_state.betting.street != Street::Preflop &&
            !root.root_uses_exact_private_hand) {
            bucket = buckets_->table(root.public_state.betting.street, root.public_state.board)
                .lookup(combos.cards(combo));
        }
        std::vector<Probability> probabilities;
        if (coordinator_.storage().has_row(infoset)) {
            probabilities = coordinator_.storage().average_strategy(infoset, bucket);
        } else {
            probabilities.assign(legal_actions.size(), 0.0);
            const auto lookup = blueprint_policy == nullptr
                ? MultiwayBlueprintLookupStatus::Missing
                : blueprint_policy->strategy_into(
                    infoset, bucket, legal_actions.data(), legal_actions.size(), probabilities.data());
            if (lookup != MultiwayBlueprintLookupStatus::Hit) {
                const auto uniform = 1.0 / static_cast<Probability>(legal_actions.size());
                std::fill(probabilities.begin(), probabilities.end(), uniform);
            }
        }
        MultiwaySearchSessionHeroRow row;
        row.combo = combo;
        row.actions.reserve(legal_actions.size());
        for (std::size_t action = 0U; action < legal_actions.size(); ++action) {
            row.actions.push_back({legal_actions[action], probabilities[action]});
        }
        if (combo == actual_hand) result.actual_hand_actions = row.actions;
        result.rows.push_back(std::move(row));
    }
    if (actual_hand_freeze_ && actual_hand_freeze_->hero_seat == hero_seat &&
        actual_hand_freeze_->combo == actual_hand &&
        actual_hand_freeze_->root_revision == root_metadata_.revision) {
        result.actual_hand_actions = actual_hand_freeze_->actions;
        result.actual_hand_frozen = true;
    }
    return result;
}

void MultiwaySearchSession::freeze_actual_hand_policy(
    PlayerId hero_seat,
    CanonicalComboId actual_hand,
    const std::vector<MultiwayRootActionProbability>& actions) {
    if (hero_seat < 0 || static_cast<std::size_t>(hero_seat) >= beliefs_.seat_count() ||
        !belief(hero_seat).legal(actual_hand) || belief(hero_seat).weight(actual_hand) <= 0.0 ||
        actions.size() != action_menu_.size() || actions.empty()) {
        throw std::invalid_argument("multiway actual hand freeze has an incompatible action menu");
    }
    Probability total = 0.0;
    for (std::size_t index = 0U; index < actions.size(); ++index) {
        if (actions[index].action != action_menu_[index] || !std::isfinite(actions[index].probability) ||
            actions[index].probability < 0.0) {
            throw std::invalid_argument("multiway actual hand freeze has invalid action data");
        }
        total += actions[index].probability;
    }
    if (std::fabs(total - 1.0) > 1e-12) {
        throw std::invalid_argument("multiway actual hand freeze policy is not normalized");
    }
    actual_hand_freeze_ = ActualHandFreeze{hero_seat, actual_hand, root_metadata_.revision, actions};
}

void MultiwaySearchSession::clear_actual_hand_freeze() noexcept {
    actual_hand_freeze_.reset();
}

MultiwayRootSnapshot MultiwaySearchSession::make_next_round_root(MultiwayRootSnapshot next_root) const {
    if (next_root.public_state.board.size() <= coordinator_.root().public_state.board.size()) {
        throw std::invalid_argument("multiway next round root must advance the public board");
    }
    return make_reroot_root(std::move(next_root));
}

MultiwayRootSnapshot MultiwaySearchSession::make_reroot_root(MultiwayRootSnapshot next_root) const {
    const auto seat_count = beliefs_.seat_count();
    if (next_root.seat_order.size() != seat_count ||
        next_root.private_ranges.ranges.size() != seat_count ||
        next_root.public_state.board.size() < coordinator_.root().public_state.board.size()) {
        throw std::invalid_argument("multiway next round root is incompatible with the current session");
    }
    for (const auto card : coordinator_.root().public_state.board) {
        if (std::find(next_root.public_state.board.begin(), next_root.public_state.board.end(), card) ==
            next_root.public_state.board.end()) {
            throw std::invalid_argument("multiway reroot removes known public cards");
        }
    }
    next_root.private_ranges.board = next_root.public_state.board;
    for (std::size_t seat = 0U; seat < seat_count; ++seat) {
        auto& range = next_root.private_ranges.ranges[seat];
        range.clear();
        const auto prior = belief(static_cast<PlayerId>(seat));
        range.reserve(MULTIWAY_HOLE_COMBINATION_COUNT);
        for (std::uint32_t value = 0U; value < MULTIWAY_HOLE_COMBINATION_COUNT; ++value) {
            const auto combo = static_cast<CanonicalComboId>(value);
            const auto weight = prior.weight(combo);
            const auto cards = canonical_combos().cards(combo);
            if (weight <= 0.0 ||
                std::find(next_root.public_state.board.begin(), next_root.public_state.board.end(), cards[0]) !=
                    next_root.public_state.board.end() ||
                std::find(next_root.public_state.board.begin(), next_root.public_state.board.end(), cards[1]) !=
                    next_root.public_state.board.end()) {
                continue;
            }
            range.push_back({cards, weight});
        }
        if (range.empty()) {
            throw std::invalid_argument("multiway next round root has no legal posterior range");
        }
    }
    next_root.validate();
    return next_root;
}

void MultiwaySearchSession::initialize_beliefs() {
    const auto& root = coordinator_.root();
    const auto seat_count = root.seat_order.size();
    std::array<std::vector<MultiwayRangeBeliefSuppliedEntry>, MULTIWAY_RANGE_BELIEF_MAX_SEATS> entries;
    std::array<MultiwayRangeBeliefSeatInput, MULTIWAY_RANGE_BELIEF_MAX_SEATS> inputs = {};
    for (std::size_t seat = 0U; seat < seat_count; ++seat) {
        const auto& source = root.private_ranges.ranges[seat];
        auto& destination = entries[seat];
        destination.reserve(source.size());
        for (const auto& weighted_hole : source) {
            destination.push_back({weighted_hole.hole, weighted_hole.weight});
        }
        inputs[seat] = {
            destination.data(),
            destination.size(),
            root.public_state.board.data(),
            root.public_state.board.size(),
        };
    }
    beliefs_.reset_supplied(seat_count, inputs.data());
}

}  // namespace texas::solver::multiway
