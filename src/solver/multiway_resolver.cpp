#include "solver/multiway_resolver.hpp"
#include "solver/multiway_memory.hpp"

#include "solver/multiway_artifact.hpp"
#include "solver/multiway_blueprint_policy_provider.hpp"
#include "solver/multiway_public_builder.hpp"
#include "solver/multiway_resolver_budget.hpp"
#include "solver/multiway_resolver_policy.hpp"
#include "solver/multiway_search_session.hpp"
#include "solver/multiway_decision_session.hpp"
#include "solver/multiway_rollout_leaf.hpp"
#include "solver/multiway_traversal.hpp"
#include "util/profiling.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <limits>
#include <optional>
#include <stdexcept>
#include <utility>

namespace texas::solver::multiway {
namespace {

constexpr double kMinimumProbability = 1e-12;

struct RuntimeSearchMeasurement {
    explicit RuntimeSearchMeasurement(MultiwayResolverDiagnostics* diagnostics) noexcept
        : diagnostics(diagnostics), started(std::chrono::steady_clock::now()) {}

    ~RuntimeSearchMeasurement() {
        diagnostics->search_elapsed_nanoseconds = static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(
                std::chrono::steady_clock::now() - started).count());
        diagnostics->search_observed_memory_bytes = observed_multiway_process_memory_bytes();
    }

    MultiwayResolverDiagnostics* diagnostics = nullptr;
    std::chrono::steady_clock::time_point started{};
};

enum class RuntimeSearchOutcome : std::uint8_t {
    NoRoot,
    MemoryRejected,
    NoCleanBatch,
    NormalizationFailed,
    Exception,
    Completed,
};

MultiwayResolverSearchFailure search_failure(RuntimeSearchOutcome outcome) noexcept {
    switch (outcome) {
        case RuntimeSearchOutcome::NoRoot: return MultiwayResolverSearchFailure::NoRoot;
        case RuntimeSearchOutcome::MemoryRejected: return MultiwayResolverSearchFailure::MemoryRejected;
        case RuntimeSearchOutcome::NoCleanBatch: return MultiwayResolverSearchFailure::NoCleanBatch;
        case RuntimeSearchOutcome::NormalizationFailed:
            return MultiwayResolverSearchFailure::NormalizationFailed;
        case RuntimeSearchOutcome::Exception: return MultiwayResolverSearchFailure::Exception;
        case RuntimeSearchOutcome::Completed: return MultiwayResolverSearchFailure::None;
    }
    return MultiwayResolverSearchFailure::Exception;
}

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

double policy_l1_distance(
    const std::vector<MultiwayResolverActionProbability>& left,
    const std::vector<MultiwayResolverActionProbability>& right) noexcept {
    double distance = 0.0;
    for (const auto& right_entry : right) {
        double left_probability = 0.0;
        for (const auto& left_entry : left) {
            if (left_entry.action == right_entry.action) {
                left_probability = left_entry.probability;
                break;
            }
        }
        distance += std::fabs(left_probability - right_entry.probability);
    }
    for (const auto& left_entry : left) {
        bool present = false;
        for (const auto& right_entry : right) {
            if (right_entry.action == left_entry.action) {
                present = true;
                break;
            }
        }
        if (!present) distance += left_entry.probability;
    }
    return distance;
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
    if (!normalize_multiway_resolver_policy(&result)) {
        throw std::logic_error("multiway resolver has no static legal policy");
    }
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
    return normalize_multiway_resolver_policy(policy);
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

bool valid_search_mode(MultiwayResolverSearchMode mode) noexcept {
    return mode == MultiwayResolverSearchMode::ReleaseDefault ||
        mode == MultiwayResolverSearchMode::FallbackOnly ||
        mode == MultiwayResolverSearchMode::SearchShadow ||
        mode == MultiwayResolverSearchMode::SearchActive ||
        mode == MultiwayResolverSearchMode::ForcedFallback;
}

bool has_runtime_search_configuration(const MultiwayResolverConfig& config) noexcept {
    return config.leaf_evaluator != nullptr && config.leaf_evaluator->valid() &&
        config.runtime_limits.max_decision_depth != 0U &&
        config.runtime_limits.max_decision_depth <= MULTIWAY_MAX_DECISION_DEPTH &&
        config.runtime_limits.max_public_chance_depth <= MULTIWAY_MAX_PUBLIC_CHANCE_DEPTH;
}

bool has_release_search_configuration(const MultiwayResolverConfig& config) noexcept {
    return has_runtime_search_configuration(config) && config.buckets != nullptr &&
        config.verified_blueprint != nullptr && config.full_blueprint != nullptr;
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

void validate_ranges(
    const MultiwayResolverRequest& request,
    const MultiwayState& state,
    MultiwayResolverDiagnostics* diagnostics) {
    std::array<bool, 6U> seen = {};
    if (request.hero_range.empty()) {
        throw std::invalid_argument("multiway resolver requires an explicit hero range");
    }
    bool hero_present = false;
    for (const auto& hand : request.hero_range) {
        if (!are_valid_and_distinct_cards(hand.hole.data(), hand.hole.size()) ||
            contains_card(request.public_state.board, hand.hole[0]) ||
            contains_card(request.public_state.board, hand.hole[1]) ||
            !std::isfinite(hand.weight) || hand.weight <= 0.0 ||
            diagnostics->admitted_range_entries == std::numeric_limits<std::uint32_t>::max()) {
            throw std::invalid_argument("multiway resolver has an invalid hero range hand");
        }
        hero_present = hero_present || hand.hole == request.hero_cards;
        ++diagnostics->admitted_range_entries;
    }
    if (!hero_present) throw std::invalid_argument("multiway resolver hero range excludes the actual hand");
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
        }
        if (!std::isfinite(total) || total <= 0.0) {
            throw std::invalid_argument("multiway resolver opponent range has no mass");
        }
    }
}

std::vector<MultiwayActionDescriptor> reconstruct_root_menu(
    const MultiwayResolverRequest& request,
    const MultiwayState& state,
    const MultiwayResolverConfig& config) {
    const auto supplied = &request.public_state.legal_actions;
    MultiwayActionAbstraction abstraction(config.action_abstraction);
    auto menu = abstraction.make_legal_actions(state.snapshot());
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
            observed.target_street_contribution);
    }
    return menu;
}

