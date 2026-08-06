#include "solver/multiway_traversal.hpp"
#include "util/thread_join_guard.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <exception>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <utility>

namespace core {

bool MultiwayExternalSamplingTraversal::append_infoset_update(
    MultiwayWorkerDeltaStream& stream,
    MultiwayInfosetId infoset,
    std::uint32_t bucket,
    std::uint64_t trajectory_id,
    const MultiwayExternalSamplingRequest& request,
    double iteration_weight) {
    if (!std::isfinite(iteration_weight) || iteration_weight <= 0.0) {
        throw std::invalid_argument("multiway iteration weight must be finite and positive");
    }
    if (infoset.public_state.value == 0U || infoset.seat != request.traverser) {
        throw std::invalid_argument("multiway traversal update infoset must identify the traverser");
    }
    const auto update = make_multiway_external_sampling_cfr_update(request);
    if (update.regret_deltas.size() != update.strategy_deltas.size()) {
        throw std::logic_error("multiway traversal produced mismatched action deltas");
    }
    if (stream.capacity() - stream.size() < update.regret_deltas.size()) return false;
    for (std::size_t action = 0; action < update.regret_deltas.size(); ++action) {
        if (!stream.try_append({
                infoset,
                bucket,
                static_cast<std::uint8_t>(action),
                update.regret_deltas[action],
                update.strategy_deltas[action] * iteration_weight,
                trajectory_id,
            })) {
            throw std::logic_error("multiway traversal delta capacity changed during append");
        }
    }
    return true;
}

MultiwayRootExternalSamplingTraversal::MultiwayRootExternalSamplingTraversal(
    MultiwaySolverCoordinator& coordinator,
    const MultiwayRootSnapshot& root,
    const MultiwayActionAbstraction& action_abstraction,
    const MultiwayBucketRegistry& buckets,
    const MultiwayLeafEvaluator* leaf_evaluator,
    std::uint32_t max_decision_depth,
    std::uint32_t max_public_chance_depth)
    : coordinator_(&coordinator),
      root_(&root),
      action_abstraction_(&action_abstraction),
      buckets_(&buckets),
      leaf_evaluator_(leaf_evaluator),
      max_decision_depth_(max_decision_depth),
      max_public_chance_depth_(max_public_chance_depth),
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
    PlayerId traverser) const {
    if (leaf_evaluator_ == nullptr || !leaf_evaluator_->valid()) {
        throw std::logic_error("multiway recursive traversal requires a leaf evaluator at its boundary");
    }
    const auto value = (*leaf_evaluator_)({&state.betting, &state.board, traverser});
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
    const auto& table = buckets_->table(state.betting.street, state.board);
    const auto bucket = table.lookup(context.terminal->sampled_hole(*context.deal, actor));
    const MultiwayInfosetId infoset = {state.id, actor};
    coordinator_->admit_infoset_row({
        infoset,
        table.bucket_count(),
        static_cast<std::uint8_t>(action_count),
    });
    std::array<Probability, MULTIWAY_MAX_TRAVERSAL_ACTIONS> strategy{};
    coordinator_->regret_matched_strategy_into(
        infoset, bucket, strategy.data(), action_count);
    const auto betting_state = MultiwayState::from_snapshot(state.betting);

    const auto evaluate_action = [&](std::size_t action) {
        const auto next = betting_state.apply(
            state.legal_actions[action].action,
            state.legal_actions[action].target_street_contribution);
        std::vector<MultiwayActionDescriptor> child_actions;
        if (next.current_player() >= 0) {
            child_actions = action_abstraction_->make_legal_actions(
                next.snapshot(), root_->action_menu_id());
            if (child_actions.size() > MULTIWAY_MAX_TRAVERSAL_ACTIONS) {
                throw std::length_error(
                    "multiway generated action menu exceeds the compact traversal limit");
            }
        }
        const auto child = MultiwayPublicBuilder::make_action_child(
            state, static_cast<std::uint32_t>(action), std::move(child_actions));
        coordinator_->admit_public_state(child);
        if (next.is_terminal()) {
            return context.terminal->resolve_admitted_terminal(child, *context.deal)
                .utilities[static_cast<std::size_t>(context.traverser)];
        }
        const auto next_depth = decision_depth + 1U;
        if (next.current_player() >= 0 && next.street() == state.betting.street &&
            next_depth < max_decision_depth_) {
            return traverse_decision(child, next_depth, public_chance_depth, context);
        }
        if (next.requires_board_runout()) {
            return traverse_public_chance(child, next_depth, public_chance_depth, context);
        }
        if (next.requires_street_transition() &&
            public_chance_depth < max_public_chance_depth_) {
            return traverse_public_chance(child, next_depth, public_chance_depth, context);
        }
        return evaluate_leaf(child, context.traverser);
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
    const auto sampled = context.terminal->sample_admitted_public_board_chance(
        state, *context.deal, context.random_state);
    const auto chance_child = MultiwayPublicBuilder::make_board_chance_child(
        state, sampled, {});
    coordinator_->admit_public_state(chance_child);

    const auto saved_chance_reach = context.public_chance_reach;
    const auto saved_sampling_reach = context.public_sampling_reach;
    context.public_chance_reach *= sampled.probability;
    context.public_sampling_reach *= sampled.probability;
    const auto next_chance_depth = public_chance_depth + 1U;

    Value value = 0.0;
    if (chance_child.board_runout.chance_only_runout) {
        if (chance_child.board.size() == 5U) {
            value = context.terminal->resolve_admitted_terminal(chance_child, *context.deal)
                .utilities[static_cast<std::size_t>(context.traverser)];
        } else {
            value = traverse_public_chance(
                chance_child, decision_depth, next_chance_depth, context);
        }
    } else {
        const auto transition =
            context.terminal->apply_admitted_public_street_transition(chance_child);
        auto next_actions = action_abstraction_->make_legal_actions(
            transition.transition.betting, root_->action_menu_id());
        if (next_actions.size() > MULTIWAY_MAX_TRAVERSAL_ACTIONS) {
            throw std::length_error(
                "multiway generated action menu exceeds the compact traversal limit");
        }
        const auto transition_child = MultiwayPublicBuilder::make_street_transition_child(
            chance_child, transition, std::move(next_actions));
        coordinator_->admit_public_state(transition_child);
        if (decision_depth < max_decision_depth_) {
            value = traverse_decision(
                transition_child, decision_depth, next_chance_depth, context);
        } else {
            value = evaluate_leaf(transition_child, context.traverser);
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
    double iteration_weight) const {
    const auto& root_state = root_->public_state;
    if (std::find(root_->seat_order.begin(), root_->seat_order.end(), traverser) ==
            root_->seat_order.end() ||
        root_state.legal_actions.empty()) {
        throw std::invalid_argument("multiway root traversal requires a valid root seat traverser");
    }
    const auto deal = terminal_.sample_private_deal(seed);
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
    const auto initial_size = stream.size();
    (void)traverse_decision(root_state, 0U, 0U, context);
    if (!context.accepted) stream.rewind(initial_size);
    return context.accepted;
}

namespace {

std::uint64_t mix_seed(std::uint64_t seed, std::uint64_t trajectory_id) noexcept {
    auto value = seed + 0x9e3779b97f4a7c15ULL + trajectory_id;
    value = (value ^ (value >> 30U)) * 0xbf58476d1ce4e5b9ULL;
    value = (value ^ (value >> 27U)) * 0x94d049bb133111ebULL;
    return value ^ (value >> 31U);
}

}  // namespace

MultiwayRootBatchRunner::MultiwayRootBatchRunner(
    MultiwayRootExternalSamplingTraversal traversal,
    MultiwaySolverCoordinator& coordinator,
    std::uint32_t worker_count,
    std::size_t worker_delta_capacity)
    : traversal_(std::move(traversal)),
      coordinator_(&coordinator),
      worker_count_(worker_count),
      worker_delta_capacity_(worker_delta_capacity) {
    if (worker_count_ == 0U || worker_delta_capacity_ == 0U) {
        throw std::invalid_argument("multiway root batch runner requires positive worker limits");
    }
    if (worker_count_ != coordinator.limits().worker_count ||
        worker_delta_capacity_ > coordinator.limits().max_worker_delta_entries) {
        throw std::invalid_argument("multiway root batch runner limits must fit its coordinator");
    }
    worker_scratch_.reserve(worker_count_);
    worker_stream_views_.reserve(worker_count_);
    for (std::uint32_t worker = 0; worker < worker_count_; ++worker) {
        worker_scratch_.emplace_back(worker, worker_delta_capacity_);
        worker_stream_views_.push_back(&worker_scratch_.back().stream);
    }
}

void MultiwayRootBatchRunner::set_test_worker_failure_for_testing(
    std::int32_t worker_index) noexcept {
    test_worker_failure_index_ = worker_index;
}

MultiwayRootBatchResult MultiwayRootBatchRunner::run(
    std::uint64_t first_trajectory_id,
    std::uint64_t trajectory_count,
    std::uint64_t seed,
    double iteration_weight) {
    if (!std::isfinite(iteration_weight) || iteration_weight <= 0.0) {
        throw std::invalid_argument("multiway batch iteration weight must be finite and positive");
    }
    const auto batches = MultiwayScheduler::partition_deterministic(trajectory_count, worker_count_);
    for (auto& scratch : worker_scratch_) scratch.reset();

    std::atomic<bool> cancelled{false};
    std::exception_ptr worker_error;
    std::mutex worker_error_mutex;
    const auto execute_worker = [&](std::size_t worker_index) {
        try {
            if (cancelled.load(std::memory_order_acquire)) return;
            if (test_worker_failure_index_ == static_cast<std::int32_t>(worker_index)) {
                throw std::runtime_error("injected multiway root batch worker failure");
            }
            const auto& batch = batches[worker_index];
            auto& scratch = worker_scratch_[worker_index];
            for (auto local_id = batch.trajectories.begin;
                 local_id < batch.trajectories.end;
                 ++local_id) {
                if (cancelled.load(std::memory_order_acquire)) return;
                const auto trajectory_id = first_trajectory_id + local_id;
                ++scratch.attempted;
                if (traversal_.run(
                        traversal_.traverser_for_trajectory(trajectory_id),
                        trajectory_id,
                        mix_seed(seed, trajectory_id),
                        scratch.stream,
                        iteration_weight)) {
                    ++scratch.accepted;
                } else {
                    ++scratch.discarded;
                }
            }
            scratch.stream.sort_fixed_order();
        } catch (...) {
            cancelled.store(true, std::memory_order_release);
            std::lock_guard<std::mutex> lock(worker_error_mutex);
            if (worker_error == nullptr) worker_error = std::current_exception();
        }
    };

    std::vector<std::thread> threads;
    threads.reserve(batches.size() > 0U ? batches.size() - 1U : 0U);
    auto thread_guard = detail::make_thread_join_guard(
        threads,
        [&cancelled] { cancelled.store(true, std::memory_order_release); });
    for (std::size_t worker = 1U; worker < batches.size(); ++worker) {
        threads.emplace_back(execute_worker, worker);
    }
    if (!batches.empty()) execute_worker(0U);
    for (auto& thread : threads) thread.join();
    thread_guard.release();
    if (worker_error != nullptr) std::rethrow_exception(worker_error);

    MultiwayRootBatchResult result;
    for (const auto& scratch : worker_scratch_) {
        result.trajectories_attempted += scratch.attempted;
        result.trajectories_accepted += scratch.accepted;
        result.trajectories_discarded += scratch.discarded;
        result.delta_entries_merged += scratch.stream.size();
    }
    coordinator_->merge_worker_streams(worker_stream_views_);
    return result;
}

}  // namespace core
