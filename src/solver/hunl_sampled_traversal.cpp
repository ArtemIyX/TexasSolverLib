#include "solver/hunl_sampled_traversal.hpp"

#include "util/pcs.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <stdexcept>

namespace core {

namespace {

constexpr std::size_t kMaxDecisionActions = 16;

struct TraversalReach {
    std::array<double, 2> player = {1.0, 1.0};
    double chance = 1.0;
    double sampling = 1.0;
};

std::uint32_t sampled_edge_id(const HUNLSampledNode& node, std::size_t edge_slot) {
    if (edge_slot >= static_cast<std::size_t>(node.edge_count)) {
        throw std::out_of_range("sampled edge slot is out of range");
    }
    const auto slot = static_cast<std::uint32_t>(edge_slot);
    if (slot > std::numeric_limits<std::uint32_t>::max() - node.edge_begin) {
        throw std::overflow_error("sampled edge id overflow");
    }
    return node.edge_begin + slot;
}

void record_sampled_edge(HUNLSampledTraversalResult& result, std::size_t edge_slot) noexcept {
    if (result.sampled_edge_slot_count >= result.sampled_edge_slots.size()) {
        return;
    }
    result.sampled_edge_slots[result.sampled_edge_slot_count++] =
        static_cast<std::uint16_t>(edge_slot);
}

std::pair<std::size_t, double> sample_chance_edge(
    const HUNLSampledBuilder& builder,
    const HUNLSampledNode& node,
    PcsRng& rng) {
    double total = 0.0;
    for (std::size_t edge_slot = 0; edge_slot < node.edge_count; ++edge_slot) {
        const auto probability = builder.edge(sampled_edge_id(node, edge_slot)).probability;
        if (!std::isfinite(probability) || probability < 0.0) {
            throw std::logic_error("sampled chance edge has invalid probability");
        }
        total += probability;
    }
    if (!(total > 0.0) || !std::isfinite(total)) {
        throw std::logic_error("sampled chance node must have positive finite probability mass");
    }

    auto draw = rng.next_unit_f64() * total;
    for (std::size_t edge_slot = 0; edge_slot < node.edge_count; ++edge_slot) {
        const auto probability = builder.edge(sampled_edge_id(node, edge_slot)).probability;
        if (probability <= 0.0) {
            continue;
        }
        if (draw < probability) {
            return {edge_slot, probability / total};
        }
        draw -= probability;
    }

    for (std::size_t edge_slot = node.edge_count; edge_slot > 0; --edge_slot) {
        const auto probability = builder.edge(sampled_edge_id(node, edge_slot - 1U)).probability;
        if (probability > 0.0) {
            return {edge_slot - 1U, probability / total};
        }
    }
    throw std::logic_error("sampled chance node has no selectable edge");
}

std::pair<std::size_t, double> sample_strategy_action(
    const std::array<float, kMaxDecisionActions>& strategy,
    std::size_t action_count,
    PcsRng& rng) {
    auto draw = rng.next_unit_f64();
    for (std::size_t action = 0; action < action_count; ++action) {
        const auto probability = static_cast<double>(strategy[action]);
        if (draw < probability) {
            return {action, probability};
        }
        draw -= probability;
    }
    for (std::size_t action = action_count; action > 0; --action) {
        if (strategy[action - 1U] > 0.0f) {
            return {action - 1U, static_cast<double>(strategy[action - 1U])};
        }
    }
    throw std::logic_error("sampled strategy has no selectable action");
}

void append_delta(
    HUNLSampledWorkerScratch& scratch,
    std::uint64_t trajectory_id,
    InfosetId infoset_id,
    std::uint32_t bucket,
    std::size_t action,
    double regret,
    double strategy_sum) {
    if (scratch.deltas.size() >= scratch.deltas.capacity()) {
        throw std::runtime_error("HUNLSampledWorkerScratch delta capacity exhausted");
    }
    scratch.deltas.push_back(HUNLSampledValueDelta{
        trajectory_id,
        infoset_id,
        bucket,
        static_cast<std::uint8_t>(action),
        regret,
        strategy_sum,
    });
}

double traverse_external(
    const HUNLSampledBuilder& builder,
    const HUNLSampledStorage& storage,
    const HUNLSampledTerminalEvaluator& terminal_evaluator,
    const HUNLSampledTraversalRequest& request,
    HUNLSampledWorkerScratch& scratch,
    PcsRng& rng,
    std::uint32_t node_id,
    TraversalReach reach,
    HUNLSampledTraversalResult& result) {
    const auto node = builder.node(node_id);
    ++result.nodes_visited;

    if (node.type == HUNLFlatNodeType::TerminalFold ||
        node.type == HUNLFlatNodeType::TerminalShowdown ||
        node.type == HUNLFlatNodeType::DepthLimited) {
        if (request.private_hole.has_value()) {
            const auto private_state = builder.state(node_id).clone_with_hole_cards(*request.private_hole);
            return private_state.utility()[static_cast<std::size_t>(request.traversing_player)];
        }
        HUNLSampledTerminalInput input;
        input.contributions = node.contributions;
        input.traversing_player = request.traversing_player;
        return terminal_evaluator.evaluate_terminal(input, node.terminal_utility);
    }

    if (!node.expanded) {
        throw HUNLSampledTraversalPreparationRequired(node_id);
    }
    const auto expanded = node;
    if (expanded.edge_count == 0) {
        throw std::logic_error("sampled non-terminal node has no edges");
    }

    if (expanded.type == HUNLFlatNodeType::Chance) {
        const auto [edge_slot, probability] = sample_chance_edge(builder, expanded, rng);
        record_sampled_edge(result, edge_slot);
        ++result.chance_nodes_sampled;
        reach.chance *= probability;
        reach.sampling *= probability;
        return traverse_external(
            builder,
            storage,
            terminal_evaluator,
            request,
            scratch,
            request.trajectory_id,
            rng,
            builder.edge(sampled_edge_id(expanded, edge_slot)).child,
            reach,
            result);
    }

    if (expanded.player < 0 || expanded.player > 1) {
        throw std::logic_error("sampled decision node has invalid player");
    }
    const auto action_count = static_cast<std::size_t>(expanded.edge_count);
    if (action_count > kMaxDecisionActions) {
        throw std::logic_error("sampled decision node exceeds scalar action capacity");
    }

    if (!storage.has_row(expanded.infoset_id)) {
        throw HUNLSampledTraversalPreparationRequired(node_id);
    }
    const auto row = storage.view(expanded.infoset_id);
    std::array<float, kMaxDecisionActions> strategy = {};
    HUNLSampledStorage::compute_current_strategy(row, request.bucket, strategy.data());

    const auto acting_player = static_cast<std::size_t>(expanded.player);
    const auto strategy_weight = reach.sampling > 0.0
        ? reach.player[acting_player] / reach.sampling
        : 0.0;

    if (expanded.player != request.traversing_player) {
        const auto [action, probability] = sample_strategy_action(strategy, action_count, rng);
        record_sampled_edge(result, action);
        ++result.opponent_nodes_sampled;
        for (std::size_t candidate = 0; candidate < action_count; ++candidate) {
            append_delta(
                scratch,
                expanded.infoset_id,
                request.bucket,
                candidate,
                0.0,
                strategy_weight * static_cast<double>(strategy[candidate]));
        }
        ++result.infosets_updated;
        reach.player[acting_player] *= probability;
        reach.sampling *= probability;
        return traverse_external(
            builder,
            storage,
            terminal_evaluator,
            request,
            scratch,
            rng,
            builder.edge(sampled_edge_id(expanded, action)).child,
            reach,
            result);
    }

    std::array<double, kMaxDecisionActions> action_values = {};
    double node_value = 0.0;
    for (std::size_t action = 0; action < action_count; ++action) {
        auto child_reach = reach;
        child_reach.player[acting_player] *= static_cast<double>(strategy[action]);
        action_values[action] = traverse_external(
            builder,
            storage,
            terminal_evaluator,
            request,
            scratch,
            rng,
            builder.edge(sampled_edge_id(expanded, action)).child,
            child_reach,
            result);
        node_value += static_cast<double>(strategy[action]) * action_values[action];
    }

    const auto opponent = 1U - acting_player;
    const auto counterfactual_reach = reach.chance * reach.player[opponent];
    const auto importance_weight = reach.sampling > 0.0
        ? counterfactual_reach / reach.sampling
        : 0.0;
    for (std::size_t action = 0; action < action_count; ++action) {
        append_delta(
            scratch,
            expanded.infoset_id,
            request.bucket,
            action,
            importance_weight * (action_values[action] - node_value),
            strategy_weight * static_cast<double>(strategy[action]));
    }
    ++result.infosets_updated;
    return node_value;
}

void merge_deltas(HUNLSampledStorage& storage, const HUNLSampledWorkerScratch& scratch) {
    for (const auto& delta : scratch.deltas) {
        const auto row = storage.view_mut(delta.infoset_id);
        if (row.empty() || delta.bucket >= row.bucket_count || delta.action >= row.action_count) {
            throw std::logic_error("sampled traversal delta does not match its infoset row");
        }
        const auto offset = HUNLSampledStorage::value_index(
            row.layout,
            row.bucket_count,
            row.action_count,
            delta.bucket,
            delta.action);
        row.regret[offset] += static_cast<float>(delta.regret);
        row.strategy_sum[offset] += static_cast<float>(delta.strategy_sum);
    }
}

}  // namespace

void HUNLSampledWorkerScratch::clear_keep_capacity() noexcept {
    action_values.clear();
    strategy.clear();
    deltas.clear();
}

void HUNLSampledWorkerScratch::reserve_deltas(std::size_t count) {
    deltas.reserve(count);
}

HUNLSampledTraversal::HUNLSampledTraversal(
    HUNLSampledBuilder& builder,
    HUNLSampledStorage& storage,
    const HUNLSampledTerminalEvaluator& terminal_evaluator)
    : builder_(builder), storage_(storage), terminal_evaluator_(terminal_evaluator) {
}

HUNLSampledTraversalResult HUNLSampledTraversal::run(
    const HUNLSampledTraversalRequest& request,
    HUNLSampledWorkerScratch& scratch) {
    prepare_hunl_sampled_trajectory(builder_, storage_, terminal_evaluator_, request);
    const auto result = run_unmerged(request, scratch);
    merge_deltas(storage_, scratch);
    return result;
}

HUNLSampledTraversalResult HUNLSampledTraversal::run_unmerged(
    const HUNLSampledTraversalRequest& request,
    HUNLSampledWorkerScratch& scratch) const {
    scratch.clear_keep_capacity();
    if (builder_.node_count() == 0 || request.root_node_id >= builder_.node_count()) {
        return {};
    }
    if (request.traversing_player < 0 || request.traversing_player > 1) {
        throw std::invalid_argument("sampled traversal requires traversing_player 0 or 1");
    }
    if (request.bucket_count == 0 || request.bucket >= request.bucket_count) {
        throw std::invalid_argument("sampled traversal bucket is out of range");
    }
    if (scratch.deltas.capacity() < request.delta_capacity_hint) {
        scratch.reserve_deltas(request.delta_capacity_hint);
    }

    HUNLSampledTraversalResult result;
    PcsRng rng(PcsRng::mix_seed(
        request.seed,
        request.trajectory_id,
        request.iteration,
        static_cast<std::uint64_t>(request.traversing_player)));
    result.value = traverse_external(
        builder_,
        storage_,
        terminal_evaluator_,
        request,
        scratch,
        rng,
        request.root_node_id,
        TraversalReach{},
        result);
    return result;
}

void merge_hunl_sampled_worker_deltas(
    HUNLSampledStorage& storage,
    HUNLSampledWorkerScratch& scratch) {
    std::sort(scratch.deltas.begin(), scratch.deltas.end(), [](const auto& lhs, const auto& rhs) {
        if (lhs.infoset_id.value != rhs.infoset_id.value) return lhs.infoset_id.value < rhs.infoset_id.value;
        if (lhs.bucket != rhs.bucket) return lhs.bucket < rhs.bucket;
        if (lhs.action != rhs.action) return lhs.action < rhs.action;
        return lhs.trajectory_id < rhs.trajectory_id;
    });
    merge_deltas(storage, scratch);
}

void prepare_hunl_sampled_trajectory(
    HUNLSampledBuilder& builder,
    HUNLSampledStorage& storage,
    const HUNLSampledTerminalEvaluator& terminal_evaluator,
    const HUNLSampledTraversalRequest& request) {
    HUNLSampledTraversal traversal(builder, storage, terminal_evaluator);
    HUNLSampledWorkerScratch scratch;
    scratch.reserve_deltas(request.delta_capacity_hint);
    for (std::size_t attempts = 0; attempts < 1'000'000U; ++attempts) {
        try {
            (void)traversal.run_unmerged(request, scratch);
            return;
        } catch (const HUNLSampledTraversalPreparationRequired& needed) {
            const auto& node = builder.node(needed.node_id());
            if (!node.expanded) {
                builder.ensure_expanded(needed.node_id());
                continue;
            }
            if (node.type == HUNLFlatNodeType::Decision && !storage.has_row(node.infoset_id)) {
                storage.ensure_row({
                    node.infoset_id,
                    node.player,
                    node.street,
                    request.bucket_count,
                    static_cast<std::uint8_t>(node.edge_count),
                });
                continue;
            }
            throw;
        }
    }
    throw std::runtime_error("sampled traversal coordinator preparation exceeded limit");
}

}  // namespace core