MultiwayResolverSearchEligibility search_eligibility(
    const MultiwayResolverRequest& request,
    const MultiwayState& state,
    const std::vector<MultiwayActionDescriptor>& menu,
    const MultiwayResolverConfig& config) noexcept {
    if (!is_postflop(state.street())) {
        return MultiwayResolverSearchEligibility::UnsupportedStreet;
    }
    const auto seat_count = state.stacks().size();
    if (seat_count < config.runtime_limits.active_min_seats ||
        seat_count > config.runtime_limits.active_max_seats) {
        return MultiwayResolverSearchEligibility::SeatCount;
    }
    if (menu.size() > config.runtime_limits.active_max_menu_actions) {
        return MultiwayResolverSearchEligibility::MenuTooLarge;
    }

    std::array<bool, 6U> has_range = {};
    for (const auto& range : request.opponent_ranges) {
        has_range[static_cast<std::size_t>(range.seat)] = true;
    }
    for (std::size_t seat = 0U; seat < seat_count; ++seat) {
        if (static_cast<PlayerId>(seat) == request.hero_seat) continue;
        if (state.folded()[seat]) return MultiwayResolverSearchEligibility::FoldedSeat;
        if (!has_range[seat]) return MultiwayResolverSearchEligibility::IncompleteRanges;
    }
    return MultiwayResolverSearchEligibility::Eligible;
}

