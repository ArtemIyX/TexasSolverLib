#include "solver/multiway_rollout_leaf.hpp"

#include "games/hunl_eval.hpp"
#include "util/pcs.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <tuple>

namespace core {
namespace {

constexpr Value kInvalid = std::numeric_limits<Value>::quiet_NaN();

bool valid_policy(MultiwayContinuationPolicyKind policy) noexcept {
    return is_valid_multiway_continuation_policy(policy);
}

MultiwayContinuationPolicyKind selected_policy(
    const MultiwayLeafEvaluationRequest& request,
    const MultiwayRolloutLeafContext& context) noexcept {
    return request.public_state.value == 0U ? context.selected_policy : request.continuation_policy;
}

std::uint64_t hash_seed_batch(const std::uint64_t* seeds, std::size_t count) noexcept {
    std::uint64_t hash = 1469598103934665603ULL;
    for (std::size_t index = 0; index < count; ++index) {
        auto value = seeds[index];
        for (std::size_t byte = 0; byte < sizeof(value); ++byte) {
            hash ^= static_cast<std::uint8_t>(value >> (byte * 8U));
            hash *= 1099511628211ULL;
        }
    }
    hash ^= count;
    hash *= 1099511628211ULL;
    return hash == 0U ? 1U : hash;
}

bool make_cache_key(
    const MultiwayLeafEvaluationRequest& request,
    const MultiwayRolloutLeafContext& context,
    MultiwayContinuationCacheKey* output) noexcept {
    if (output == nullptr || context.seeds == nullptr || context.seed_count == 0U ||
        context.seed_count > std::numeric_limits<std::uint32_t>::max()) return false;
    std::uint64_t bias_factor_bits = 0;
    static_assert(sizeof(bias_factor_bits) == sizeof(context.bias_factor));
    std::memcpy(&bias_factor_bits, &context.bias_factor, sizeof(bias_factor_bits));
    *output = {
        request.public_state,
        request.traverser,
        request.continuation_actor,
        request.future_bucket,
        request.action_abstraction_version,
        request.leaf_model_version,
        request.range_context_identity,
        request.private_context_identity,
        hash_seed_batch(context.seeds, context.seed_count),
        bias_factor_bits,
        static_cast<std::uint32_t>(context.seed_count),
        context.limits.max_betting_actions,
        context.limits.max_exact_runouts,
    };
    return output->valid();
}

MultiwayRolloutRunoutMode merge_runout_mode(
    MultiwayRolloutRunoutMode left,
    MultiwayRolloutRunoutMode right) noexcept {
    return left == right ? left : MultiwayRolloutRunoutMode::Mixed;
}

void record_invalid(MultiwayContinuationDiagnostics* diagnostics) noexcept {
    if (diagnostics != nullptr) ++diagnostics->invalid_count;
}

void record_success(
    MultiwayContinuationDiagnostics* diagnostics,
    const MultiwayRolloutProfileResult& result) noexcept {
    if (diagnostics == nullptr) return;
    const auto completed = diagnostics->leaf_count - diagnostics->invalid_count;
    diagnostics->sample_count += result.seed_count;
    diagnostics->runout_mode = completed == 1U
        ? result.runout_mode
        : merge_runout_mode(diagnostics->runout_mode, result.runout_mode);
    if (result.status == MultiwayRolloutStatus::CappedFallback) ++diagnostics->capped_count;
    const auto count = static_cast<Value>(completed);
    for (std::size_t policy = 0; policy < result.values.size(); ++policy) {
        diagnostics->mean_policy_values[policy] +=
            (result.values[policy] - diagnostics->mean_policy_values[policy]) / count;
    }
}

bool valid_cached_result(
    const MultiwayContinuationCacheKey& key,
    const MultiwayRolloutProfileResult& value) noexcept {
    if (value.seed_count != key.seed_count ||
        (value.status != MultiwayRolloutStatus::Complete &&
         value.status != MultiwayRolloutStatus::CappedFallback) ||
        value.runout_mode > MultiwayRolloutRunoutMode::Mixed) return false;
    for (const auto policy_value : value.values) {
        if (!std::isfinite(policy_value)) return false;
    }
    return true;
}

std::uint8_t compact_to_hunl_card(std::uint8_t card) noexcept {
    return static_cast<std::uint8_t>((card / 4U + 2U) * 4U + card % 4U);
}

bool valid_input(const MultiwayRolloutInput& input) noexcept {
    if (input.state == nullptr || input.holes == nullptr || input.board == nullptr ||
        input.board_count > MULTIWAY_ROLLOUT_MAX_BOARD_CARDS ||
        input.state->seat_count < 2U || input.state->seat_count > kMultiwayFixedMaxSeats ||
        input.next_street_first_player < 0 ||
        static_cast<std::size_t>(input.next_street_first_player) >= input.state->seat_count ||
        input.odd_chip_first_seat < 0 ||
        static_cast<std::size_t>(input.odd_chip_first_seat) >= input.state->seat_count) return false;
    const auto& state = *input.state;
    if (state.big_blind <= 0 || state.last_full_raise_size <= 0 || state.current_bet < 0 ||
        state.current_player < -1 || state.current_player >= state.seat_count ||
        state.last_aggressor < -1 || state.last_aggressor >= state.seat_count ||
        state.street > Street::River) return false;
    std::int64_t total_contributions = 0;
    bool has_live = false;
    for (std::size_t seat = 0; seat < state.seat_count; ++seat) {
        if (state.stacks[seat] < 0 || state.contributions[seat] < 0 ||
            state.street_contributions[seat] < 0 ||
            state.street_contributions[seat] > state.contributions[seat] ||
            state.all_in[seat] != (state.stacks[seat] == 0) ||
            ((state.folded[seat] || state.all_in[seat]) &&
             (state.may_raise[seat] || state.pending[seat]))) return false;
        total_contributions += state.contributions[seat];
        has_live = has_live || !state.folded[seat];
    }
    if (!has_live || total_contributions > std::numeric_limits<int>::max()) return false;
    const auto expected_board_count = state.street == Street::Preflop ? 0U :
        (state.street == Street::Flop ? 3U : (state.street == Street::Turn ? 4U : 5U));
    if (input.board_count != expected_board_count) return false;
    const auto rake_valid = input.rake_policy.mode == MultiwayRakeMode::ExplicitZero
        ? input.rake_policy.basis_points == 0U && input.rake_policy.cap == 0 &&
              input.rake_policy.no_flop_no_drop
        : input.rake_policy.mode == MultiwayRakeMode::PercentageOfContestedPot &&
              input.rake_policy.basis_points > 0U && input.rake_policy.basis_points <= 10'000U &&
              input.rake_policy.cap > 0;
    if (!rake_valid) return false;
    std::array<bool, 64U> used{};
    for (std::size_t card = 0; card < input.board_count; ++card) {
        if (input.board[card] >= MULTIWAY_ROLLOUT_DECK_CARDS || used[input.board[card]]) return false;
        used[input.board[card]] = true;
    }
    for (std::size_t seat = 0; seat < input.state->seat_count; ++seat) {
        const auto hole = (*input.holes)[seat];
        if (hole[0] >= MULTIWAY_ROLLOUT_DECK_CARDS || hole[1] >= MULTIWAY_ROLLOUT_DECK_CARDS ||
            hole[0] == hole[1] || used[hole[0]] || used[hole[1]]) return false;
        used[hole[0]] = true;
        used[hole[1]] = true;
    }
    return true;
}

bool action_is_legal(
    const MultiwayFixedState& state,
    const MultiwayActionDescriptor& descriptor) noexcept {
    const auto legal = state.legal_actions();
    if (!legal.contains(descriptor.action)) return false;
    const auto seat = static_cast<std::size_t>(state.current_player);
    if (descriptor.action == MultiwayAction::Bet || descriptor.action == MultiwayAction::Raise) {
        return descriptor.target_street_contribution >= state.current_bet + state.last_full_raise_size &&
               descriptor.target_street_contribution <
                   state.street_contributions[seat] + state.stacks[seat];
    }
    if (descriptor.action == MultiwayAction::AllIn) {
        return descriptor.target_street_contribution == state.street_contributions[seat] + state.stacks[seat];
    }
    return descriptor.target_street_contribution == state.street_contributions[seat];
}

bool valid_menu(const MultiwayFixedState& state, const MultiwayRolloutActionMenu& menu) noexcept {
    if (menu.count == 0U || menu.count > MULTIWAY_ROLLOUT_MAX_ACTIONS) return false;
    Probability total = 0.0;
    for (std::size_t index = 0; index < menu.count; ++index) {
        if (!action_is_legal(state, menu.actions[index]) || !std::isfinite(menu.blueprint[index]) ||
            menu.blueprint[index] < 0.0) return false;
        for (std::size_t earlier = 0; earlier < index; ++earlier) {
            if (menu.actions[earlier].action == menu.actions[index].action &&
                menu.actions[earlier].target_street_contribution ==
                    menu.actions[index].target_street_contribution) return false;
        }
        total += menu.blueprint[index];
    }
    return std::isfinite(total) && total > 0.0;
}

void mark_used(MultiwayRolloutScratch& scratch, std::uint8_t board_count) noexcept {
    scratch.used.fill(false);
    for (std::size_t card = 0; card < board_count; ++card) scratch.used[scratch.board[card]] = true;
    for (std::size_t seat = 0; seat < scratch.state.seat_count; ++seat) {
        scratch.used[scratch.holes[seat][0]] = true;
        scratch.used[scratch.holes[seat][1]] = true;
    }
}

Value settle(MultiwayRolloutScratch& scratch, std::uint8_t board_count, PlayerId traverser,
             PlayerId odd_chip_first_seat, const MultiwayRakePolicy& rake_policy) noexcept {
    if (board_count != MULTIWAY_ROLLOUT_MAX_BOARD_CARDS || traverser < 0 ||
        static_cast<std::size_t>(traverser) >= scratch.state.seat_count) return kInvalid;
    MultiwayFixedTerminalInput input;
    input.seat_count = scratch.state.seat_count;
    input.odd_chip_first_seat = odd_chip_first_seat;
    input.rake_policy = rake_policy;
    input.flop_seen = scratch.state.street != Street::Preflop;
    for (std::size_t seat = 0; seat < input.seat_count; ++seat) {
        input.contributions[seat] = scratch.state.contributions[seat];
        input.folded[seat] = scratch.state.folded[seat];
        std::array<std::uint8_t, 7> cards{};
        for (std::size_t card = 0; card < board_count; ++card) {
            cards[card] = compact_to_hunl_card(scratch.board[card]);
        }
        cards[5] = compact_to_hunl_card(scratch.holes[seat][0]);
        cards[6] = compact_to_hunl_card(scratch.holes[seat][1]);
        input.strengths[seat] = Strength::evaluate_7(cards);
    }
    settle_multiway_terminal_fixed(input, scratch.terminal_scratch, scratch.terminal_result);
    return scratch.terminal_result.utilities[static_cast<std::size_t>(traverser)];
}

bool next_unused_card(MultiwayRolloutScratch& scratch, std::uint64_t& random_state, std::uint8_t* card) noexcept {
    std::uint32_t remaining = 0;
    for (std::uint8_t candidate = 0; candidate < MULTIWAY_ROLLOUT_DECK_CARDS; ++candidate) {
        if (!scratch.used[candidate]) ++remaining;
    }
    if (remaining == 0U) return false;
    PcsRng rng(random_state);
    const auto selected = static_cast<std::uint32_t>(rng.gen_range(remaining));
    random_state = rng.next_u64();
    std::uint32_t seen = 0;
    for (std::uint8_t candidate = 0; candidate < MULTIWAY_ROLLOUT_DECK_CARDS; ++candidate) {
        if (scratch.used[candidate]) continue;
        if (seen++ == selected) {
            *card = candidate;
            scratch.used[candidate] = true;
            return true;
        }
    }
    return false;
}

Value exact_all_in_value(MultiwayRolloutScratch& scratch, std::uint8_t board_count, PlayerId traverser,
                         PlayerId odd_chip_first_seat, const MultiwayRakePolicy& rake_policy,
                         std::uint32_t max_runouts, std::uint32_t* runout_count) noexcept {
    const auto missing = static_cast<std::uint8_t>(MULTIWAY_ROLLOUT_MAX_BOARD_CARDS - board_count);
    if (missing > 2U) return kInvalid;
    std::array<std::uint8_t, MULTIWAY_ROLLOUT_DECK_CARDS> cards{};
    std::uint32_t count = 0;
    for (std::uint8_t card = 0; card < MULTIWAY_ROLLOUT_DECK_CARDS; ++card) {
        if (!scratch.used[card]) cards[count++] = card;
    }
    const auto runouts = missing == 0U ? 1U : (missing == 1U ? count : count * (count - 1U) / 2U);
    if (runouts == 0U || runouts > max_runouts) return kInvalid;
    Value total = 0.0;
    if (missing == 0U) total = settle(scratch, board_count, traverser, odd_chip_first_seat, rake_policy);
    for (std::uint32_t first = 0; missing == 1U && first < count; ++first) {
        scratch.board[board_count] = cards[first];
        total += settle(scratch, 5U, traverser, odd_chip_first_seat, rake_policy);
    }
    for (std::uint32_t first = 0; missing == 2U && first < count; ++first) {
        for (std::uint32_t second = first + 1U; second < count; ++second) {
            scratch.board[board_count] = cards[first];
            scratch.board[board_count + 1U] = cards[second];
            total += settle(scratch, 5U, traverser, odd_chip_first_seat, rake_policy);
        }
    }
    *runout_count += runouts;
    return std::isfinite(total) ? total / static_cast<Value>(runouts) : kInvalid;
}

Value one_rollout(MultiwayRolloutScratch& scratch, const MultiwayRolloutInput& input,
                  const MultiwayRolloutLeafContext& context, MultiwayContinuationPolicyKind policy,
                  PlayerId traverser, std::uint64_t seed, std::uint32_t* action_count, std::uint32_t* exact_runouts,
                  bool* capped, bool* used_exact_runout, bool* used_seeded_runout) noexcept {
    auto board_count = input.board_count;
    std::uint64_t random_state = seed;
    for (;;) {
        const auto kind = scratch.state.next_node_kind();
        if (kind == MultiwayNextNodeKind::FoldTerminal) {
            // A folded terminal may occur before a five-card board. Complete
            // the board deterministically only to keep fixed settlement input.
            while (board_count < 5U) {
                std::uint8_t card = 0;
                if (!next_unused_card(scratch, random_state, &card)) return kInvalid;
                scratch.board[board_count++] = card;
            }
            return settle(scratch, board_count, traverser,
                          input.odd_chip_first_seat, input.rake_policy);
        }
        if (kind == MultiwayNextNodeKind::ShowdownTerminal) {
            while (board_count < 5U) {
                std::uint8_t card = 0;
                if (!next_unused_card(scratch, random_state, &card)) return kInvalid;
                scratch.board[board_count++] = card;
                *used_seeded_runout = true;
            }
            return settle(scratch, board_count, traverser, input.odd_chip_first_seat, input.rake_policy);
        }
        if (kind == MultiwayNextNodeKind::BoardRunout) {
            const auto exact = exact_all_in_value(
                scratch, board_count, traverser, input.odd_chip_first_seat, input.rake_policy,
                context.limits.max_exact_runouts, exact_runouts);
            if (std::isfinite(exact)) {
                *used_exact_runout = true;
                return exact;
            }
            *capped = true;
            while (board_count < 5U) {
                std::uint8_t card = 0;
                if (!next_unused_card(scratch, random_state, &card)) return kInvalid;
                scratch.board[board_count++] = card;
                *used_seeded_runout = true;
            }
            return settle(scratch, board_count, traverser, input.odd_chip_first_seat, input.rake_policy);
        }
        if (kind == MultiwayNextNodeKind::StreetTransition) {
            const auto next = scratch.state.street == Street::Preflop ? Street::Flop :
                (scratch.state.street == Street::Flop ? Street::Turn : Street::River);
            const auto cards_to_deal = scratch.state.street == Street::Preflop ? 3U : 1U;
            for (std::uint8_t dealt = 0; dealt < cards_to_deal; ++dealt) {
                std::uint8_t card = 0;
                if (!next_unused_card(scratch, random_state, &card)) return kInvalid;
                scratch.board[board_count++] = card;
                *used_seeded_runout = true;
            }
            scratch.state = scratch.state.begin_next_street(next, input.next_street_first_player);
            continue;
        }
        if (*action_count >= context.limits.max_betting_actions || context.provide_actions == nullptr) {
            *capped = true;
            while (board_count < 5U) {
                std::uint8_t card = 0;
                if (!next_unused_card(scratch, random_state, &card)) return kInvalid;
                scratch.board[board_count++] = card;
                *used_seeded_runout = true;
            }
            return settle(scratch, board_count, traverser, input.odd_chip_first_seat, input.rake_policy);
        }
        MultiwayRolloutActionMenu menu;
        if (!context.provide_actions(scratch.state, scratch.board.data(), board_count, &menu,
                                     context.action_context) || !valid_menu(scratch.state, menu) ||
            !MultiwayFixedContinuationPolicy::apply(policy, menu.actions.data(), menu.blueprint.data(),
                                                     menu.count, scratch.policy.data(), context.bias_factor)) {
            return kInvalid;
        }
        PcsRng rng(random_state);
        const auto draw = rng.next_unit_f64();
        random_state = rng.next_u64();
        Probability cumulative = 0.0;
        std::size_t selected = menu.count - 1U;
        for (std::size_t action = 0; action < menu.count; ++action) {
            cumulative += scratch.policy[action];
            if (draw < cumulative) { selected = action; break; }
        }
        scratch.state = scratch.state.apply(menu.actions[selected].action,
                                            menu.actions[selected].target_street_contribution);
        ++*action_count;
    }
}

}  // namespace

bool MultiwayContinuationCacheKey::valid() const noexcept {
    return public_state.value != 0U && traverser >= 0 && actor >= 0 &&
           static_cast<std::size_t>(traverser) < kMultiwayFixedMaxSeats &&
           static_cast<std::size_t>(actor) < kMultiwayFixedMaxSeats &&
           action_abstraction_version != 0U && leaf_model_version != 0U &&
           range_context_identity != 0U && private_context_identity != 0U &&
           seed_batch_identity != 0U && seed_count != 0U &&
           max_betting_actions != 0U && max_exact_runouts != 0U;
}

bool MultiwayContinuationCacheKey::same_context(
    const MultiwayContinuationCacheKey& other) const noexcept {
    return public_state == other.public_state && traverser == other.traverser && actor == other.actor &&
           future_bucket == other.future_bucket &&
           action_abstraction_version == other.action_abstraction_version &&
           leaf_model_version == other.leaf_model_version &&
           range_context_identity == other.range_context_identity &&
           private_context_identity == other.private_context_identity &&
           bias_factor_bits == other.bias_factor_bits && seed_count == other.seed_count &&
           max_betting_actions == other.max_betting_actions &&
           max_exact_runouts == other.max_exact_runouts;
}

bool MultiwayContinuationCacheKey::operator<(
    const MultiwayContinuationCacheKey& other) const noexcept {
    return std::tie(public_state, traverser, actor, future_bucket, action_abstraction_version,
                    leaf_model_version, range_context_identity, private_context_identity,
                    seed_batch_identity, bias_factor_bits, seed_count, max_betting_actions,
                    max_exact_runouts) <
           std::tie(other.public_state, other.traverser, other.actor, other.future_bucket,
                    other.action_abstraction_version, other.leaf_model_version,
                    other.range_context_identity, other.private_context_identity,
                    other.seed_batch_identity, other.bias_factor_bits, other.seed_count,
                    other.max_betting_actions, other.max_exact_runouts);
}

MultiwayContinuationCache::MultiwayContinuationCache(
    std::size_t max_entries,
    std::uint64_t max_bytes) {
    const auto byte_capacity = max_bytes / entry_bytes();
    const auto size_capacity = byte_capacity > std::numeric_limits<std::size_t>::max()
        ? std::numeric_limits<std::size_t>::max()
        : static_cast<std::size_t>(byte_capacity);
    capacity_ = std::min(max_entries, size_capacity);
    entries_.reserve(capacity_);
}

bool MultiwayContinuationCache::find(
    const MultiwayContinuationCacheKey& key,
    MultiwayRolloutProfileResult* output) const noexcept {
    if (output == nullptr || !key.valid()) return false;
    const auto found = std::lower_bound(
        entries_.begin(), entries_.end(), key,
        [](const Entry& entry, const MultiwayContinuationCacheKey& candidate) {
            return entry.key < candidate;
        });
    if (found == entries_.end() || found->key < key || key < found->key) return false;
    *output = found->value;
    return true;
}

bool MultiwayContinuationCache::try_insert(
    const MultiwayContinuationCacheKey& key,
    const MultiwayRolloutProfileResult& value) noexcept {
    if (!key.valid() || !valid_cached_result(key, value)) return false;
    const auto found = std::lower_bound(
        entries_.begin(), entries_.end(), key,
        [](const Entry& entry, const MultiwayContinuationCacheKey& candidate) {
            return entry.key < candidate;
        });
    if (found != entries_.end() && !(found->key < key) && !(key < found->key)) return true;
    if (entries_.size() >= capacity_) return false;
    entries_.insert(found, Entry{key, value});
    return true;
}

void MultiwayContinuationCache::record_repeated_seed_variance(
    const MultiwayContinuationCacheKey& key,
    const MultiwayRolloutProfileResult& value,
    MultiwayContinuationDiagnostics* diagnostics) const noexcept {
    if (diagnostics == nullptr || !key.valid()) return;
    for (const auto& entry : entries_) {
        if (!entry.key.same_context(key) || entry.key.seed_batch_identity == key.seed_batch_identity) continue;
        ++diagnostics->repeated_seed_pairs;
        const auto pairs = static_cast<double>(diagnostics->repeated_seed_pairs);
        for (std::size_t policy = 0; policy < value.values.size(); ++policy) {
            const auto difference = value.values[policy] - entry.value.values[policy];
            const auto estimate = 0.5 * difference * difference;
            diagnostics->repeated_seed_variance[policy] +=
                (estimate - diagnostics->repeated_seed_variance[policy]) / pairs;
        }
    }
}

std::uint64_t MultiwayContinuationCache::memory_bytes() const noexcept {
    return static_cast<std::uint64_t>(entries_.capacity()) * entry_bytes();
}

bool evaluate_multiway_rollout_profiles(const MultiwayLeafEvaluationRequest& request,
                                        const MultiwayRolloutLeafContext& context,
                                        MultiwayRolloutProfileResult* output) noexcept {
    if (context.diagnostics != nullptr) {
        ++context.diagnostics->leaf_count;
        context.diagnostics->policy_mode = selected_policy(request, context);
        if (context.seeds != nullptr && context.seed_count != 0U) {
            context.diagnostics->seed = context.seeds[0];
        }
    }
    if (output == nullptr) {
        record_invalid(context.diagnostics);
        return false;
    }
    *output = {};
    if (context.provide_input == nullptr || context.provide_actions == nullptr || context.scratch == nullptr ||
        context.seeds == nullptr || context.seed_count == 0U ||
        context.limits.max_betting_actions == 0U || context.limits.max_exact_runouts == 0U ||
        !std::isfinite(context.bias_factor) || context.bias_factor <= 0.0 ||
        !valid_policy(selected_policy(request, context))) {
        record_invalid(context.diagnostics);
        return false;
    }
    MultiwayContinuationCacheKey cache_key;
    const auto cacheable = context.cache != nullptr && make_cache_key(request, context, &cache_key);
    if (cacheable) {
        if (context.cache->find(cache_key, output)) {
            if (context.diagnostics != nullptr) ++context.diagnostics->cache_hits;
            record_success(context.diagnostics, *output);
            return true;
        }
        if (context.diagnostics != nullptr) ++context.diagnostics->cache_misses;
    } else if (context.diagnostics != nullptr) {
        ++context.diagnostics->cache_bypasses;
    }
    MultiwayRolloutInput input;
    if (!context.provide_input(request, &input, context.input_context) || !valid_input(input)) {
        record_invalid(context.diagnostics);
        return false;
    }
    const auto rollout_count = context.seed_count;
    bool used_exact_runout = false;
    bool used_seeded_runout = false;
    for (std::size_t policy_index = 0; policy_index < MULTIWAY_FIXED_CONTINUATION_POLICIES.size(); ++policy_index) {
        Value total = 0.0;
        std::uint32_t actions = 0;
        std::uint32_t exact_runouts = 0;
        bool capped = false;
        for (std::size_t seed = 0; seed < rollout_count; ++seed) {
            auto& scratch = *context.scratch;
            scratch.state = *input.state;
            scratch.holes = *input.holes;
            for (std::size_t card = 0; card < input.board_count; ++card) scratch.board[card] = input.board[card];
            mark_used(scratch, input.board_count);
            const auto value = one_rollout(scratch, input, context, MULTIWAY_FIXED_CONTINUATION_POLICIES[policy_index],
                                           request.traverser, context.seeds[seed], &actions, &exact_runouts, &capped,
                                           &used_exact_runout, &used_seeded_runout);
            if (!std::isfinite(value)) {
                record_invalid(context.diagnostics);
                return false;
            }
            total += value;
        }
        output->values[policy_index] = total / static_cast<Value>(rollout_count);
        output->betting_actions = actions;
        output->exact_runouts = exact_runouts;
        if (capped) output->status = MultiwayRolloutStatus::CappedFallback;
    }
    if (output->status != MultiwayRolloutStatus::CappedFallback) output->status = MultiwayRolloutStatus::Complete;
    output->seed_count = static_cast<std::uint32_t>(context.seed_count);
    output->runout_mode = used_exact_runout && used_seeded_runout
        ? MultiwayRolloutRunoutMode::Mixed
        : (used_exact_runout ? MultiwayRolloutRunoutMode::Exact
                             : (used_seeded_runout ? MultiwayRolloutRunoutMode::Seeded
                                                   : MultiwayRolloutRunoutMode::None));
    if (cacheable) {
        context.cache->record_repeated_seed_variance(cache_key, *output, context.diagnostics);
        if (!context.cache->try_insert(cache_key, *output) && context.diagnostics != nullptr) {
            ++context.diagnostics->cache_admission_rejections;
        }
    }
    record_success(context.diagnostics, *output);
    return true;
}

Value evaluate_multiway_rollout_leaf(const MultiwayLeafEvaluationRequest& request, const void* context) noexcept {
    if (context == nullptr) return kInvalid;
    const auto& leaf = *static_cast<const MultiwayRolloutLeafContext*>(context);
    MultiwayRolloutProfileResult result;
    if (!evaluate_multiway_rollout_profiles(request, leaf, &result)) return kInvalid;
    const auto policy = selected_policy(request, leaf);
    for (std::size_t index = 0; index < MULTIWAY_FIXED_CONTINUATION_POLICIES.size(); ++index) {
        if (policy == MULTIWAY_FIXED_CONTINUATION_POLICIES[index]) return result.values[index];
    }
    return kInvalid;
}

MultiwayLeafEvaluator make_multiway_rollout_leaf_evaluator(const MultiwayRolloutLeafContext* context) noexcept {
    if (context == nullptr || !valid_policy(context->selected_policy)) return {};
    return {evaluate_multiway_rollout_leaf, context};
}

}  // namespace core
