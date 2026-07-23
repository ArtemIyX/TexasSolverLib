#include "solver/hunl_sampled_traversal.hpp"

#include "util/pcs.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
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
    if (!std::isfinite(regret) || !std::isfinite(strategy_sum)) {
        throw std::overflow_error("sampled traversal produced a non-finite delta");
    }
    if (scratch.deltas.size() >= scratch.deltas.capacity()) {
        throw std::runtime_error("HUNLSampledWorkerScratch delta capacity exhausted");
    }
    scratch.deltas.push_back(HUNLSampledValueDelta{
        infoset_id,
        bucket,
        static_cast<std::uint8_t>(action),
        regret,
        strategy_sum,
        trajectory_id,
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
    if (!std::isfinite(reach.sampling) || reach.sampling <= 0.0 ||
        !std::isfinite(reach.player[acting_player])) {
        throw std::underflow_error("sampled traversal sampling reach underflowed or became non-finite");
    }
    const auto strategy_weight = reach.player[acting_player] / reach.sampling;
    if (!std::isfinite(strategy_weight)) {
        throw std::overflow_error("sampled traversal strategy importance weight is non-finite");
    }

    if (expanded.player != request.traversing_player) {
        const auto [action, probability] = sample_strategy_action(strategy, action_count, rng);
        record_sampled_edge(result, action);
        ++result.opponent_nodes_sampled;
        for (std::size_t candidate = 0; candidate < action_count; ++candidate) {
            append_delta(
                scratch,
                request.trajectory_id,
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
    if (!std::isfinite(counterfactual_reach) || counterfactual_reach <= 0.0) {
        throw std::underflow_error("sampled traversal counterfactual reach underflowed or became non-finite");
    }
    const auto importance_weight = counterfactual_reach / reach.sampling;
    if (!std::isfinite(importance_weight)) {
        throw std::overflow_error("sampled traversal regret importance weight is non-finite");
    }
    for (std::size_t action = 0; action < action_count; ++action) {
        append_delta(
            scratch,
            request.trajectory_id,
            expanded.infoset_id,
            request.bucket,
            action,
            importance_weight * (action_values[action] - node_value),
            strategy_weight * static_cast<double>(strategy[action]));
    }
    ++result.infosets_updated;
    return node_value;
}

bool delta_cell_less(
    const HUNLSampledValueDelta& lhs,
    const HUNLSampledValueDelta& rhs) noexcept {
    if (lhs.infoset_id.value != rhs.infoset_id.value) {
        return lhs.infoset_id.value < rhs.infoset_id.value;
    }
    if (lhs.bucket != rhs.bucket) return lhs.bucket < rhs.bucket;
    return lhs.action < rhs.action;
}

bool same_delta_cell(
    const HUNLSampledValueDelta& lhs,
    const HUNLSampledValueDelta& rhs) noexcept {
    return lhs.infoset_id.value == rhs.infoset_id.value &&
           lhs.bucket == rhs.bucket &&
           lhs.action == rhs.action;
}

bool delta_order_less(
    const HUNLSampledValueDelta& lhs,
    const HUNLSampledValueDelta& rhs) noexcept {
    if (delta_cell_less(lhs, rhs)) return true;
    if (delta_cell_less(rhs, lhs)) return false;
    return lhs.trajectory_id < rhs.trajectory_id;
}

template <class Visitor>
void visit_merged_delta_cells(
    HUNLSampledWorkerScratch* streams,
    std::size_t stream_count,
    Visitor&& visitor) {
    for (std::size_t stream = 0; stream < stream_count; ++stream) {
        streams[stream].merge_cursor = 0;
    }

    while (true) {
        const HUNLSampledValueDelta* cell = nullptr;
        for (std::size_t stream = 0; stream < stream_count; ++stream) {
            const auto cursor = streams[stream].merge_cursor;
            if (cursor >= streams[stream].deltas.size()) continue;
            const auto& candidate = streams[stream].deltas[cursor];
            if (cell == nullptr || delta_cell_less(candidate, *cell)) {
                cell = &candidate;
            }
        }
        if (cell == nullptr) return;
        const auto cell_key = *cell;

        double regret_delta = 0.0;
        double strategy_delta = 0.0;
        while (true) {
            std::size_t selected_stream = stream_count;
            const HUNLSampledValueDelta* selected = nullptr;
            for (std::size_t stream = 0; stream < stream_count; ++stream) {
                const auto cursor = streams[stream].merge_cursor;
                if (cursor >= streams[stream].deltas.size()) continue;
                const auto& candidate = streams[stream].deltas[cursor];
                if (!same_delta_cell(candidate, cell_key)) continue;
                if (selected == nullptr || delta_order_less(candidate, *selected) ||
                    (!delta_order_less(*selected, candidate) && stream < selected_stream)) {
                    selected = &candidate;
                    selected_stream = stream;
                }
            }
            if (selected == nullptr) break;
            if (!std::isfinite(selected->regret) ||
                !std::isfinite(selected->strategy_sum) ||
                !std::isfinite(regret_delta + selected->regret) ||
                !std::isfinite(strategy_delta + selected->strategy_sum)) {
                throw std::overflow_error("sampled merge contains a non-finite delta");
            }
            regret_delta += selected->regret;
            strategy_delta += selected->strategy_sum;
            ++streams[selected_stream].merge_cursor;
        }
        visitor(cell_key, regret_delta, strategy_delta);
    }
}

void merge_delta_streams(
    HUNLSampledStorage& storage,
    HUNLSampledWorkerScratch* streams,
    std::size_t stream_count) {
    for (std::size_t stream = 0; stream < stream_count; ++stream) {
        std::sort(
            streams[stream].deltas.begin(),
            streams[stream].deltas.end(),
            delta_order_less);
    }

    // Validate the complete batch before changing the first central value.
    visit_merged_delta_cells(
        streams,
        stream_count,
        [&storage](
            const HUNLSampledValueDelta& cell,
            double regret_delta,
            double strategy_delta) {
            const auto row = storage.view(cell.infoset_id);
            if (row.empty() || cell.bucket >= row.bucket_count ||
                cell.action >= row.action_count) {
                throw std::logic_error(
                    "sampled traversal delta does not match its infoset row");
            }
            const auto offset = HUNLSampledStorage::value_index(
                row.layout,
                row.bucket_count,
                row.action_count,
                cell.bucket,
                cell.action);
            const auto next_regret =
                static_cast<double>(row.regret[offset]) + regret_delta;
            const auto next_strategy =
                static_cast<double>(row.strategy_sum[offset]) + strategy_delta;
            if (!std::isfinite(next_regret) || !std::isfinite(next_strategy) ||
                std::fabs(next_regret) > std::numeric_limits<float>::max() ||
                std::fabs(next_strategy) > std::numeric_limits<float>::max()) {
                throw std::overflow_error(
                    "sampled merge would produce a non-finite float row");
            }
        });

    visit_merged_delta_cells(
        streams,
        stream_count,
        [&storage](
            const HUNLSampledValueDelta& cell,
            double regret_delta,
            double strategy_delta) {
            const auto row = storage.view_mut(cell.infoset_id);
            const auto offset = HUNLSampledStorage::value_index(
                row.layout,
                row.bucket_count,
                row.action_count,
                cell.bucket,
                cell.action);
            row.regret[offset] = static_cast<float>(
                static_cast<double>(row.regret[offset]) + regret_delta);
            row.strategy_sum[offset] = static_cast<float>(
                static_cast<double>(row.strategy_sum[offset]) + strategy_delta);
        });
}

}  // namespace

void HUNLSampledWorkerScratch::clear_keep_capacity() noexcept {
    action_values.clear();
    strategy.clear();
    deltas.clear();
    merge_cursor = 0;
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
    merge_delta_streams(storage_, &scratch, 1U);
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
    merge_delta_streams(storage, &scratch, 1U);
}

void merge_hunl_sampled_worker_streams(
    HUNLSampledStorage& storage,
    std::vector<HUNLSampledWorkerScratch>& streams) {
    merge_delta_streams(storage, streams.data(), streams.size());
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