bool make_search_root(
    const MultiwayResolverRequest& request,
    const MultiwayState& state,
    const std::vector<std::uint8_t>& board,
    const std::vector<MultiwayActionDescriptor>& menu,
    std::uint32_t bucket,
    MultiwayRootSnapshot* root) {
    const auto seat_count = state.stacks().size();
    if (state.street() < Street::Flop || state.street() > Street::River ||
        seat_count < 2U || seat_count > 6U) {
        return false;
    }

    std::array<const MultiwayResolverSeatRange*, 6U> ranges = {};
    for (const auto& range : request.opponent_ranges) {
        ranges[static_cast<std::size_t>(range.seat)] = &range;
    }
    for (std::size_t seat = 0U; seat < seat_count; ++seat) {
        if (static_cast<PlayerId>(seat) != request.hero_seat &&
            (state.folded()[seat] || ranges[seat] == nullptr)) {
            return false;
        }
    }

    root->public_state = MultiwayPublicBuilder::make_root(state.snapshot(), board, menu);
    root->root_infoset = {root->public_state.id, request.hero_seat};
    // The live root is the lossless current-round boundary.  Keep future
    // continuation rows bucketed, but index this row by the canonical exact
    // private-hand id so blockers and draws cannot share regrets.
    root->root_bucket = static_cast<std::uint32_t>(
        MultiwayBucketTable::hole_index(request.hero_cards));
    root->root_uses_exact_private_hand = true;
    root->seat_order.clear();
    root->seat_order.reserve(seat_count);
    for (std::size_t seat = 0U; seat < seat_count; ++seat) {
        root->seat_order.push_back(static_cast<PlayerId>(seat));
    }
    root->next_street_first_seat = 0;
    root->odd_chip_first_seat = 0;
    root->private_ranges.board = board;
    root->private_ranges.ranges.assign(seat_count, {});
    for (std::size_t seat = 0U; seat < seat_count; ++seat) {
        if (static_cast<PlayerId>(seat) == request.hero_seat) {
            root->private_ranges.ranges[seat] = request.hero_range;
        } else {
            root->private_ranges.ranges[seat] = ranges[seat]->hands;
        }
    }
    root->action_abstraction_version = request.blueprint_identity.action_abstraction_hash;
    root->leaf_model_version = request.blueprint_identity.terminal_model_hash;
    return true;
}

std::uint64_t saturating_add(std::uint64_t left, std::uint64_t right) noexcept {
    return right > std::numeric_limits<std::uint64_t>::max() - left
        ? std::numeric_limits<std::uint64_t>::max()
        : left + right;
}

std::uint64_t saturating_multiply(std::uint64_t left, std::uint64_t right) noexcept {
    return left != 0U && right > std::numeric_limits<std::uint64_t>::max() / left
        ? std::numeric_limits<std::uint64_t>::max()
        : left * right;
}

MultiwayMemoryInputs search_memory_inputs(
    const MultiwayResolverRequest& request,
    const MultiwayState& state,
    std::size_t root_action_count,
    const MultiwayResolverConfig& config,
    const MultiwaySolverLimits& limits) noexcept {
    MultiwayMemoryInputs inputs;
    inputs.blueprint_index_bytes = config.full_blueprint == nullptr
        ? 0U : config.full_blueprint->memory_bytes();
    inputs.range_row_count = state.stacks().size();
    inputs.range_entry_count = request.hero_range.size();
    for (const auto& range : request.opponent_ranges) {
        inputs.range_entry_count = saturating_add(inputs.range_entry_count, range.hands.size());
    }
    inputs.future_bucket_cache_bytes = config.runtime_limits.future_bucket_cache_bytes;
    inputs.off_tree_menu_entries = saturating_multiply(
        limits.max_public_states,
        config.runtime_limits.active_max_menu_actions);
    inputs.continuation_scratch_bytes_per_worker =
        config.runtime_limits.continuation_scratch_bytes_per_worker;
    inputs.continuation_cache_bytes = config.runtime_limits.continuation_cache_bytes;
    if (config.leaf_evaluator != nullptr &&
        config.leaf_evaluator->evaluate == &evaluate_multiway_rollout_leaf &&
        config.leaf_evaluator->context != nullptr) {
        const auto* rollout = static_cast<const MultiwayRolloutLeafContext*>(
            config.leaf_evaluator->context);
        if (inputs.continuation_scratch_bytes_per_worker == 0U) {
            inputs.continuation_scratch_bytes_per_worker = sizeof(MultiwayRolloutScratch);
        }
        if (inputs.continuation_cache_bytes == 0U && rollout->cache != nullptr) {
            inputs.continuation_cache_bytes = rollout->cache->memory_bytes();
        }
    }
    inputs.export_action_capacity = root_action_count;
    return inputs;
}

void record_memory_preflight(
    const MultiwayMemoryPreflight& preflight,
    MultiwayResolverDiagnostics* diagnostics) noexcept {
    diagnostics->search_memory_status = preflight.status;
    diagnostics->search_memory_stage = preflight.highest_admitted_stage;
    diagnostics->search_estimated_memory_bytes = preflight.estimate.total_bytes;
    diagnostics->search_admitted_memory_bytes = preflight.estimate.admitted_bytes;
    diagnostics->search_memory_degraded = preflight.degraded;
}

