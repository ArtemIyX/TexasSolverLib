#include "solver/multiway_action_abstraction.hpp"
#include "solver/multiway_public_builder.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <limits>
#include <numeric>
#include <stdexcept>
#include <utility>

namespace core {
namespace {

bool is_aggressive(MultiwayAction action) noexcept {
    return action == MultiwayAction::Bet || action == MultiwayAction::Raise;
}

void append_u64(std::uint64_t& hash, std::uint64_t value) noexcept {
    constexpr std::uint64_t kPrime = 1099511628211ULL;
    for (std::size_t index = 0U; index < sizeof(value); ++index) {
        hash ^= static_cast<std::uint8_t>(value & 0xffU);
        hash *= kPrime;
        value >>= 8U;
    }
}

double action_scale(const MultiwayState& state, const MultiwayActionDescriptor& action) noexcept {
    const auto actor = static_cast<std::size_t>(state.current_player());
    const auto contribution = state.street_contributions()[actor];
    const auto call = std::max(0, state.current_bet() - contribution);
    const auto pot = std::accumulate(state.contributions().begin(), state.contributions().end(), 0);
    const auto reference = std::max(1, pot + call);
    const auto increment = action.action == MultiwayAction::Bet
        ? action.target_street_contribution - contribution
        : action.target_street_contribution - state.current_bet();
    return static_cast<double>(std::max(0, increment)) / static_cast<double>(reference);
}

double pseudo_harmonic_distance(double left, double right) noexcept {
    const auto sum = left + right;
    return sum <= 0.0 ? (left == right ? 0.0 : 2.0) : 2.0 * std::fabs(left - right) / sum;
}

int saturated_multiply_divide(int value, std::uint16_t basis_points) noexcept {
    const auto product = static_cast<std::int64_t>(value) * basis_points;
    const auto result = product / 10000;
    return result > std::numeric_limits<int>::max()
        ? std::numeric_limits<int>::max()
        : static_cast<int>(result);
}

int saturated_add(int left, int right) noexcept {
    const auto result = static_cast<std::int64_t>(left) + right;
    return result > std::numeric_limits<int>::max()
        ? std::numeric_limits<int>::max()
        : static_cast<int>(result);
}

MultiwayPreflopSituation inferred_preflop_situation(const MultiwayState& state) noexcept {
    const auto big_blind = state.snapshot().big_blind;
    if (state.current_bet() <= big_blind) {
        return MultiwayPreflopSituation::Unopened;
    }
    // A snapshot intentionally has no action history. Callers that need to
    // distinguish a squeeze from a three-bet must provide the context.
    return static_cast<std::int64_t>(state.current_bet()) <= static_cast<std::int64_t>(big_blind) * 5
        ? MultiwayPreflopSituation::FacingSingleOpen
        : MultiwayPreflopSituation::FacingThreeBetOrMore;
}

void normalize_menu(
    const MultiwayState& state,
    std::vector<MultiwayActionDescriptor>& menu,
    const MultiwayActionDescriptor* protected_action) {
    const auto actor = static_cast<std::size_t>(state.current_player());
    std::vector<MultiwayActionDescriptor> unique;
    unique.reserve(menu.size());
    for (const auto& candidate : menu) {
        const auto duplicate = std::find_if(unique.begin(), unique.end(),
            [&candidate](const MultiwayActionDescriptor& existing) {
                return existing.action == candidate.action &&
                    existing.target_street_contribution == candidate.target_street_contribution;
            });
        if (duplicate == unique.end()) {
            const auto successor = state.apply(candidate.action, candidate.target_street_contribution);
            unique.push_back({candidate.action, 0U,
                successor.street_contributions()[actor], 0U});
        }
    }

    const auto protected_match = [protected_action](const MultiwayActionDescriptor& action) {
        return protected_action != nullptr && action.action == protected_action->action &&
            action.target_street_contribution == protected_action->target_street_contribution;
    };
    while (unique.size() > MULTIWAY_MAX_ABSTRACTED_ACTIONS) {
        auto remove = unique.end();
        for (auto candidate = unique.begin(); candidate != unique.end(); ++candidate) {
            if (!is_aggressive(candidate->action) || protected_match(*candidate)) continue;
            const auto candidate_distance = protected_action == nullptr ? candidate->target_street_contribution :
                std::abs(candidate->target_street_contribution - protected_action->target_street_contribution);
            const auto remove_distance = remove == unique.end() ? -1 : protected_action == nullptr
                ? remove->target_street_contribution
                : std::abs(remove->target_street_contribution - protected_action->target_street_contribution);
            if (remove == unique.end() || candidate_distance > remove_distance ||
                (candidate_distance == remove_distance &&
                    candidate->target_street_contribution > remove->target_street_contribution)) {
                remove = candidate;
            }
        }
        if (remove == unique.end()) {
            throw std::length_error("multiway action abstraction cannot compact required legal actions");
        }
        unique.erase(remove);
    }
    menu = MultiwayPublicBuilder::canonicalize_action_menu(state.snapshot(), std::move(unique));
}

}  // namespace

void MultiwayActionAbstractionConfig::validate() const {
    if (menu_profile_version == 0U || translation_policy_version == 0U ||
        translation_max_pseudo_harmonic_distance_basis_points > 20000U) {
        throw std::invalid_argument("multiway action abstraction has an invalid version or translation threshold");
    }
    if (multiway_first_bet_count == 0U || multiway_first_bet_count > first_bet_basis_points.size() ||
        three_way_first_bet_count == 0U || three_way_first_bet_count > first_bet_basis_points.size() ||
        heads_up_first_bet_count == 0U || heads_up_first_bet_count > first_bet_basis_points.size()) {
        throw std::invalid_argument("multiway action abstraction has invalid first-bet counts");
    }
    const auto require_nonzero = [](const auto& sizes, const char* message) {
        for (const auto size : sizes) {
            if (size == 0U) throw std::invalid_argument(message);
        }
    };
    require_nonzero(first_bet_basis_points, "multiway action abstraction has a zero bet size");
    require_nonzero(raise_basis_points, "multiway action abstraction has a zero raise size");
    require_nonzero(unopened_raise_to_big_blind_basis_points,
        "multiway action abstraction has a zero unopened raise size");
    require_nonzero(contextual_multiway_first_bet_basis_points,
        "multiway action abstraction has a zero contextual multiway bet size");
    require_nonzero(contextual_three_way_first_bet_basis_points,
        "multiway action abstraction has a zero contextual three-way bet size");
    require_nonzero(contextual_heads_up_first_bet_basis_points,
        "multiway action abstraction has a zero contextual heads-up bet size");
    require_nonzero(contextual_raise_basis_points,
        "multiway action abstraction has a zero contextual raise size");
    if (single_open_in_position_basis_points == 0U || single_open_out_of_position_basis_points == 0U ||
        open_caller_increment_big_blind_basis_points == 0U || three_bet_or_more_basis_points == 0U) {
        throw std::invalid_argument("multiway action abstraction has a zero preflop template size");
    }
}

void MultiwayDeviationExpansionConfig::validate() const {
    if (policy_version == 0U || minimum_pseudo_harmonic_distance_basis_points > 20000U ||
        minimum_active_seats < 2U || minimum_active_seats > 6U ||
        enabled_postflop_street_mask == 0U || maximum_menu_actions == 0U ||
        maximum_menu_actions > MULTIWAY_MAX_ABSTRACTED_ACTIONS) {
        throw std::invalid_argument("multiway deviation expansion configuration is invalid");
    }
}

std::uint64_t MultiwayActionAbstraction::menu_profile_identity(
    MultiwayActionAbstractionContext context) const noexcept {
    constexpr std::uint64_t kOffset = 14695981039346656037ULL;
    auto hash = kOffset;
    append_u64(hash, config_.menu_profile_version);
    append_u64(hash, config_.multiway_first_bet_count);
    append_u64(hash, config_.three_way_first_bet_count);
    append_u64(hash, config_.heads_up_first_bet_count);
    append_u64(hash, static_cast<std::uint8_t>(context.preflop_situation));
    append_u64(hash, static_cast<std::uint8_t>(context.relative_position));
    append_u64(hash, static_cast<std::uint8_t>(context.postflop_sizing));
    for (const auto value : config_.first_bet_basis_points) append_u64(hash, value);
    for (const auto value : config_.raise_basis_points) append_u64(hash, value);
    for (const auto value : config_.unopened_raise_to_big_blind_basis_points) append_u64(hash, value);
    append_u64(hash, config_.single_open_in_position_basis_points);
    append_u64(hash, config_.single_open_out_of_position_basis_points);
    append_u64(hash, config_.open_caller_increment_big_blind_basis_points);
    append_u64(hash, config_.three_bet_or_more_basis_points);
    for (const auto value : config_.contextual_multiway_first_bet_basis_points) append_u64(hash, value);
    for (const auto value : config_.contextual_three_way_first_bet_basis_points) append_u64(hash, value);
    for (const auto value : config_.contextual_heads_up_first_bet_basis_points) append_u64(hash, value);
    for (const auto value : config_.contextual_raise_basis_points) append_u64(hash, value);
    return hash;
}

MultiwayActionAbstraction::MultiwayActionAbstraction(MultiwayActionAbstractionConfig config)
    : config_(config) {
    config_.validate();
}

std::vector<MultiwayActionDescriptor> MultiwayActionAbstraction::make_legal_actions(
    const MultiwayBettingSnapshot& betting,
    std::uint64_t action_menu_id) const {
    return make_legal_actions(betting, action_menu_id, {});
}

MultiwayActionTranslation MultiwayActionAbstraction::translate_observed_action(
    const MultiwayBettingSnapshot& betting,
    const std::vector<MultiwayActionDescriptor>& menu,
    MultiwayAction observed_action,
    int target_street_contribution,
    MultiwayActionAbstractionContext context) const {
    const auto state = MultiwayState::from_snapshot(betting);
    const auto legal = state.legal_actions();
    if (std::find(legal.begin(), legal.end(), observed_action) == legal.end()) {
        throw std::invalid_argument("multiway observed action is not legal in this public state");
    }
    const auto actor = static_cast<std::size_t>(state.current_player());
    const auto successor = state.apply(observed_action, target_street_contribution);
    MultiwayActionTranslation result;
    result.observed_action = {observed_action, 0U, successor.street_contributions()[actor], 0U};
    result.menu_profile_identity = menu_profile_identity(context);
    result.policy_version = config_.translation_policy_version;
    result.policy_identity = result.menu_profile_identity;
    append_u64(result.policy_identity, config_.translation_policy_version);
    append_u64(result.policy_identity, config_.translation_max_pseudo_harmonic_distance_basis_points);

    for (const auto& candidate : menu) {
        const auto candidate_successor = state.apply(candidate.action, candidate.target_street_contribution);
        if (candidate_successor.street_contributions()[actor] != candidate.target_street_contribution) {
            throw std::invalid_argument("multiway translation menu has a non-exact action target");
        }
        if (candidate.action == result.observed_action.action &&
            candidate.target_street_contribution == result.observed_action.target_street_contribution) {
            result.translated_action = candidate;
            result.status = MultiwayActionTranslationStatus::ExactMenuAction;
            return result;
        }
    }
    if (!is_aggressive(observed_action)) return result;

    result.observed_scale = action_scale(state, result.observed_action);
    double best_distance = std::numeric_limits<double>::infinity();
    for (const auto& candidate : menu) {
        if (candidate.action != observed_action || !is_aggressive(candidate.action)) continue;
        const auto scale = action_scale(state, candidate);
        const auto distance = pseudo_harmonic_distance(result.observed_scale, scale);
        if (distance < best_distance ||
            (distance == best_distance && candidate.target_street_contribution < result.translated_action.target_street_contribution)) {
            best_distance = distance;
            result.translated_action = candidate;
            result.translated_scale = scale;
        }
    }
    const auto threshold = static_cast<double>(config_.translation_max_pseudo_harmonic_distance_basis_points) / 10000.0;
    if (best_distance <= threshold) result.status = MultiwayActionTranslationStatus::Translated;
    return result;
}

MultiwayDeviationDisposition MultiwayActionAbstraction::classify_observed_action(
    const MultiwayBettingSnapshot& betting,
    const std::vector<MultiwayActionDescriptor>& menu,
    MultiwayAction observed_action,
    int target_street_contribution,
    const MultiwayDeviationExpansionConfig& expansion,
    MultiwayActionAbstractionContext context) const {
    expansion.validate();
    const auto translated = translate_observed_action(
        betting, menu, observed_action, target_street_contribution, context);
    if (translated.status == MultiwayActionTranslationStatus::ExactMenuAction ||
        !is_aggressive(observed_action) || menu.size() >= expansion.maximum_menu_actions) {
        return MultiwayDeviationDisposition::Translate;
    }
    const auto state = MultiwayState::from_snapshot(betting);
    const auto active = static_cast<std::uint8_t>(std::count(
        state.folded().begin(), state.folded().end(), false));
    const auto street_bit = state.street() < Street::Flop || state.street() > Street::River
        ? 0U : static_cast<std::uint8_t>(1U << (static_cast<std::uint8_t>(state.street()) -
                                                static_cast<std::uint8_t>(Street::Flop)));
    if (active < expansion.minimum_active_seats || (expansion.enabled_postflop_street_mask & street_bit) == 0U) {
        return MultiwayDeviationDisposition::Translate;
    }
    if (translated.status == MultiwayActionTranslationStatus::DeviationTooLarge) {
        return MultiwayDeviationDisposition::Expand;
    }
    const auto distance = pseudo_harmonic_distance(translated.observed_scale, translated.translated_scale);
    return distance >= static_cast<double>(expansion.minimum_pseudo_harmonic_distance_basis_points) / 10000.0
        ? MultiwayDeviationDisposition::Expand : MultiwayDeviationDisposition::Translate;
}

std::vector<MultiwayActionDescriptor> MultiwayActionAbstraction::make_legal_actions(
    const MultiwayBettingSnapshot& betting,
    std::uint64_t action_menu_id,
    MultiwayActionAbstractionContext context) const {
    (void)action_menu_id;
    const auto state = MultiwayState::from_snapshot(betting);
    const auto base_actions = state.legal_actions();
    const auto actor = static_cast<std::size_t>(state.current_player());
    const auto pot = std::accumulate(state.contributions().begin(), state.contributions().end(), 0);
    const auto actor_contribution = state.street_contributions()[actor];
    const auto all_in_target = actor_contribution + state.stacks()[actor];
    const auto live_count = static_cast<std::size_t>(std::count(state.folded().begin(), state.folded().end(), false));

    std::vector<MultiwayActionDescriptor> result;
    result.reserve(MULTIWAY_MAX_ABSTRACTED_ACTIONS);
    const auto append = [&](MultiwayAction action, int target) {
        const auto successor = state.apply(action, target);
        const auto actual_target = successor.street_contributions()[actor];
        const auto duplicate = std::find_if(result.begin(), result.end(),
            [action, actual_target](const MultiwayActionDescriptor& existing) {
                return existing.action == action && existing.target_street_contribution == actual_target;
            });
        if (duplicate == result.end()) {
            result.push_back({action, 0U, actual_target, 0U});
        }
    };
    const auto append_aggressive = [&](MultiwayAction action, int target) {
        const auto clipped = std::min(all_in_target, target);
        if (clipped > state.current_bet() && clipped < all_in_target) append(action, clipped);
    };

    const auto has_action = [&base_actions](MultiwayAction action) {
        return std::find(base_actions.begin(), base_actions.end(), action) != base_actions.end();
    };
    for (const auto action : base_actions) {
        if (action != MultiwayAction::AllIn && !is_aggressive(action)) append(action, 0);
    }
    if (state.street() == Street::Preflop) {
        const auto situation = context.preflop_situation == MultiwayPreflopSituation::Auto
            ? inferred_preflop_situation(state)
            : context.preflop_situation;
        const auto aggressive = has_action(MultiwayAction::Bet) ? MultiwayAction::Bet : MultiwayAction::Raise;
        if (has_action(aggressive)) {
            switch (situation) {
                case MultiwayPreflopSituation::Auto:
                    break;
                case MultiwayPreflopSituation::Unopened:
                    for (const auto size : config_.unopened_raise_to_big_blind_basis_points) {
                        append_aggressive(aggressive, saturated_multiply_divide(betting.big_blind, size));
                    }
                    break;
                case MultiwayPreflopSituation::FacingSingleOpen: {
                    const auto size = context.relative_position == MultiwayRelativePosition::OutOfPosition
                        ? config_.single_open_out_of_position_basis_points
                        : config_.single_open_in_position_basis_points;
                    append_aggressive(aggressive, saturated_multiply_divide(state.current_bet(), size));
                    break;
                }
                case MultiwayPreflopSituation::FacingOpenAndCallers: {
                    const auto caller_count = std::max(1, static_cast<int>(
                        std::count(state.street_contributions().begin(), state.street_contributions().end(),
                            state.current_bet())) - 1);
                    const auto increment = static_cast<std::int64_t>(betting.big_blind) *
                        config_.open_caller_increment_big_blind_basis_points * caller_count / 10000;
                    const auto squeeze = std::max(saturated_add(state.current_bet(), state.last_full_raise_size()),
                        saturated_add(state.current_bet(), static_cast<int>(std::min<std::int64_t>(
                            increment, std::numeric_limits<int>::max() - state.current_bet()))));
                    append_aggressive(aggressive, squeeze);
                    const auto call = state.current_bet() - actor_contribution;
                    append_aggressive(aggressive, saturated_add(saturated_add(state.current_bet(), pot), call));
                    break;
                }
                case MultiwayPreflopSituation::FacingThreeBetOrMore:
                    append_aggressive(aggressive,
                        saturated_multiply_divide(state.current_bet(), config_.three_bet_or_more_basis_points));
                    break;
            }
        }
    } else {
        const auto first_bet_count = live_count <= 2U ? config_.heads_up_first_bet_count :
            live_count == 3U ? config_.three_way_first_bet_count : config_.multiway_first_bet_count;
        if (context.postflop_sizing == MultiwayPostflopSizingMode::Compatibility) {
            if (has_action(MultiwayAction::Bet)) {
                for (std::uint8_t index = 0; index < first_bet_count; ++index) {
                    const auto wager = std::max(state.last_full_raise_size(),
                        saturated_multiply_divide(pot, config_.first_bet_basis_points[index]));
                    append_aggressive(MultiwayAction::Bet, saturated_add(actor_contribution, wager));
                }
            } else if (has_action(MultiwayAction::Raise)) {
                const auto call = state.current_bet() - actor_contribution;
                for (const auto fraction : config_.raise_basis_points) {
                    const auto raise = std::max(state.last_full_raise_size(),
                        saturated_multiply_divide(pot + call, fraction));
                    append_aggressive(MultiwayAction::Raise, saturated_add(state.current_bet(), raise));
                }
            }
        } else {
            const auto effective_stack = [&state, actor]() {
                auto effective = state.stacks()[actor];
                for (std::size_t seat = 0; seat < state.stacks().size(); ++seat) {
                    if (seat != actor && !state.folded()[seat] && !state.all_in()[seat]) {
                        effective = std::min(effective, state.stacks()[seat]);
                    }
                }
                return effective;
            }();
            const auto low_spr = static_cast<std::int64_t>(effective_stack) * 2 <
                static_cast<std::int64_t>(pot) * 3;
            const auto medium_spr = !low_spr && effective_stack <= pot * 4;
            if (has_action(MultiwayAction::Bet)) {
                const auto append_first_bet_sizes = [&](const auto& sizes, std::size_t count) {
                    for (std::size_t index = 0; index < count; ++index) {
                        const auto wager = std::max(state.last_full_raise_size(),
                            saturated_multiply_divide(pot, sizes[index]));
                        append_aggressive(MultiwayAction::Bet, saturated_add(actor_contribution, wager));
                    }
                };
                if (low_spr) {
                    append_first_bet_sizes(config_.contextual_multiway_first_bet_basis_points, 1U);
                } else if (medium_spr) {
                    append_first_bet_sizes(config_.contextual_multiway_first_bet_basis_points, 2U);
                } else if (live_count <= 2U) {
                    append_first_bet_sizes(config_.contextual_heads_up_first_bet_basis_points,
                        config_.contextual_heads_up_first_bet_basis_points.size());
                } else if (live_count == 3U) {
                    append_first_bet_sizes(config_.contextual_three_way_first_bet_basis_points,
                        config_.contextual_three_way_first_bet_basis_points.size());
                } else {
                    append_first_bet_sizes(config_.contextual_multiway_first_bet_basis_points,
                        config_.contextual_multiway_first_bet_basis_points.size());
                }
            } else if (has_action(MultiwayAction::Raise)) {
                const auto call = state.current_bet() - actor_contribution;
                append_aggressive(MultiwayAction::Raise,
                    saturated_add(state.current_bet(), state.last_full_raise_size()));
                if (!low_spr) {
                    const auto count = medium_spr ? 1U : config_.contextual_raise_basis_points.size();
                    for (std::size_t index = 0; index < count; ++index) {
                        const auto raise = std::max(state.last_full_raise_size(),
                            saturated_multiply_divide(pot + call, config_.contextual_raise_basis_points[index]));
                        append_aggressive(MultiwayAction::Raise, saturated_add(state.current_bet(), raise));
                    }
                }
            }
        }
    }

    if (has_action(MultiwayAction::AllIn)) append(MultiwayAction::AllIn, 0);
    normalize_menu(state, result, nullptr);
    return result;
}

std::vector<MultiwayActionDescriptor> MultiwayActionAbstraction::insert_exact_observed_action(
    const MultiwayBettingSnapshot& betting,
    std::vector<MultiwayActionDescriptor> menu,
    MultiwayAction observed_action,
    int target_street_contribution,
    std::uint64_t action_menu_id) {
    (void)action_menu_id;
    const auto state = MultiwayState::from_snapshot(betting);
    const auto legal = state.legal_actions();
    if (std::find(legal.begin(), legal.end(), observed_action) == legal.end()) {
        throw std::invalid_argument("multiway observed action is not legal in this public state");
    }
    const auto successor = state.apply(observed_action, target_street_contribution);
    const auto actor = static_cast<std::size_t>(state.current_player());
    const MultiwayActionDescriptor observed = {
        observed_action, 0U, successor.street_contributions()[actor], 0U};
    menu.push_back(observed);
    normalize_menu(state, menu, &observed);
    return menu;
}

}  // namespace core
