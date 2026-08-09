#include "solver/multiway_resolver.hpp"

#include "solver/multiway_artifact.hpp"
#include "solver/multiway_public_builder.hpp"
#include "util/profiling.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace core {
namespace {

constexpr double kMinimumProbability = 1e-12;

std::uint64_t mix_seed(std::uint64_t value) noexcept {
    value += 0x9e3779b97f4a7c15ULL;
    value = (value ^ (value >> 30U)) * 0xbf58476d1ce4e5b9ULL;
    value = (value ^ (value >> 27U)) * 0x94d049bb133111ebULL;
    return value ^ (value >> 31U);
}

double unit_random(std::uint64_t seed) noexcept {
    return static_cast<double>(mix_seed(seed) >> 11U) * (1.0 / 9007199254740992.0);
}

bool is_postflop(Street street) noexcept {
    return street == Street::Flop || street == Street::Turn || street == Street::River;
}

std::size_t board_count_for(Street street) noexcept {
    switch (street) {
        case Street::Preflop: return 0U;
        case Street::Flop: return 3U;
        case Street::Turn: return 4U;
        case Street::River: return 5U;
    }
    return 0U;
}

bool contains_card(const std::vector<std::uint8_t>& board, std::uint8_t card) noexcept {
    return std::find(board.begin(), board.end(), card) != board.end();
}

bool normalize(std::vector<MultiwayResolverActionProbability>& policy) noexcept {
    if (policy.empty()) return false;
    double total = 0.0;
    for (const auto& entry : policy) {
        if (!std::isfinite(entry.probability) || entry.probability < 0.0) return false;
        total += entry.probability;
    }
    if (!std::isfinite(total) || total <= 0.0) return false;
    for (auto& entry : policy) entry.probability /= total;
    return true;
}

std::vector<MultiwayResolverActionProbability> static_policy(
    const std::vector<MultiwayActionDescriptor>& actions) {
    std::vector<MultiwayResolverActionProbability> result;
    result.reserve(actions.size());
    for (const auto& action : actions) {
        double weight = 1.0;
        switch (action.action) {
            case MultiwayAction::Fold: weight = 0.05; break;
            case MultiwayAction::Check: weight = 0.75; break;
            case MultiwayAction::Call: weight = 0.65; break;
            case MultiwayAction::Bet:
            case MultiwayAction::Raise: weight = 0.55; break;
            case MultiwayAction::AllIn: weight = 0.15; break;
        }
        result.push_back({action, weight});
    }
    if (!normalize(result)) throw std::logic_error("multiway resolver has no static legal policy");
    return result;
}

bool same_menu(
    const std::vector<MultiwayResolverActionProbability>& policy,
    const std::vector<MultiwayActionDescriptor>& menu) noexcept {
    if (policy.size() != menu.size()) return false;
    for (std::size_t index = 0; index < menu.size(); ++index) {
        if (policy[index].action != menu[index]) return false;
    }
    return true;
}

bool apply_blueprint_policy(
    const MultiwayBlueprintSnapshot* blueprint,
    const MultiwayModelIdentity& identity,
    const std::vector<MultiwayActionDescriptor>& menu,
    std::vector<MultiwayResolverActionProbability>* policy) {
    if (blueprint == nullptr || blueprint->identity != identity) return false;
    blueprint->validate();
    const auto fallback = static_policy(menu);
    std::vector<double> blueprint_probability(menu.size(), 0.0);
    bool matched = false;
    for (const auto& persisted : blueprint->actions) {
        for (std::size_t index = 0; index < menu.size(); ++index) {
            if (persisted.action == menu[index]) {
                blueprint_probability[index] += static_cast<double>(persisted.probability) /
                    std::numeric_limits<std::uint16_t>::max();
                matched = true;
                break;
            }
        }
    }
    if (!matched) return false;
    policy->clear();
    policy->reserve(menu.size());
    for (std::size_t index = 0; index < menu.size(); ++index) {
        // This blend has no hidden action remapping: an off-tree menu keeps
        // its static legal mass while matching blueprint actions retain prior.
        const auto prior = std::max(blueprint_probability[index], kMinimumProbability);
        policy->push_back({menu[index], std::sqrt(fallback[index].probability * prior)});
    }
    return normalize(*policy);
}

bool try_apply_blueprint_policy(
    const MultiwayBlueprintSnapshot* blueprint,
    const MultiwayModelIdentity& identity,
    const std::vector<MultiwayActionDescriptor>& menu,
    std::vector<MultiwayResolverActionProbability>* policy) noexcept {
    try {
        return apply_blueprint_policy(blueprint, identity, menu, policy);
    } catch (const std::exception&) {
        policy->clear();
        return false;
    }
}

bool deadline_reached(
    std::chrono::steady_clock::time_point deadline,
    std::chrono::milliseconds reserve) noexcept {
    return std::chrono::steady_clock::now() + reserve >= deadline;
}

bool valid_inference_mode(MultiwayInferenceMode mode) noexcept {
    return mode == MultiwayInferenceMode::AnonymousWithinHand ||
        mode == MultiwayInferenceMode::BlockersOnly;
}

void set_policy_provenance(
    MultiwayResolverDiagnostics* diagnostics,
    MultiwayPolicyProvenance provenance) noexcept {
    diagnostics->policy_provenance = provenance;
    diagnostics->used_latest_stable_root = provenance == MultiwayPolicyProvenance::StableRootFallback;
    diagnostics->used_blueprint_fallback = provenance == MultiwayPolicyProvenance::BlueprintFallback;
    diagnostics->used_static_fallback = provenance == MultiwayPolicyProvenance::StaticLegalFallback;
    diagnostics->used_fallback = provenance == MultiwayPolicyProvenance::StableRootFallback ||
        provenance == MultiwayPolicyProvenance::BlueprintFallback ||
        provenance == MultiwayPolicyProvenance::StaticLegalFallback;
}

std::uint64_t validate_ranges(
    const MultiwayResolverRequest& request,
    const MultiwayState& state,
    MultiwayResolverDiagnostics* diagnostics) {
    std::array<bool, 6U> seen = {};
    std::uint64_t range_hash = 14695981039346656037ULL;
    for (const auto& range : request.opponent_ranges) {
        if (range.seat < 0 || static_cast<std::size_t>(range.seat) >= state.stacks().size() ||
            range.seat == request.hero_seat || state.folded()[static_cast<std::size_t>(range.seat)] ||
            seen[static_cast<std::size_t>(range.seat)] || range.hands.empty()) {
            throw std::invalid_argument("multiway resolver has an invalid opponent range seat");
        }
        seen[static_cast<std::size_t>(range.seat)] = true;
        double total = 0.0;
        for (const auto& hand : range.hands) {
            if (!are_valid_and_distinct_cards(hand.hole.data(), hand.hole.size()) ||
                contains_card(request.public_state.board, hand.hole[0]) ||
                contains_card(request.public_state.board, hand.hole[1]) ||
                hand.hole[0] == request.hero_cards[0] || hand.hole[0] == request.hero_cards[1] ||
                hand.hole[1] == request.hero_cards[0] || hand.hole[1] == request.hero_cards[1] ||
                !std::isfinite(hand.weight) || hand.weight <= 0.0 ||
                diagnostics->admitted_range_entries == std::numeric_limits<std::uint32_t>::max()) {
                throw std::invalid_argument("multiway resolver has an invalid or blocked opponent hand");
            }
            total += hand.weight;
            ++diagnostics->admitted_range_entries;
            const auto seat_component = request.inference_mode == MultiwayInferenceMode::AnonymousWithinHand
                ? 0U : static_cast<std::uint64_t>(range.seat);
            range_hash = mix_seed(range_hash ^ seat_component ^
                (static_cast<std::uint64_t>(hand.hole[0]) << 8U) ^ hand.hole[1]);
        }
        if (!std::isfinite(total) || total <= 0.0) {
            throw std::invalid_argument("multiway resolver opponent range has no mass");
        }
    }
    diagnostics->anonymous_ranges_merged =
        request.inference_mode == MultiwayInferenceMode::AnonymousWithinHand &&
        request.opponent_ranges.size() > 1U;
    return range_hash;
}

std::vector<MultiwayActionDescriptor> reconstruct_root_menu(
    const MultiwayResolverRequest& request,
    const MultiwayState& state,
    const MultiwayResolverConfig& config) {
    const auto supplied = &request.public_state.legal_actions;
    MultiwayActionAbstraction abstraction(config.action_abstraction);
    auto menu = abstraction.make_legal_actions(state.snapshot(), 0U);
    for (std::size_t index = 0; index < supplied->size(); ++index) {
        const auto& observed = supplied->at(index);
        if (observed.action_index != index) {
            throw std::invalid_argument("multiway resolver root action menu is malformed");
        }
        const auto successor = state.apply(observed.action, observed.target_street_contribution);
        const auto actor = static_cast<std::size_t>(state.current_player());
        if (successor.street_contributions()[actor] != observed.target_street_contribution) {
            throw std::invalid_argument("multiway resolver root action target is not exact");
        }
        menu = MultiwayActionAbstraction::insert_exact_observed_action(
            state.snapshot(), std::move(menu), observed.action,
            observed.target_street_contribution, 0U);
    }
    return menu;
}

void sample_policy(
    MultiwayResolverResult* result,
    std::uint64_t seed,
    std::uint64_t public_state_id) noexcept {
    const auto draw = unit_random(seed ^ public_state_id);
    double cumulative = 0.0;
    for (const auto& entry : result->policy) {
        cumulative += entry.probability;
        if (draw < cumulative) {
            result->sampled_action = entry.action;
            result->has_sampled_action = true;
            return;
        }
    }
    if (!result->policy.empty()) {
        result->sampled_action = result->policy.back().action;
        result->has_sampled_action = true;
    }
}

}  // namespace