RuntimeSearchOutcome run_search(
    const MultiwayResolverRequest& request,
    const MultiwayState& state,
    const MultiwayResolverConfig& config,
    const std::vector<std::uint8_t>& board,
    const std::vector<MultiwayActionDescriptor>& menu,
    std::uint32_t bucket,
    std::uint64_t public_state_id,
    std::vector<MultiwayResolverActionProbability>* policy,
    MultiwayResolverDiagnostics* diagnostics) {
    try {
        auto budget_limits = config.runtime_limits.solver;
        // The live resolver root is indexed by canonical private hand. Its
        // dense row is intentionally larger than a continuation bucket row.
        const auto exact_root_values = saturating_multiply(
            MULTIWAY_HOLE_COMBINATION_COUNT, menu.size());
        const auto admitted_sparse_values = saturating_add(
            budget_limits.max_sparse_values, exact_root_values);
        budget_limits.max_sparse_values = admitted_sparse_values >
                static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max())
            ? std::numeric_limits<std::size_t>::max()
            : static_cast<std::size_t>(admitted_sparse_values);
        const auto memory_preflight = preflight_multiway_memory(
            budget_limits,
            config.runtime_limits.memory,
            search_memory_inputs(request, state, menu.size(), config, budget_limits));
        record_memory_preflight(memory_preflight, diagnostics);
        if (memory_preflight.status == MultiwayMemoryStatus::Rejected) {
            return RuntimeSearchOutcome::MemoryRejected;
        }
        if (config.future_bucket_artifact != nullptr &&
            config.future_bucket_artifact->registry().identity() != request.blueprint_identity) {
            throw std::invalid_argument("multiway future bucket artifact identity does not match the request");
        }
        MultiwayRootSnapshot root;
        if (!make_search_root(request, state, board, menu, bucket, &root)) {
            return RuntimeSearchOutcome::NoRoot;
        }
        MultiwayCFRConfig cfr;
        cfr.player_count = static_cast<std::uint8_t>(root.seat_order.size());
        MultiwaySolveRequest solve_request(std::move(root), cfr, config.runtime_limits.solver);
        // The release search uses the same live owner exposed to hosts. This
        // keeps beliefs, reroot metadata, freeze state, and clean snapshots
        // on one lifecycle object instead of maintaining a parallel search
        // session implementation inside the resolver.
        MultiwayDecisionSession session(
            std::move(solve_request),
            MultiwaySearchSessionDependencies{config.buckets, config.continuation_selector});
        RuntimeSearchMeasurement measurement(diagnostics);
        MultiwayActionAbstraction abstraction(config.action_abstraction);
        std::optional<MultiwayRolloutLeafContext> rollout_context;
        MultiwayLeafEvaluator effective_leaf = *config.leaf_evaluator;
        if (config.leaf_evaluator->evaluate == &evaluate_multiway_rollout_leaf &&
            config.leaf_evaluator->context != nullptr) {
            if (config.runtime_limits.solver.worker_count > 1U) {
                // Rollout contexts own mutable scratch, cache, and diagnostics.
                // A shallow copy would make concurrent workers race on all
                // three, so reject this configuration until worker-local
                // context construction is available at the public boundary.
                return RuntimeSearchOutcome::Exception;
            }
            rollout_context = *static_cast<const MultiwayRolloutLeafContext*>(
                config.leaf_evaluator->context);
            if (!memory_preflight.optional_continuation_cache_admitted) {
                rollout_context->cache = nullptr;
            }
            effective_leaf = make_multiway_rollout_leaf_evaluator(&*rollout_context);
        }
        std::optional<MultiwayBlueprintPolicyProvider> blueprint_provider;
        if (config.full_blueprint != nullptr) blueprint_provider.emplace(*config.full_blueprint);
        MultiwayRootExternalSamplingTraversal traversal(
            session.round().coordinator(), session.round().coordinator().root(), abstraction, *config.buckets,
            &effective_leaf, config.runtime_limits.max_decision_depth,
            config.runtime_limits.max_public_chance_depth,
            blueprint_provider ? &*blueprint_provider : nullptr,
            config.continuation_selector ? config.continuation_selector.get() : nullptr,
            config.future_bucket_artifact.get());
        MultiwayRootBatchRunner runner(
            traversal, session.round().coordinator(), config.runtime_limits.solver.worker_count,
            config.runtime_limits.solver.max_worker_delta_entries,
            config.search_profile_mode);
        MultiwaySearchProfile search_profile(config.search_profile_mode);

        MultiwayResolverBudget budget({
            request.deadline,
            config.runtime_limits.deadline_reserve,
            config.runtime_limits.solver.max_batches,
            config.runtime_limits.solver.trajectories_per_batch,
            budget_limits.max_sparse_rows,
            budget_limits.max_sparse_values,
        });
        std::uint64_t first_trajectory = 0U;
        for (std::uint32_t batch = 0U; batch < config.runtime_limits.solver.max_batches; ++batch) {
            if (budget.checkpoint(batch, first_trajectory) !=
                MultiwayResolverBudgetCheckpoint::Ready) {
                break;
            }
            const auto batch_first_trajectory = first_trajectory;
            const auto batch_result = runner.run(
                batch_first_trajectory, config.runtime_limits.solver.trajectories_per_batch,
                request.sampling_seed ^ public_state_id);
            search_profile.merge(batch_result.profile);
            first_trajectory += batch_result.trajectories_attempted;
            if (!budget.accept_clean_batch(
                    batch_result.clean,
                    batch_result.trajectories_accepted,
                    batch_result.delta_entries_merged,
                    session.round().coordinator().storage().row_count(),
                    session.round().coordinator().storage().value_count())) {
                return RuntimeSearchOutcome::NoCleanBatch;
            }
            {
                MultiwaySearchProfileScope profile_scope(
                    &search_profile, MultiwaySearchProfileStage::RootExport);
                if (!session.round().capture_clean_snapshot(
                        batch_result.clean,
                        static_cast<std::uint64_t>(batch) + 1U,
                        batch_first_trajectory,
                        batch_result.trajectories_attempted,
                        batch_result.trajectories_accepted,
                        batch_result.delta_entries_merged,
                        config.runtime_limits.solver.worker_count)) {
                    diagnostics->search_profile = search_profile.snapshot();
                    return RuntimeSearchOutcome::NoCleanBatch;
                }
            }
            const auto* snapshot = session.round().clean_snapshot();
            if (snapshot == nullptr) return RuntimeSearchOutcome::NoCleanBatch;
            ++diagnostics->completed_batches;
            diagnostics->completed_trajectories += batch_result.trajectories_accepted;
            diagnostics->search_merged_delta_entries += batch_result.delta_entries_merged;
            diagnostics->search_first_trajectory_id = snapshot->first_trajectory_id;
            diagnostics->search_trajectory_count = snapshot->trajectory_count;
            diagnostics->search_root_revision = snapshot->root_revision;
            diagnostics->search_worker_count = snapshot->worker_count;
            diagnostics->search_admitted_rows = snapshot->rows.row_count;
            diagnostics->search_admitted_values = snapshot->rows.value_count;
            diagnostics->search_schedule_fingerprint = batch_result.run.schedule_fingerprint;
            diagnostics->search_merged_stream_fingerprint =
                batch_result.run.merged_stream_fingerprint;
            diagnostics->search_bitwise_deterministic = batch_result.run.bitwise_deterministic;
            diagnostics->search_profile = search_profile.snapshot();
            if (budget.deadline_reached()) break;
        }
        if (diagnostics->completed_batches == 0U) {
            diagnostics->deadline_expired = budget.deadline_expired();
            return RuntimeSearchOutcome::NoCleanBatch;
        }

        const auto* snapshot = session.round().clean_snapshot();
        if (snapshot == nullptr) return RuntimeSearchOutcome::NoCleanBatch;
        const auto& root_policy = snapshot->root_policy;
        policy->clear();
        policy->reserve(root_policy.actions.size());
        for (const auto& action : root_policy.actions) {
            policy->push_back({action.action, action.probability});
        }
        if (!normalize_multiway_resolver_policy(policy)) {
            return RuntimeSearchOutcome::NormalizationFailed;
        }
        diagnostics->deadline_expired = budget.deadline_expired() || budget.deadline_reached();
        diagnostics->search_profile = search_profile.snapshot();
        return RuntimeSearchOutcome::Completed;
    } catch (const std::exception&) {
        policy->clear();
        return RuntimeSearchOutcome::Exception;
    }
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

