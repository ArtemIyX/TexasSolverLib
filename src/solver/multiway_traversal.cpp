#include "solver/multiway_traversal.hpp"
#include "core/fingerprint.hpp"
#include "games/multiway_fixed.hpp"
#include "solver/multiway_blueprint_policy_provider.hpp"
#include "solver/multiway_continuation_selector.hpp"
#include "solver/multiway_future_bucket.hpp"
#include "util/profiling.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstring>
#include <exception>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <utility>

namespace texas::solver::multiway {
namespace {

void hash_u64(std::uint64_t value, std::uint64_t& hash) noexcept {
    texas::core::fingerprint::append_u64(hash, value);
}

std::uint64_t private_range_identity(const MultiwayPrivateConfig& ranges) noexcept {
    std::uint64_t hash = 1469598103934665603ULL;
    hash_u64(ranges.board.size(), hash);
    for (const auto card : ranges.board) hash_u64(card, hash);
    hash_u64(ranges.ranges.size(), hash);
    for (const auto& seat : ranges.ranges) {
        hash_u64(seat.size(), hash);
        for (const auto& entry : seat) {
            hash_u64(entry.hole[0], hash);
            hash_u64(entry.hole[1], hash);
            std::uint64_t weight_bits = 0;
            static_assert(sizeof(weight_bits) == sizeof(entry.weight));
            std::memcpy(&weight_bits, &entry.weight, sizeof(weight_bits));
            hash_u64(weight_bits, hash);
        }
    }
    return hash == 0U ? 1U : hash;
}

std::uint64_t range_context_identity(
    std::uint64_t range_model_identity,
    const Probability* reaches,
    std::size_t count) noexcept {
    auto hash = range_model_identity;
    hash_u64(count, hash);
    for (std::size_t player = 0; player < count; ++player) {
        std::uint64_t reach_bits = 0;
        static_assert(sizeof(reach_bits) == sizeof(reaches[player]));
        std::memcpy(&reach_bits, reaches + player, sizeof(reach_bits));
        hash_u64(reach_bits, hash);
    }
    return hash == 0U ? 1U : hash;
}

std::uint64_t private_deal_identity(
    const MultiwayTerminalAdapter& terminal,
    const MultiwaySamplerDealToken& deal,
    std::size_t player_count) {
    std::uint64_t hash = 1469598103934665603ULL;
    hash_u64(player_count, hash);
    for (std::size_t player = 0; player < player_count; ++player) {
        auto hole = terminal.sampled_hole(deal, static_cast<PlayerId>(player));
        if (hole[1] < hole[0]) std::swap(hole[0], hole[1]);
        hash_u64(hole[0], hash);
        hash_u64(hole[1], hash);
    }
    return hash == 0U ? 1U : hash;
}

}  // namespace

MultiwayRootExternalSamplingTraversal::MultiwayRootExternalSamplingTraversal(
    MultiwaySolverCoordinator& coordinator,
    const MultiwayRootSnapshot& root,
    const MultiwayActionAbstraction& action_abstraction,
    const MultiwayBucketRegistry& buckets,
    const MultiwayLeafEvaluator* leaf_evaluator,
    std::uint32_t max_decision_depth,
    std::uint32_t max_public_chance_depth,
    const MultiwayBlueprintPolicyProvider* blueprint_policy,
    const MultiwayFixedContinuationSelector* continuation_selector,
    const MultiwayFutureBucketArtifact* future_bucket_artifact)
    : coordinator_(&coordinator),
      root_(&root),
      action_abstraction_(&action_abstraction),
      buckets_(&buckets),
      leaf_evaluator_(leaf_evaluator),
      max_decision_depth_(max_decision_depth),
      max_public_chance_depth_(max_public_chance_depth),
      blueprint_policy_(blueprint_policy),
      continuation_selector_(continuation_selector),
      future_bucket_artifact_(future_bucket_artifact),
      range_model_identity_(private_range_identity(root.private_ranges)),
      terminal_(coordinator) {
    root.validate();
    if (root.public_state.betting.street < Street::Flop ||
        root.public_state.betting.street > Street::River) {
        throw std::invalid_argument("multiway root traversal currently requires a postflop root");
    }
    if (max_decision_depth_ == 0U || max_decision_depth_ > MULTIWAY_MAX_DECISION_DEPTH) {
        throw std::invalid_argument("multiway traversal decision depth is outside the supported range");
    }
    if (max_public_chance_depth_ > MULTIWAY_MAX_PUBLIC_CHANCE_DEPTH) {
        throw std::invalid_argument("multiway traversal public chance depth is outside the supported range");
    }
    if (root.public_state.legal_actions.size() > MULTIWAY_MAX_TRAVERSAL_ACTIONS) {
        throw std::invalid_argument("multiway root action menu exceeds the compact traversal limit");
    }
}

struct MultiwayRootExternalSamplingTraversal::TraversalContext {
    const MultiwayTerminalAdapter* terminal = nullptr;
    const MultiwaySamplerDealToken* deal = nullptr;
    MultiwayWorkerDeltaStream* stream = nullptr;
    std::array<Probability, 6> player_reaches{};
    std::size_t player_count = 0;
    PlayerId traverser = -1;
    std::uint64_t trajectory_id = 0;
    std::uint64_t random_state = 0;
    Probability private_chance_reach = 0.0;
    Probability private_sampling_reach = 0.0;
    Probability public_chance_reach = 1.0;
    Probability public_sampling_reach = 1.0;
    double iteration_weight = 1.0;
    MultiwaySearchProfile* profile = nullptr;
    bool accepted = true;
};

namespace {

std::uint64_t next_random(std::uint64_t& state) noexcept {
    state += 0x9e3779b97f4a7c15ULL;
    auto value = state;
    value = (value ^ (value >> 30U)) * 0xbf58476d1ce4e5b9ULL;
    value = (value ^ (value >> 27U)) * 0x94d049bb133111ebULL;
    return value ^ (value >> 31U);
}

std::size_t sample_action(
    const Probability* strategy,
    std::size_t action_count,
    std::uint64_t& random_state) noexcept {
    const auto bits = next_random(random_state) >> 11U;
    const auto sample = static_cast<Probability>(bits) * (1.0 / 9007199254740992.0);
    Probability cumulative = 0.0;
    for (std::size_t action = 0; action + 1U < action_count; ++action) {
        cumulative += strategy[action];
        if (sample < cumulative) return action;
    }
    return action_count - 1U;
}

bool append_infoset_update_noalloc(
    MultiwayWorkerDeltaStream& stream,
    MultiwayInfosetId infoset,
    std::uint32_t bucket,
    std::uint64_t trajectory_id,
    const Probability* player_reaches,
    std::size_t player_count,
    PlayerId traverser,
    Probability chance_reach,
    Probability sampling_reach,
    const Probability* strategy,
    const Value* action_values,
    std::size_t action_count,
    double iteration_weight,
    Value& node_value) {
    if (infoset.public_state.value == 0U || infoset.seat != traverser ||
        player_reaches == nullptr || player_count < 2U || player_count > 6U ||
        traverser < 0 || static_cast<std::size_t>(traverser) >= player_count ||
        strategy == nullptr || action_values == nullptr || action_count == 0U ||
        !std::isfinite(chance_reach) || chance_reach < 0.0 || chance_reach > 1.0 ||
        !std::isfinite(sampling_reach) || sampling_reach <= 0.0 || sampling_reach > 1.0 ||
        !std::isfinite(iteration_weight) || iteration_weight <= 0.0) {
        throw std::invalid_argument("multiway allocation-free update has invalid inputs");
    }
    if (stream.capacity() - stream.size() < action_count) return false;

    Probability counterfactual_reach = chance_reach;
    for (std::size_t player = 0; player < player_count; ++player) {
        const auto reach = player_reaches[player];
        if (!std::isfinite(reach) || reach < 0.0 || reach > 1.0) {
            throw std::invalid_argument("multiway allocation-free update has an invalid player reach");
        }
        if (static_cast<PlayerId>(player) != traverser) counterfactual_reach *= reach;
    }
    const auto importance_weight = counterfactual_reach / sampling_reach;
    const auto average_strategy_weight =
        player_reaches[static_cast<std::size_t>(traverser)] / sampling_reach;
    if (!std::isfinite(importance_weight) || !std::isfinite(average_strategy_weight)) {
        throw std::overflow_error("multiway allocation-free importance weight is non-finite");
    }

    node_value = 0.0;
    Probability strategy_total = 0.0;
    for (std::size_t action = 0; action < action_count; ++action) {
        if (!std::isfinite(strategy[action]) || strategy[action] < 0.0 ||
            strategy[action] > 1.0 || !std::isfinite(action_values[action])) {
            throw std::invalid_argument("multiway allocation-free update has invalid action data");
        }
        strategy_total += strategy[action];
        node_value += strategy[action] * action_values[action];
    }
    if (std::fabs(strategy_total - 1.0) > 1e-12 || !std::isfinite(node_value)) {
        throw std::overflow_error("multiway allocation-free update has invalid normalization");
    }
    for (std::size_t action = 0; action < action_count; ++action) {
        const auto regret = importance_weight * (action_values[action] - node_value);
        const auto strategy_sum = average_strategy_weight * strategy[action] * iteration_weight;
        if (!std::isfinite(regret) || !std::isfinite(strategy_sum)) {
            throw std::overflow_error("multiway allocation-free update contains a non-finite delta");
        }
    }
    for (std::size_t action = 0; action < action_count; ++action) {
        const auto appended = stream.try_append({
            infoset,
            bucket,
            static_cast<std::uint8_t>(action),
            importance_weight * (action_values[action] - node_value),
            average_strategy_weight * strategy[action] * iteration_weight,
            trajectory_id,
        });
        if (!appended) {
            throw std::logic_error("multiway delta capacity changed during allocation-free append");
        }
    }
    return true;
}

}  // namespace

Value MultiwayRootExternalSamplingTraversal::evaluate_leaf(
    const MultiwayPublicStateDescriptor& state,
    const TraversalContext& context) const {
    if (leaf_evaluator_ == nullptr || !leaf_evaluator_->valid()) {
        throw std::logic_error("multiway recursive traversal requires a leaf evaluator at its boundary");
    }
    const auto actor = state.betting.current_player >= 0 ? state.betting.current_player : context.traverser;
    const auto hole = context.terminal->sampled_hole(*context.deal, actor);
    const auto bucket = future_bucket_artifact_ != nullptr
        ? future_bucket_artifact_->lookup(state.betting.street, state.board, hole)
        : buckets_->lookup(state.betting.street, state.board, hole);
    const MultiwayContinuationSelectionKey continuation_key = {
        state.id, actor, state.betting.street, bucket,
        root_->action_abstraction_version, root_->leaf_model_version,
    };
    MultiwaySearchProfileScope profile_scope(
        context.profile, MultiwaySearchProfileStage::ContinuationLeaf);
    const MultiwayLeafEvaluationRequest request = {
        &state.betting,
        &state.board,
        context.traverser,
        state.id,
        actor,
        bucket,
        root_->action_abstraction_version,
        root_->leaf_model_version,
        MultiwayContinuationPolicyKind::Blueprint,
        context.deal,
        context.terminal,
        context.player_reaches.data(),
        context.player_count,
        range_context_identity(
            range_model_identity_, context.player_reaches.data(), context.player_count),
        private_deal_identity(*context.terminal, *context.deal, context.player_count),
    };
    if (continuation_selector_ == nullptr) {
        const auto value = (*leaf_evaluator_)(request);
        if (!std::isfinite(value)) {
            throw std::logic_error("multiway leaf evaluator returned a non-finite value");
        }
        return value;
    }
    const auto mixture = continuation_selector_->strategy(continuation_key);
    std::array<Value, MULTIWAY_FIXED_CONTINUATION_POLICIES.size()> values{};
    Value value = 0.0;
    for (std::size_t index = 0U; index < values.size(); ++index) {
        auto policy_request = request;
        policy_request.continuation_policy = MULTIWAY_FIXED_CONTINUATION_POLICIES[index];
        values[index] = (*leaf_evaluator_)(policy_request);
        if (!std::isfinite(values[index])) {
            throw std::logic_error("multiway leaf evaluator returned a non-finite value");
        }
        value += mixture[index] * values[index];
    }
    continuation_selector_->update_regrets(continuation_key, mixture, values);
    if (!std::isfinite(value)) {
        throw std::logic_error("multiway leaf evaluator returned a non-finite value");
    }
    return value;
}

Value MultiwayRootExternalSamplingTraversal::traverse_decision(
    const MultiwayPublicStateDescriptor& state,
    std::uint32_t decision_depth,
    std::uint32_t public_chance_depth,
    TraversalContext& context) const {
    if (!context.accepted) return 0.0;
    const auto actor = state.betting.current_player;
    if (actor < 0 || state.legal_actions.empty()) {
        throw std::logic_error("multiway recursive traversal requires a decision state");
    }
    const auto action_count = state.legal_actions.size();
    if (action_count > MULTIWAY_MAX_TRAVERSAL_ACTIONS) {
        throw std::length_error("multiway traversal action menu exceeds the compact traversal limit");
    }
    const MultiwayBucketTable* table_ptr = nullptr;
    std::uint32_t bucket = 0U;
    {
        MultiwaySearchProfileScope profile_scope(
            context.profile, MultiwaySearchProfileStage::RowLookup);
        table_ptr = &buckets_->table(state.betting.street, state.board);
        const auto same_root_street = state.betting.street == root_->public_state.betting.street;
        bucket = root_->root_uses_exact_private_hand && same_root_street
            ? static_cast<std::uint32_t>(MultiwayBucketTable::hole_index(
                context.terminal->sampled_hole(*context.deal, actor)))
            : (state.id == root_->public_state.id
                ? root_->root_bucket
                : table_ptr->lookup(context.terminal->sampled_hole(*context.deal, actor)));
    }
    const auto& table = *table_ptr;
    const MultiwayInfosetId infoset = {state.id, actor};
    {
        MultiwaySearchProfileScope profile_scope(
            context.profile, MultiwaySearchProfileStage::PublicGraphAdmission);
        coordinator_->admit_infoset_row({
            infoset,
            root_->root_uses_exact_private_hand &&
                state.betting.street == root_->public_state.betting.street
                ? static_cast<std::uint32_t>(MULTIWAY_HOLE_COMBINATION_COUNT)
                : table.bucket_count(),
            static_cast<std::uint8_t>(action_count),
        });
    }
    std::array<Probability, MULTIWAY_MAX_TRAVERSAL_ACTIONS> strategy{};
    const auto lookup = actor == context.traverser || blueprint_policy_ == nullptr
        ? MultiwayBlueprintLookupStatus::Missing
        : blueprint_policy_->strategy_into(
            infoset, bucket, state.legal_actions.data(), action_count, strategy.data());
    if (lookup != MultiwayBlueprintLookupStatus::Hit) {
        MultiwaySearchProfileScope profile_scope(
            context.profile, MultiwaySearchProfileStage::RegretMatching);
        coordinator_->regret_matched_strategy_into(
            infoset, bucket, strategy.data(), action_count);
    }
    const auto betting_state = make_multiway_fixed_state(state.betting);

    const auto evaluate_action = [&](std::size_t action) {
        const auto next = betting_state.apply(
            state.legal_actions[action].action,
            state.legal_actions[action].target_street_contribution);
        std::array<MultiwayActionDescriptor, MULTIWAY_MAX_TRAVERSAL_ACTIONS> child_actions{};
        std::size_t child_action_count = 0U;
        if (next.current_player >= 0) {
            MultiwaySearchProfileScope profile_scope(
                context.profile, MultiwaySearchProfileStage::ActionMenuGeneration);
            const auto generated_actions = action_abstraction_->make_legal_actions(
                make_multiway_betting_snapshot(next));
            if (generated_actions.size() > MULTIWAY_MAX_TRAVERSAL_ACTIONS) {
                throw std::length_error(
                    "multiway generated action menu exceeds the compact traversal limit");
            }
            child_action_count = generated_actions.size();
            std::copy(generated_actions.begin(), generated_actions.end(), child_actions.begin());
        }
        MultiwayPublicStateDescriptor child;
        {
            MultiwaySearchProfileScope profile_scope(
                context.profile, MultiwaySearchProfileStage::PublicGraphAdmission);
            child = MultiwayPublicBuilder::make_action_child(
                state,
                static_cast<std::uint32_t>(action),
                make_multiway_betting_snapshot(next),
                child_actions.data(),
                child_action_count);
            coordinator_->admit_public_state(child);
        }
        if (next.next_node_kind() == MultiwayNextNodeKind::FoldTerminal ||
            next.next_node_kind() == MultiwayNextNodeKind::ShowdownTerminal) {
            MultiwaySearchProfileScope profile_scope(
                context.profile, MultiwaySearchProfileStage::TerminalSettlement);
            return context.terminal->resolve_admitted_terminal(child, *context.deal)
                .utilities[static_cast<std::size_t>(context.traverser)];
        }
        const auto next_depth = decision_depth + 1U;
        if (next.current_player >= 0 && next.street == state.betting.street &&
            next_depth < max_decision_depth_) {
            return traverse_decision(child, next_depth, public_chance_depth, context);
        }
        if (next.next_node_kind() == MultiwayNextNodeKind::BoardRunout) {
            return traverse_public_chance(child, next_depth, public_chance_depth, context);
        }
        if (next.next_node_kind() == MultiwayNextNodeKind::StreetTransition &&
            public_chance_depth < max_public_chance_depth_) {
            return traverse_public_chance(child, next_depth, public_chance_depth, context);
        }
        return evaluate_leaf(child, context);
    };

    if (actor != context.traverser) {
        const auto action = sample_action(strategy.data(), action_count, context.random_state);
        const auto probability = strategy[action];
        if (probability <= 0.0) {
            throw std::logic_error("multiway traversal sampled a zero-probability action");
        }
        auto& reach = context.player_reaches[static_cast<std::size_t>(actor)];
        const auto saved_reach = reach;
        const auto saved_sampling = context.public_sampling_reach;
        reach *= probability;
        context.public_sampling_reach *= probability;
        const auto value = evaluate_action(action);
        reach = saved_reach;
        context.public_sampling_reach = saved_sampling;
        return value;
    }

    std::array<Value, MULTIWAY_MAX_TRAVERSAL_ACTIONS> action_values{};
    auto& traverser_reach = context.player_reaches[static_cast<std::size_t>(actor)];
    const auto saved_reach = traverser_reach;
    for (std::size_t action = 0; action < action_count; ++action) {
        traverser_reach = saved_reach * strategy[action];
        action_values[action] = evaluate_action(action);
        if (!context.accepted) break;
    }
    traverser_reach = saved_reach;
    if (!context.accepted) return 0.0;

    const auto sampling_reach = context.private_sampling_reach * context.public_sampling_reach;
    if (!std::isfinite(sampling_reach) || sampling_reach <= 0.0) {
        throw std::overflow_error("multiway traversal sampling reach is non-finite");
    }
    Value node_value = 0.0;
    context.accepted = append_infoset_update_noalloc(
        *context.stream,
        infoset,
        bucket,
        context.trajectory_id,
        context.player_reaches.data(),
        context.player_count,
        actor,
        context.private_chance_reach * context.public_chance_reach,
        sampling_reach,
        strategy.data(),
        action_values.data(),
        action_count,
        context.iteration_weight,
        node_value);
    if (!context.accepted) return 0.0;
    return node_value;
}

Value MultiwayRootExternalSamplingTraversal::traverse_public_chance(
    const MultiwayPublicStateDescriptor& state,
    std::uint32_t decision_depth,
    std::uint32_t public_chance_depth,
    TraversalContext& context) const {
    MultiwaySampledPublicBoardChance sampled;
    {
        MultiwaySearchProfileScope profile_scope(
            context.profile, MultiwaySearchProfileStage::PublicChanceSampling);
        sampled = context.terminal->sample_admitted_public_board_chance(
            state, *context.deal, context.random_state);
    }
    MultiwayPublicStateDescriptor chance_child;
    {
        MultiwaySearchProfileScope profile_scope(
            context.profile, MultiwaySearchProfileStage::PublicGraphAdmission);
        chance_child = MultiwayPublicBuilder::make_board_chance_child(
            state, sampled, {});
        coordinator_->admit_public_state(chance_child);
    }

    const auto saved_chance_reach = context.public_chance_reach;
    const auto saved_sampling_reach = context.public_sampling_reach;
    context.public_chance_reach *= sampled.probability;
    context.public_sampling_reach *= sampled.probability;
    const auto next_chance_depth = public_chance_depth + 1U;

    Value value = 0.0;
    if (chance_child.board_runout.chance_only_runout) {
        if (chance_child.board.size() == 5U) {
            MultiwaySearchProfileScope profile_scope(
                context.profile, MultiwaySearchProfileStage::TerminalSettlement);
            value = context.terminal->resolve_admitted_terminal(chance_child, *context.deal)
                .utilities[static_cast<std::size_t>(context.traverser)];
        } else {
            value = traverse_public_chance(
                chance_child, decision_depth, next_chance_depth, context);
        }
    } else {
        const auto transition =
            context.terminal->apply_admitted_public_street_transition(chance_child);
        auto next_actions = [&] {
            MultiwaySearchProfileScope profile_scope(
                context.profile, MultiwaySearchProfileStage::ActionMenuGeneration);
            return action_abstraction_->make_legal_actions(
                transition.transition.betting);
        }();
        if (next_actions.size() > MULTIWAY_MAX_TRAVERSAL_ACTIONS) {
            throw std::length_error(
                "multiway generated action menu exceeds the compact traversal limit");
        }
        const auto transition_child = [&] {
            MultiwaySearchProfileScope profile_scope(
                context.profile, MultiwaySearchProfileStage::PublicGraphAdmission);
            auto child = MultiwayPublicBuilder::make_street_transition_child(
                chance_child, transition, std::move(next_actions));
            coordinator_->admit_public_state(child);
            return child;
        }();
        if (decision_depth < max_decision_depth_) {
            value = traverse_decision(
                transition_child, decision_depth, next_chance_depth, context);
        } else {
            value = evaluate_leaf(transition_child, context);
        }
    }

    context.public_chance_reach = saved_chance_reach;
    context.public_sampling_reach = saved_sampling_reach;
    return value;
}

bool MultiwayRootExternalSamplingTraversal::run(
    PlayerId traverser,
    std::uint64_t trajectory_id,
    std::uint64_t seed,
    MultiwayWorkerDeltaStream& stream,
    double iteration_weight,
    MultiwaySearchProfile* profile) const {
    const auto& root_state = root_->public_state;
    if (std::find(root_->seat_order.begin(), root_->seat_order.end(), traverser) ==
            root_->seat_order.end() ||
        root_state.legal_actions.empty()) {
        throw std::invalid_argument("multiway root traversal requires a valid root seat traverser");
    }
    const auto deal = [&] {
        MultiwaySearchProfileScope profile_scope(
            profile, MultiwaySearchProfileStage::PrivateDealSampling);
        return terminal_.sample_private_deal(seed);
    }();
    const auto sampled_reach = terminal_.sampled_reach(deal);
    TraversalContext context;
    context.terminal = &terminal_;
    context.deal = &deal;
    context.stream = &stream;
    context.player_reaches.fill(1.0);
    context.player_count = root_state.betting.stacks.size();
    context.traverser = traverser;
    context.trajectory_id = trajectory_id;
    context.random_state = seed;
    context.private_chance_reach = sampled_reach.chance_reach;
    context.private_sampling_reach = sampled_reach.proposal_reach;
    context.iteration_weight = iteration_weight;
    context.profile = profile;
    const auto initial_size = stream.size();
    (void)traverse_decision(root_state, 0U, 0U, context);
    if (!context.accepted) stream.rewind(initial_size);
    return context.accepted;
}

MultiwayRootBatchRunner::MultiwayRootBatchRunner(
    MultiwayRootExternalSamplingTraversal traversal,
    MultiwaySolverCoordinator& coordinator,
    std::uint32_t worker_count,
    std::size_t worker_delta_capacity,
    MultiwaySearchProfileMode profile_mode)
    : traversal_(std::move(traversal)),
      coordinator_(&coordinator),
      worker_count_(worker_count),
      worker_delta_capacity_(worker_delta_capacity),
      profile_mode_(profile_mode) {
    if (worker_count_ == 0U || worker_delta_capacity_ == 0U) {
        throw std::invalid_argument("multiway root batch runner requires positive worker limits");
    }
    if (worker_count_ != coordinator.limits().worker_count ||
        worker_delta_capacity_ > coordinator.limits().max_worker_delta_entries) {
        throw std::invalid_argument("multiway root batch runner limits must fit its coordinator");
    }
    worker_scratch_.reserve(worker_count_);
    worker_stream_views_.reserve(worker_count_);
    worker_batches_.resize(worker_count_);
    threads_.reserve(worker_count_);
    for (std::uint32_t worker = 0; worker < worker_count_; ++worker) {
        worker_scratch_.emplace_back(worker, worker_delta_capacity_);
        worker_stream_views_.push_back(&worker_scratch_.back().stream);
        threads_.emplace_back(&MultiwayRootBatchRunner::worker_loop, this, worker);
    }
}

MultiwayRootBatchRunner::~MultiwayRootBatchRunner() {
    {
        std::lock_guard<std::mutex> lock(pool_mutex_);
        stop_workers_ = true;
    }
    work_cv_.notify_all();
    for (auto& thread : threads_) {
        if (thread.joinable()) thread.join();
    }
}

void MultiwayRootBatchRunner::worker_loop(std::size_t worker_index) {
    std::uint64_t observed_generation = 0U;
    while (true) {
        std::size_t batch_count = 0U;
        std::uint64_t first_trajectory_id = 0U;
        std::uint64_t seed = 0U;
        double iteration_weight = 1.0;
        {
            std::unique_lock<std::mutex> lock(pool_mutex_);
            work_cv_.wait(lock, [this, observed_generation] {
                return stop_workers_ || (batch_active_ && batch_generation_ != observed_generation);
            });
            if (stop_workers_) return;
            observed_generation = batch_generation_;
            batch_count = active_batch_count_;
            first_trajectory_id = active_first_trajectory_id_;
            seed = active_seed_;
            iteration_weight = active_iteration_weight_;
        }

        try {
            if (worker_index < batch_count) {
                const auto& batch = worker_batches_[worker_index];
                auto& scratch = worker_scratch_[worker_index];
                for (auto local_id = batch.trajectories.begin;
                     local_id < batch.trajectories.end;
                     ++local_id) {
                    if (cancelled_.load(std::memory_order_acquire)) break;
                    const auto trajectory_id = first_trajectory_id + local_id;
                    ++scratch.attempted;
                    if (traversal_.run(
                            traversal_.traverser_for_trajectory(trajectory_id),
                            trajectory_id,
                            multiway_deterministic_trajectory_seed(seed, trajectory_id),
                            scratch.stream,
                            iteration_weight,
                            &scratch.profile)) {
                        ++scratch.accepted;
                    } else {
                        ++scratch.discarded;
                    }
                }
                scratch.stream.sort_fixed_order();
            }
        } catch (...) {
            cancelled_.store(true, std::memory_order_release);
            std::lock_guard<std::mutex> lock(pool_mutex_);
            if (worker_error_ == nullptr) worker_error_ = std::current_exception();
        }

        {
            std::lock_guard<std::mutex> lock(pool_mutex_);
            ++completed_workers_;
            if (completed_workers_ == worker_count_) completion_cv_.notify_one();
        }
    }
}

MultiwayRootBatchResult MultiwayRootBatchRunner::run(
    std::uint64_t first_trajectory_id,
    std::uint64_t trajectory_count,
    std::uint64_t seed,
    double iteration_weight) {
    TEXASSOLVER_PROFILE_SCOPE("multiway.traversal.root_batch");
    if (!std::isfinite(iteration_weight) || iteration_weight <= 0.0) {
        throw std::invalid_argument("multiway batch iteration weight must be finite and positive");
    }
    const auto batch_count = MultiwayScheduler::partition_deterministic_into(
        trajectory_count,
        worker_count_,
        worker_batches_.data(),
        worker_batches_.size());
    for (auto& scratch : worker_scratch_) scratch.reset(profile_mode_);

    {
        std::lock_guard<std::mutex> lock(pool_mutex_);
        if (batch_active_) {
            throw std::logic_error("multiway root batch runner does not support concurrent batches");
        }
        batch_active_ = true;
        ++batch_generation_;
        completed_workers_ = 0U;
        active_batch_count_ = batch_count;
        active_first_trajectory_id_ = first_trajectory_id;
        active_seed_ = seed;
        active_iteration_weight_ = iteration_weight;
        worker_error_ = nullptr;
        cancelled_.store(false, std::memory_order_release);
    }
    work_cv_.notify_all();
    {
        std::unique_lock<std::mutex> lock(pool_mutex_);
        completion_cv_.wait(lock, [this] { return completed_workers_ == worker_count_; });
        batch_active_ = false;
        if (worker_error_ != nullptr) std::rethrow_exception(worker_error_);
    }

    MultiwayRootBatchResult result;
    result.run.worker_count = worker_count_;
    result.run.base_seed = seed;
    result.run.first_trajectory_id = first_trajectory_id;
    result.run.trajectory_count = trajectory_count;
    result.run.schedule_fingerprint = multiway_deterministic_schedule_fingerprint(
        worker_count_, seed, first_trajectory_id, trajectory_count);
    MultiwaySearchProfile batch_profile(profile_mode_);
    result.minimum_worker_trajectories = std::numeric_limits<std::uint64_t>::max();
    for (const auto& scratch : worker_scratch_) {
        result.trajectories_attempted += scratch.attempted;
        result.trajectories_accepted += scratch.accepted;
        result.trajectories_discarded += scratch.discarded;
        result.delta_entries_merged += scratch.stream.size();
        result.minimum_worker_trajectories = std::min(result.minimum_worker_trajectories, scratch.attempted);
        result.maximum_worker_trajectories = std::max(result.maximum_worker_trajectories, scratch.attempted);
        batch_profile.merge(scratch.profile.snapshot());
    }
    if (result.minimum_worker_trajectories == std::numeric_limits<std::uint64_t>::max()) {
        result.minimum_worker_trajectories = 0U;
    }
    {
        MultiwaySearchProfileScope profile_scope(
            &batch_profile, MultiwaySearchProfileStage::DeltaMerge);
        coordinator_->merge_worker_streams(worker_stream_views_);
    }
    result.run.merged_stream_fingerprint =
        coordinator_->diagnostics().last_merged_stream_fingerprint;
    result.profile = batch_profile.snapshot();
    result.clean = true;
    return result;
}

}  // namespace texas::solver::multiway