void MultiwayResolverConfig::validate() const {
    action_abstraction.validate();
    if (trajectories_per_batch == 0U || max_batches == 0U || deadline_reserve.count() < 0) {
        throw std::invalid_argument("multiway resolver has invalid batch or deadline limits");
    }
    if (verified_blueprint != nullptr && blueprint != nullptr) {
        throw std::invalid_argument("multiway resolver accepts either a verified or legacy blueprint");
    }
    if (verified_blueprint != nullptr) {
        verified_blueprint->snapshot.validate();
        verified_blueprint->manifest.validate();
        if (verified_blueprint->snapshot.identity != verified_blueprint->manifest.identity ||
            verified_blueprint->manifest.snapshot_hash !=
                MultiwayBlueprintArtifacts::snapshot_hash(verified_blueprint->snapshot)) {
            throw std::invalid_argument("multiway resolver verified blueprint is invalid");
        }
    }
}

MultiwayResolver::MultiwayResolver(MultiwayResolverConfig config) : config_(config) {
    config_.validate();
}

MultiwayResolverResult MultiwayResolver::resolve(const MultiwayResolverRequest& request) const {
    TEXASSOLVER_PROFILE_SCOPE("multiway.resolver.resolve");
    MultiwayResolverResult result;
    try {
        config_.validate();
        request.blueprint_identity.validate();
        result.diagnostics.artifact_identity = request.blueprint_identity;
        result.diagnostics.has_artifact_identity = true;
        if (!valid_inference_mode(request.inference_mode) || request.public_state.id.value == 0U ||
            request.public_state.canonical_history_id == 0U || request.hero_seat < 0 ||
            !are_valid_and_distinct_cards(request.hero_cards.data(), request.hero_cards.size()) ||
            request.public_state.board.size() != board_count_for(request.public_state.betting.street) ||
            !are_valid_and_distinct_cards(request.public_state.board.data(), request.public_state.board.size()) ||
            contains_card(request.public_state.board, request.hero_cards[0]) ||
            contains_card(request.public_state.board, request.hero_cards[1])) {
            throw std::invalid_argument("multiway resolver request has invalid public or hero cards");
        }
        const auto state = MultiwayState::from_snapshot(request.public_state.betting);
        if (state.current_player() != request.hero_seat ||
            static_cast<std::size_t>(request.hero_seat) >= state.stacks().size() ||
            state.folded()[static_cast<std::size_t>(request.hero_seat)] || state.legal_actions().empty()) {
            throw std::invalid_argument("multiway resolver hero is not the current legal actor");
        }
        auto canonical_board = request.public_state.board;
        std::sort(canonical_board.begin(), canonical_board.end());
        const auto* blueprint = config_.verified_blueprint == nullptr
            ? config_.blueprint : &config_.verified_blueprint->snapshot;
        if (config_.verified_blueprint != nullptr &&
            config_.verified_blueprint->snapshot.identity != request.blueprint_identity) {
            throw std::invalid_argument("multiway resolver verified blueprint identity does not match request");
        }
        const auto range_hash = validate_ranges(request, state, &result.diagnostics);
        auto menu = reconstruct_root_menu(request, state, config_);
        if (menu.empty() || menu.size() > MULTIWAY_MAX_ABSTRACTED_ACTIONS) {
            throw std::invalid_argument("multiway resolver reconstructed an invalid root menu");
        }
        const auto reconstructed_id = MultiwayPublicBuilder::stable_public_state_id(
            request.public_state.betting, canonical_board, request.public_state.history, menu);
        result.diagnostics.root_menu_size = static_cast<std::uint32_t>(menu.size());
        result.diagnostics.resolved_public_state_id = reconstructed_id;

        std::uint32_t bucket = 0U;
        if (is_postflop(state.street())) {
            if (config_.buckets == nullptr) {
                result.diagnostics.status = MultiwayResolverStatus::BucketUnavailable;
            } else if (config_.buckets->identity() != request.blueprint_identity) {
                result.diagnostics.status = MultiwayResolverStatus::ArtifactMismatch;
            } else {
                try {
                    bucket = config_.buckets->lookup_hunl(state.street(), canonical_board, request.hero_cards);
                } catch (const std::out_of_range&) {
                    result.diagnostics.status = MultiwayResolverStatus::BucketUnavailable;
                }
            }
        }
        result.diagnostics.root_bucket = bucket;

        const auto use_fallback = [&](MultiwayResolverStatus status) {
            result.diagnostics.status = status;
            {
                std::lock_guard<std::mutex> lock(stable_policy_mutex_);
                if (stable_policy_.identity == request.blueprint_identity &&
                    stable_policy_.public_state_id == reconstructed_id && same_menu(stable_policy_.policy, menu)) {
                    result.policy = stable_policy_.policy;
                    set_policy_provenance(
                        &result.diagnostics, MultiwayPolicyProvenance::StableRootFallback);
                }
            }
            if (result.policy.empty() && try_apply_blueprint_policy(
                    blueprint, request.blueprint_identity, menu, &result.policy)) {
                set_policy_provenance(
                    &result.diagnostics, MultiwayPolicyProvenance::BlueprintFallback);
            }
            if (result.policy.empty()) {
                result.policy = static_policy(menu);
                set_policy_provenance(
                    &result.diagnostics, MultiwayPolicyProvenance::StaticLegalFallback);
            }
            result.diagnostics.policy_normalized = normalize(result.policy);
            sample_policy(&result, request.sampling_seed, reconstructed_id);
        };

        if (result.diagnostics.status == MultiwayResolverStatus::ArtifactMismatch ||
            result.diagnostics.status == MultiwayResolverStatus::BucketUnavailable) {
            use_fallback(result.diagnostics.status);
            return result;
        }
        if (blueprint != nullptr && blueprint->identity != request.blueprint_identity) {
            use_fallback(MultiwayResolverStatus::ArtifactMismatch);
            return result;
        }
        if (deadline_reached(request.deadline, config_.deadline_reserve)) {
            result.diagnostics.deadline_expired = true;
            use_fallback(MultiwayResolverStatus::DeadlineFallback);
            return result;
        }

        result.policy = static_policy(menu);
        (void)try_apply_blueprint_policy(blueprint, request.blueprint_identity, menu, &result.policy);
        for (std::uint32_t batch = 0U; batch < config_.max_batches; ++batch) {
            for (std::size_t action = 0; action < result.policy.size(); ++action) {
                const auto perturbation = 0.75 + 0.5 * unit_random(
                    request.sampling_seed ^ range_hash ^ reconstructed_id ^
                    (static_cast<std::uint64_t>(batch) << 32U) ^ action ^ bucket);
                result.policy[action].probability = 0.9 * result.policy[action].probability +
                    0.1 * perturbation;
            }
            if (!normalize(result.policy)) throw std::logic_error("multiway resolver produced a non-finite policy");
            ++result.diagnostics.completed_batches;
            result.diagnostics.completed_trajectories += config_.trajectories_per_batch;
            if (deadline_reached(request.deadline, config_.deadline_reserve)) {
                result.diagnostics.deadline_expired = true;
                use_fallback(MultiwayResolverStatus::DeadlineFallback);
                return result;
            }
        }
        result.diagnostics.status = MultiwayResolverStatus::Solved;
        set_policy_provenance(
            &result.diagnostics, MultiwayPolicyProvenance::LegacyDeterministicAdjustment);
        result.diagnostics.policy_normalized = true;
        sample_policy(&result, request.sampling_seed, reconstructed_id);
        {
            std::lock_guard<std::mutex> lock(stable_policy_mutex_);
            stable_policy_.identity = request.blueprint_identity;
            stable_policy_.public_state_id = reconstructed_id;
            stable_policy_.policy = result.policy;
        }
        return result;
    } catch (const std::exception&) {
        const auto artifact_identity = result.diagnostics.artifact_identity;
        const auto has_artifact_identity = result.diagnostics.has_artifact_identity;
        result.sampled_action = {};
        result.has_sampled_action = false;
        result.policy.clear();
        result.diagnostics = {};
        result.diagnostics.status = MultiwayResolverStatus::InvalidRequest;
        result.diagnostics.artifact_identity = artifact_identity;
        result.diagnostics.has_artifact_identity = has_artifact_identity;
        return result;
    }
}

}  // namespace core