void MultiwayRuntimeSearchLimits::validate() const {
    solver.validate();
    memory.validate();
    if (deadline_reserve.count() < 0 || max_decision_depth == 0U ||
        max_decision_depth > MULTIWAY_MAX_DECISION_DEPTH ||
        max_public_chance_depth > MULTIWAY_MAX_PUBLIC_CHANCE_DEPTH ||
        active_min_seats < 2U || active_min_seats > active_max_seats ||
        active_max_seats > 6U || active_max_menu_actions == 0U ||
        active_max_menu_actions > MULTIWAY_MAX_ABSTRACTED_ACTIONS) {
        throw std::invalid_argument("multiway runtime search limits are invalid");
    }
}

bool MultiwayStableRootPolicyCache::find(
    const MultiwayModelIdentity& identity,
    std::uint64_t public_state_id,
    const std::vector<MultiwayActionDescriptor>& menu,
    std::vector<MultiwayResolverActionProbability>* policy) const {
    if (policy == nullptr) return false;
    std::lock_guard<std::mutex> lock(mutex_);
    if (entry_.identity != identity || entry_.public_state_id != public_state_id ||
        entry_.policy.size() != menu.size()) {
        return false;
    }
    for (std::size_t index = 0U; index < menu.size(); ++index) {
        if (entry_.policy[index].action != menu[index]) return false;
    }
    *policy = entry_.policy;
    return true;
}

void MultiwayStableRootPolicyCache::store(
    const MultiwayModelIdentity& identity,
    std::uint64_t public_state_id,
    std::vector<MultiwayResolverActionProbability> policy) {
    std::lock_guard<std::mutex> lock(mutex_);
    entry_.identity = identity;
    entry_.public_state_id = public_state_id;
    entry_.policy = std::move(policy);
}

void MultiwayResolverConfig::validate() const {
    action_abstraction.validate();
    runtime_limits.validate();
    if (!valid_search_mode(search_mode)) {
        throw std::invalid_argument("multiway resolver has an invalid search mode");
    }
    if (search_profile_mode != MultiwaySearchProfileMode::Disabled &&
        search_profile_mode != MultiwaySearchProfileMode::Checkpoints) {
        throw std::invalid_argument("multiway resolver has an invalid search profile mode");
    }
    const auto explicitly_requests_search =
        search_mode == MultiwayResolverSearchMode::SearchShadow ||
        search_mode == MultiwayResolverSearchMode::SearchActive;
    if (explicitly_requests_search || leaf_evaluator != nullptr) {
        if (!has_runtime_search_configuration(*this)) {
            throw std::invalid_argument("multiway resolver search configuration is invalid");
        }
    }
    if (full_blueprint != nullptr) full_blueprint->identity().validate();
    if (continuation_selector != nullptr && !continuation_selector->valid()) {
        throw std::invalid_argument("multiway resolver continuation selector is invalid");
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

std::unique_ptr<MultiwayDecisionSession> MultiwayResolver::begin_decision_session(
    const MultiwayResolverRequest& request) const {
    config_.validate();
    request.blueprint_identity.validate();
    if (request.public_state.id.value == 0U ||
        request.public_state.canonical_history_id == 0U || request.hero_seat < 0 ||
        !are_valid_and_distinct_cards(request.hero_cards.data(), request.hero_cards.size()) ||
        request.public_state.board.size() != board_count_for(request.public_state.betting.street) ||
        !are_valid_and_distinct_cards(request.public_state.board.data(), request.public_state.board.size()) ||
        contains_card(request.public_state.board, request.hero_cards[0]) ||
        contains_card(request.public_state.board, request.hero_cards[1])) {
        throw std::invalid_argument("multiway decision session request has invalid hero cards");
    }
    const auto state = MultiwayState::from_snapshot(request.public_state.betting);
    if (state.current_player() != request.hero_seat ||
        static_cast<std::size_t>(request.hero_seat) >= state.stacks().size() ||
        state.folded()[static_cast<std::size_t>(request.hero_seat)] || state.legal_actions().empty() ||
        !is_postflop(state.street()) ||
        config_.buckets == nullptr || config_.buckets->identity() != request.blueprint_identity) {
        throw std::invalid_argument("multiway decision session has incompatible public state or buckets");
    }
    MultiwayResolverDiagnostics diagnostics;
    validate_ranges(request, state, &diagnostics);
    auto board = request.public_state.board;
    std::sort(board.begin(), board.end());
    const auto menu = reconstruct_root_menu(request, state, config_);
    const auto bucket = config_.buckets->lookup(state.street(), board, request.hero_cards);
    auto limits = config_.runtime_limits.solver;
    if (limits.worker_count == 0U) limits.worker_count = 1U;
    if (limits.max_public_states == 0U) limits.max_public_states = 1'024U;
    if (limits.max_sparse_rows == 0U) limits.max_sparse_rows = 1'024U;
    if (limits.max_sparse_values == 0U) limits.max_sparse_values = 65'536U;
    if (limits.max_worker_delta_entries == 0U) limits.max_worker_delta_entries = 8'192U;
    limits.validate();
    const auto memory_preflight = preflight_multiway_memory(
        limits,
        config_.runtime_limits.memory,
        search_memory_inputs(request, state, menu.size(), config_, limits));
    if (memory_preflight.status == MultiwayMemoryStatus::Rejected) {
        throw std::length_error("multiway decision session memory preflight rejected the request");
    }
    MultiwayRootSnapshot root;
    if (!make_search_root(request, state, board, menu, bucket, &root)) {
        throw std::invalid_argument("multiway decision session could not construct a search root");
    }
    MultiwayCFRConfig cfr;
    cfr.player_count = static_cast<std::uint8_t>(root.seat_order.size());
    return std::make_unique<MultiwayDecisionSession>(
        MultiwaySolveRequest(std::move(root), cfr, limits),
        MultiwaySearchSessionDependencies{
            config_.buckets,
            config_.continuation_selector});
}

MultiwayResolverResult MultiwayResolver::resolve(const MultiwayResolverRequest& request) const {
    TEXASSOLVER_PROFILE_SCOPE("multiway.resolver.resolve");
    MultiwayResolverResult result;
    try {
        config_.validate();
        request.blueprint_identity.validate();
        result.diagnostics.artifact_identity = request.blueprint_identity;
        result.diagnostics.has_artifact_identity = true;
        if (request.public_state.id.value == 0U ||
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
            ? nullptr : &config_.verified_blueprint->snapshot;
        if (config_.verified_blueprint != nullptr &&
            config_.verified_blueprint->snapshot.identity != request.blueprint_identity) {
            throw std::invalid_argument("multiway resolver verified blueprint identity does not match request");
        }
        validate_ranges(request, state, &result.diagnostics);
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
                    bucket = config_.buckets->lookup(state.street(), canonical_board, request.hero_cards);
                } catch (const std::out_of_range&) {
                    result.diagnostics.status = MultiwayResolverStatus::BucketUnavailable;
                }
            }
        }
        result.diagnostics.root_bucket = bucket;

        const auto use_fallback = [&](MultiwayResolverStatus status) {
            result.diagnostics.status = status;
            if (config_.stable_root_cache != nullptr &&
                config_.stable_root_cache->find(
                    request.blueprint_identity, reconstructed_id, menu, &result.policy)) {
                set_policy_provenance(
                    &result.diagnostics, MultiwayPolicyProvenance::StableRootFallback);
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
            result.diagnostics.policy_normalized =
                normalize_multiway_resolver_policy(&result.policy);
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
        if (config_.full_blueprint != nullptr && config_.full_blueprint->identity() != request.blueprint_identity) {
            use_fallback(MultiwayResolverStatus::ArtifactMismatch);
            return result;
        }
        if (deadline_reached(request.deadline, config_.runtime_limits.deadline_reserve)) {
            result.diagnostics.deadline_expired = true;
            use_fallback(MultiwayResolverStatus::DeadlineFallback);
            return result;
        }

        if (config_.search_mode == MultiwayResolverSearchMode::ForcedFallback) {
            use_fallback(MultiwayResolverStatus::RejectedByBudget);
            return result;
        }

        const auto runtime_search_requested =
            config_.search_mode == MultiwayResolverSearchMode::SearchShadow ||
            config_.search_mode == MultiwayResolverSearchMode::SearchActive ||
            (config_.search_mode == MultiwayResolverSearchMode::ReleaseDefault &&
             has_release_search_configuration(config_));
        const auto runtime_search_delivers_policy =
            config_.search_mode == MultiwayResolverSearchMode::SearchActive ||
            config_.search_mode == MultiwayResolverSearchMode::ReleaseDefault;
        if (runtime_search_requested) {
            result.diagnostics.search_eligibility = search_eligibility(request, state, menu, config_);
            if (runtime_search_delivers_policy &&
                result.diagnostics.search_eligibility != MultiwayResolverSearchEligibility::Eligible) {
                use_fallback(MultiwayResolverStatus::RejectedByBudget);
                return result;
            }
        } else if (config_.search_mode == MultiwayResolverSearchMode::ReleaseDefault) {
            use_fallback(MultiwayResolverStatus::RejectedByBudget);
            return result;
        }

        std::vector<MultiwayResolverActionProbability> search_policy;
        if (result.diagnostics.search_eligibility == MultiwayResolverSearchEligibility::Eligible) {
            MultiwayResolverDiagnostics search_diagnostics = result.diagnostics;
            const auto search_outcome = run_search(
                request, state, config_, canonical_board, menu, bucket, reconstructed_id,
                &search_policy, &search_diagnostics);
            result.diagnostics.search_failure = search_failure(search_outcome);
            result.diagnostics.search_memory_status = search_diagnostics.search_memory_status;
            result.diagnostics.search_memory_stage = search_diagnostics.search_memory_stage;
            result.diagnostics.search_estimated_memory_bytes =
                search_diagnostics.search_estimated_memory_bytes;
            result.diagnostics.search_admitted_memory_bytes =
                search_diagnostics.search_admitted_memory_bytes;
            result.diagnostics.search_memory_degraded = search_diagnostics.search_memory_degraded;
            if (search_outcome == RuntimeSearchOutcome::Completed &&
                runtime_search_delivers_policy) {
                result.policy = std::move(search_policy);
                result.diagnostics = search_diagnostics;
                result.diagnostics.status = result.diagnostics.deadline_expired
                    ? MultiwayResolverStatus::Partial : MultiwayResolverStatus::Solved;
                result.diagnostics.search_engine = MultiwayResolverEngine::RootExternalSamplingMCCFR;
                result.diagnostics.search_engine_version = MULTIWAY_ROOT_SEARCH_RESOLVER_ENGINE_VERSION;
                set_policy_provenance(&result.diagnostics, MultiwayPolicyProvenance::RuntimeSearch);
                result.diagnostics.policy_normalized = true;
                sample_policy(&result, request.sampling_seed, reconstructed_id);
                if (config_.stable_root_cache != nullptr) {
                    config_.stable_root_cache->store(
                        request.blueprint_identity, reconstructed_id, result.policy);
                }
                return result;
            }
            if (runtime_search_delivers_policy) {
                const auto fallback_status = deadline_reached(request.deadline, config_.runtime_limits.deadline_reserve)
                    ? MultiwayResolverStatus::DeadlineFallback
                    : (search_outcome == RuntimeSearchOutcome::NoRoot ||
                       search_outcome == RuntimeSearchOutcome::MemoryRejected)
                        ? MultiwayResolverStatus::RejectedByBudget
                        : MultiwayResolverStatus::ResourceExhausted;
                use_fallback(fallback_status);
                return result;
            }
            if (search_outcome == RuntimeSearchOutcome::Completed) {
                result.diagnostics.shadow_search_completed = true;
                result.diagnostics.shadow_completed_batches = search_diagnostics.completed_batches;
                result.diagnostics.shadow_completed_trajectories =
                    search_diagnostics.completed_trajectories;
                result.diagnostics.shadow_search_merged_delta_entries =
                    search_diagnostics.search_merged_delta_entries;
                result.diagnostics.shadow_search_elapsed_nanoseconds =
                    search_diagnostics.search_elapsed_nanoseconds;
                result.diagnostics.shadow_search_observed_memory_bytes =
                    search_diagnostics.search_observed_memory_bytes;
            }
        }

        use_fallback(MultiwayResolverStatus::Solved);
        if (result.diagnostics.shadow_search_completed) {
            result.diagnostics.shadow_policy_l1_distance =
                policy_l1_distance(search_policy, result.policy);
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

}  // namespace texas::solver::multiway
