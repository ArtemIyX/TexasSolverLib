#include "solver/hunl_flat_state.hpp"

#include <algorithm>
#include <stdexcept>

namespace core {

namespace {

template <class T, class Allocator>
std::uint64_t vector_storage_bytes(const std::vector<T, Allocator>& values) {
    return static_cast<std::uint64_t>(values.capacity()) * sizeof(T);
}

std::size_t max_depth_slice_width(const HUNLFlatSolveGraph& graph) {
    std::size_t max_width = 0;
    for (const auto& slice : graph.depth_slices) {
        max_width = std::max<std::size_t>(max_width, slice.count);
    }
    return max_width;
}

std::vector<HUNLFlatRange> partition_range(std::uint32_t total_count, std::size_t worker_count) {
    const auto workers = std::max<std::size_t>(1, worker_count);
    std::vector<HUNLFlatRange> ranges;
    ranges.reserve(workers);

    const auto base = total_count / static_cast<std::uint32_t>(workers);
    const auto remainder = total_count % static_cast<std::uint32_t>(workers);
    std::uint32_t begin = 0;
    for (std::size_t i = 0; i < workers; ++i) {
        const auto width = base + (i < remainder ? 1U : 0U);
        ranges.push_back(HUNLFlatRange{begin, begin + width});
        begin += width;
    }
    return ranges;
}

void validate_infoset_table_inputs(
    const HUNLFlatSolveGraph& graph,
    const std::array<std::size_t, 2>& bucket_count_per_player,
    const HUNLFlatBucketMap* bucket_map) {
    for (std::size_t player = 0; player < bucket_count_per_player.size(); ++player) {
        if (bucket_count_per_player[player] == 0 && bucket_map == nullptr) {
            throw std::invalid_argument("HUNLFlatInfosetTable bucket_count_per_player must be positive");
        }
    }
    if (bucket_map != nullptr && graph.infosets.size() > 0 && bucket_map->empty()) {
        throw std::invalid_argument("HUNLFlatInfosetTable bucket_map must not be empty when graph has infosets");
    }
}

std::vector<HUNLFlatRange> partition_depth_slice_by_cost(
    const HUNLFlatSolveGraph& graph,
    const HUNLFlatInfosetTable& infoset_table,
    HUNLFlatSlice slice,
    std::size_t worker_count,
    std::vector<std::uint64_t>* out_costs = nullptr) {
    const auto workers = std::max<std::size_t>(1, worker_count);
    std::vector<HUNLFlatRange> ranges;
    ranges.reserve(workers);
    if (out_costs != nullptr) {
        out_costs->clear();
        out_costs->reserve(workers);
    }

    if (slice.count == 0) {
        for (std::size_t worker = 0; worker < workers; ++worker) {
            ranges.push_back(HUNLFlatRange{slice.begin, slice.begin});
            if (out_costs != nullptr) {
                out_costs->push_back(0);
            }
        }
        return ranges;
    }

    std::vector<std::uint32_t> costs;
    costs.reserve(slice.count);
    std::uint64_t remaining_cost = 0;
    for (std::uint32_t offset = 0; offset < slice.count; ++offset) {
        const auto node_idx = graph.depth_order[slice.begin + offset];
        const auto cost = HUNLFlatParallelPlan::estimated_backward_cost(graph.node_meta[node_idx], infoset_table);
        costs.push_back(cost);
        remaining_cost += cost;
    }

    std::uint32_t cursor = slice.begin;
    std::uint32_t local_cursor = 0;
    for (std::size_t worker = 0; worker < workers; ++worker) {
        const auto remaining_workers = workers - worker;
        if (remaining_workers == 1) {
            ranges.push_back(HUNLFlatRange{cursor, slice.begin + slice.count});
            if (out_costs != nullptr) {
                out_costs->push_back(remaining_cost);
            }
            break;
        }

        const auto target_cost =
            static_cast<std::uint64_t>((remaining_cost + remaining_workers - 1) / remaining_workers);
        std::uint64_t assigned_cost = 0;
        const auto range_begin = cursor;
        while (local_cursor < slice.count) {
            const auto nodes_left = slice.count - local_cursor;
            if (nodes_left == remaining_workers - 1 && cursor > range_begin) {
                break;
            }

            const auto next_cost = static_cast<std::uint64_t>(costs[local_cursor]);
            if (assigned_cost > 0 && assigned_cost + next_cost > target_cost) {
                const auto overshoot = assigned_cost + next_cost - target_cost;
                const auto undershoot = target_cost - assigned_cost;
                if (overshoot > undershoot) {
                    break;
                }
            }

            assigned_cost += next_cost;
            ++local_cursor;
            ++cursor;
        }

        if (cursor == range_begin && local_cursor < slice.count) {
            assigned_cost += costs[local_cursor];
            ++local_cursor;
            ++cursor;
        }

        ranges.push_back(HUNLFlatRange{range_begin, cursor});
        if (out_costs != nullptr) {
            out_costs->push_back(assigned_cost);
        }
        remaining_cost -= assigned_cost;
    }

    while (ranges.size() < workers) {
        ranges.push_back(HUNLFlatRange{slice.begin + slice.count, slice.begin + slice.count});
        if (out_costs != nullptr) {
            out_costs->push_back(0);
        }
    }

    return ranges;
}

}  // namespace

std::uint64_t HUNLFlatMemoryEstimate::total_bytes() const noexcept {
    return graph_bytes +
        infoset_table_bytes +
        solver_buffers_bytes +
        worker_scratch_bytes +
        parallel_plan_bytes +
        auxiliary_bytes;
}

HUNLFlatMemoryEstimate estimate_memory(
    const HUNLFlatSolveGraph& graph,
    const HUNLFlatInfosetTable& infoset_table,
    std::size_t worker_count,
    const HUNLFlatMemoryEstimateOptions& options) {
    HUNLFlatMemoryEstimate estimate;
    const auto workers = std::max<std::size_t>(1, worker_count);

    estimate.graph_bytes += sizeof(HUNLFlatSolveGraph);
    estimate.graph_bytes += vector_storage_bytes(graph.node_meta);
    estimate.graph_bytes += vector_storage_bytes(graph.children);
    estimate.graph_bytes += vector_storage_bytes(graph.actions);
    estimate.graph_bytes += vector_storage_bytes(graph.chance_outcomes);
    estimate.graph_bytes += vector_storage_bytes(graph.infosets);
    estimate.graph_bytes += vector_storage_bytes(graph.infoset_debug_keys);
    estimate.graph_bytes += vector_storage_bytes(graph.infoset_nodes);
    estimate.graph_bytes += vector_storage_bytes(graph.forward_order);
    estimate.graph_bytes += vector_storage_bytes(graph.reverse_order);
    estimate.graph_bytes += vector_storage_bytes(graph.node_depths);
    estimate.graph_bytes += vector_storage_bytes(graph.street_order);
    estimate.graph_bytes += vector_storage_bytes(graph.depth_slices);
    estimate.graph_bytes += vector_storage_bytes(graph.depth_order);
    estimate.graph_bytes += vector_storage_bytes(graph.depth_worker_ranges);
    estimate.graph_bytes += vector_storage_bytes(graph.terminal_nodes);
    estimate.graph_bytes += vector_storage_bytes(graph.terminal_node_values);
    estimate.graph_bytes += vector_storage_bytes(graph.fold_terminal_nodes);
    estimate.graph_bytes += vector_storage_bytes(graph.fold_terminal_values);
    estimate.graph_bytes += vector_storage_bytes(graph.showdown_terminal_nodes);
    estimate.graph_bytes += vector_storage_bytes(graph.showdown_terminal_values);
    for (const auto& key : graph.infoset_debug_keys) {
        estimate.graph_bytes += static_cast<std::uint64_t>(key.capacity()) * sizeof(char);
    }
    for (const auto& ranges : graph.depth_worker_ranges) {
        estimate.graph_bytes += vector_storage_bytes(ranges);
    }

    estimate.infoset_table_bytes += sizeof(HUNLFlatInfosetTable);
    estimate.infoset_table_bytes += vector_storage_bytes(infoset_table.meta());
    estimate.infoset_table_bytes +=
        static_cast<std::uint64_t>(infoset_table.total_value_count()) * sizeof(double) * 3ULL;

    if (options.include_solver_buffers) {
        const auto node_count = static_cast<std::uint64_t>(graph.node_count());
        const auto edge_count = static_cast<std::uint64_t>(graph.children.size());
        const auto bucket_count = static_cast<std::uint64_t>(infoset_table.total_bucket_count());
        const auto infoset_count = static_cast<std::uint64_t>(graph.infosets.size());
        estimate.solver_buffers_bytes += node_count * sizeof(double) * 5ULL;
        estimate.solver_buffers_bytes += edge_count * sizeof(double);
        estimate.solver_buffers_bytes += bucket_count * sizeof(double) * 2ULL;
        estimate.solver_buffers_bytes += infoset_count * sizeof(double);
    }

    if (options.include_worker_scratch) {
        const auto depth_width = static_cast<std::uint64_t>(max_depth_slice_width(graph));
        const auto bucket_count = static_cast<std::uint64_t>(infoset_table.total_bucket_count());
        const auto max_child_count = static_cast<std::uint64_t>(options.max_child_count);
        const auto max_bucket_count = static_cast<std::uint64_t>(options.max_bucket_count);
        std::uint64_t per_worker_bytes = 0;
        per_worker_bytes += sizeof(HUNLFlatWorkerScratch);
        per_worker_bytes += depth_width * sizeof(double) * 3ULL;
        per_worker_bytes += bucket_count * sizeof(double);
        per_worker_bytes += max_child_count * sizeof(double) * 2ULL;
        per_worker_bytes += max_bucket_count * sizeof(double);
        per_worker_bytes += depth_width * sizeof(std::uint32_t);
        per_worker_bytes += bucket_count * sizeof(std::uint32_t);
        per_worker_bytes += depth_width * sizeof(std::uint32_t) * 2ULL;
        estimate.worker_scratch_bytes += per_worker_bytes * static_cast<std::uint64_t>(workers);
    }

    if (options.include_parallel_plan) {
        const auto depth_count = static_cast<std::uint64_t>(graph.depth_slices.size());
        estimate.parallel_plan_bytes += sizeof(HUNLFlatParallelPlan);
        estimate.parallel_plan_bytes +=
            static_cast<std::uint64_t>(workers) * sizeof(HUNLFlatWorkerAssignment);
        estimate.parallel_plan_bytes +=
            static_cast<std::uint64_t>(workers) * depth_count * sizeof(HUNLFlatRange) * 2ULL;
        estimate.parallel_plan_bytes +=
            static_cast<std::uint64_t>(workers) * depth_count * sizeof(std::uint64_t);
    }

    estimate.auxiliary_bytes += options.auxiliary_bytes;
    return estimate;
}

void HUNLFlatWorkerScratch::reset_values() noexcept {
    std::fill(player0_reach.begin(), player0_reach.end(), 0.0);
    std::fill(player1_reach.begin(), player1_reach.end(), 0.0);
    std::fill(chance_reach.begin(), chance_reach.end(), 0.0);
    std::fill(bucket_reach.begin(), bucket_reach.end(), 0.0);
    dirty_nodes.clear();
    dirty_buckets.clear();
    next_depth_local_offsets.clear();
}

void HUNLFlatWorkerScratch::ensure_capacity(
    std::size_t node_count,
    std::size_t edge_count) {
    ensure_capacity(node_count, edge_count, 0, 0, 0);
}

void HUNLFlatWorkerScratch::ensure_capacity(
    std::size_t node_count,
    std::size_t edge_count,
    std::size_t total_bucket_count) {
    ensure_capacity(node_count, edge_count, total_bucket_count, 0, 0);
}

void HUNLFlatWorkerScratch::ensure_capacity(
    std::size_t node_count,
    std::size_t edge_count,
    std::size_t total_bucket_count,
    std::size_t max_child_count,
    std::size_t max_bucket_count) {
    (void)edge_count;
    player0_reach.assign(node_count, 0.0);
    player1_reach.assign(node_count, 0.0);
    chance_reach.assign(node_count, 0.0);
    bucket_reach.assign(total_bucket_count, 0.0);
    row_values.assign(max_child_count, 0.0);
    row_weights.assign(max_child_count, 0.0);
    local_bucket_mass.assign(max_bucket_count, 0.0);
    dirty_nodes.clear();
    dirty_nodes.reserve(node_count);
    dirty_buckets.clear();
    dirty_buckets.reserve(total_bucket_count);
    next_depth_local_offsets.clear();
    next_depth_local_offsets.reserve(node_count);
}

HUNLFlatParallelPlan HUNLFlatParallelPlan::build(const HUNLFlatSolveGraph& graph, std::size_t worker_count) {
    HUNLFlatInfosetTable empty_table;
    (void)empty_table;
    HUNLFlatParallelPlan plan;
    const auto workers = std::max<std::size_t>(1, worker_count);
    const auto infoset_ranges = partition_range(static_cast<std::uint32_t>(graph.infosets.size()), workers);
    const auto node_ranges = partition_range(static_cast<std::uint32_t>(graph.node_count()), workers);

    plan.workers.reserve(workers);
    for (std::size_t worker_index = 0; worker_index < workers; ++worker_index) {
        HUNLFlatWorkerAssignment assignment;
        assignment.worker_index = static_cast<std::uint32_t>(worker_index);
        assignment.infoset_range = infoset_ranges[worker_index];
        assignment.bucket_range = {};
        assignment.value_range = {};
        assignment.node_range = node_ranges[worker_index];
        assignment.depth_node_ranges.reserve(graph.depth_slices.size());
        assignment.depth_reduce_ranges.reserve(graph.depth_slices.size());
        assignment.depth_backward_costs.reserve(graph.depth_slices.size());

        for (const auto& slice : graph.depth_slices) {
            const auto count = slice.count;
            const auto base = count / static_cast<std::uint32_t>(workers);
            const auto remainder = count % static_cast<std::uint32_t>(workers);
            const auto relative_begin =
                static_cast<std::uint32_t>(worker_index) * base +
                std::min<std::uint32_t>(static_cast<std::uint32_t>(worker_index), remainder);
            const auto width = base + (worker_index < remainder ? 1U : 0U);
            assignment.depth_node_ranges.push_back(HUNLFlatRange{
                slice.begin + relative_begin,
                slice.begin + relative_begin + width,
            });
            assignment.depth_backward_costs.push_back(0);
        }

        for (std::size_t depth = 0; depth < graph.depth_slices.size(); ++depth) {
            if (depth + 1 >= graph.depth_slices.size()) {
                assignment.depth_reduce_ranges.push_back({});
                continue;
            }
            const auto& slice = graph.depth_slices[depth + 1];
            const auto count = slice.count;
            const auto base = count / static_cast<std::uint32_t>(workers);
            const auto remainder = count % static_cast<std::uint32_t>(workers);
            const auto relative_begin =
                static_cast<std::uint32_t>(worker_index) * base +
                std::min<std::uint32_t>(static_cast<std::uint32_t>(worker_index), remainder);
            const auto width = base + (worker_index < remainder ? 1U : 0U);
            assignment.depth_reduce_ranges.push_back(HUNLFlatRange{
                slice.begin + relative_begin,
                slice.begin + relative_begin + width,
            });
        }

        plan.workers.push_back(std::move(assignment));
    }

    return plan;
}

HUNLFlatParallelPlan HUNLFlatParallelPlan::build(
    const HUNLFlatSolveGraph& graph,
    const HUNLFlatInfosetTable& infoset_table,
    std::size_t worker_count) {
    HUNLFlatParallelPlan plan;
    const auto workers = std::max<std::size_t>(1, worker_count);
    const auto infoset_ranges = partition_range(static_cast<std::uint32_t>(graph.infosets.size()), workers);
    const auto node_ranges = partition_range(static_cast<std::uint32_t>(graph.node_count()), workers);

    plan.workers.reserve(workers);
    for (std::size_t worker_index = 0; worker_index < workers; ++worker_index) {
        HUNLFlatWorkerAssignment assignment;
        assignment.worker_index = static_cast<std::uint32_t>(worker_index);
        assignment.infoset_range = infoset_ranges[worker_index];
        assignment.bucket_range = {};
        assignment.value_range = {};
        assignment.node_range = node_ranges[worker_index];
        assignment.depth_node_ranges.reserve(graph.depth_slices.size());
        assignment.depth_reduce_ranges.reserve(graph.depth_slices.size());
        assignment.depth_backward_costs.reserve(graph.depth_slices.size());

        if (assignment.infoset_range.begin < assignment.infoset_range.end) {
            const auto bucket_begin =
                infoset_table.infoset_bucket_range(graph.infosets[assignment.infoset_range.begin].id).begin;
            const auto bucket_end =
                infoset_table.infoset_bucket_range(graph.infosets[assignment.infoset_range.end - 1].id).end;
            assignment.bucket_range = HUNLFlatRange{bucket_begin, bucket_end};
            assignment.value_range = infoset_table.infoset_value_range(assignment.infoset_range);
        }

        for (std::size_t depth = 0; depth < graph.depth_slices.size(); ++depth) {
            std::vector<std::uint64_t> costs;
            const auto ranges = partition_depth_slice_by_cost(
                graph,
                infoset_table,
                graph.depth_slices[depth],
                workers,
                &costs);
            assignment.depth_node_ranges.push_back(ranges[worker_index]);
            assignment.depth_backward_costs.push_back(costs[worker_index]);
        }

        for (std::size_t depth = 0; depth < graph.depth_slices.size(); ++depth) {
            if (depth + 1 >= graph.depth_slices.size()) {
                assignment.depth_reduce_ranges.push_back({});
                continue;
            }
            const auto& slice = graph.depth_slices[depth + 1];
            const auto count = slice.count;
            const auto base = count / static_cast<std::uint32_t>(workers);
            const auto remainder = count % static_cast<std::uint32_t>(workers);
            const auto relative_begin =
                static_cast<std::uint32_t>(worker_index) * base +
                std::min<std::uint32_t>(static_cast<std::uint32_t>(worker_index), remainder);
            const auto width = base + (worker_index < remainder ? 1U : 0U);
            assignment.depth_reduce_ranges.push_back(HUNLFlatRange{
                slice.begin + relative_begin,
                slice.begin + relative_begin + width,
            });
        }

        plan.workers.push_back(std::move(assignment));
    }

    return plan;
}

std::uint32_t HUNLFlatParallelPlan::estimated_backward_cost(
    const HUNLFlatNodeMeta& meta,
    const HUNLFlatInfosetTable& infoset_table) {
    switch (meta.type) {
        case HUNLFlatNodeType::TerminalFold:
        case HUNLFlatNodeType::TerminalShowdown:
        case HUNLFlatNodeType::DepthLimited:
            return 1;
        case HUNLFlatNodeType::Chance:
            return std::max<std::uint32_t>(1, meta.chance_count);
        case HUNLFlatNodeType::Decision:
            if (!meta.has_infoset) {
                return 1;
            }
            return std::max<std::uint32_t>(
                1,
                static_cast<std::uint32_t>(
                    std::max<std::size_t>(
                        1,
                        static_cast<std::size_t>(meta.action_count) *
                            infoset_table.meta().at(meta.infoset_id.value).bucket_count)));
    }
    return 1;
}

HUNLFlatInfosetTable HUNLFlatInfosetTable::build(
    const HUNLFlatSolveGraph& graph,
    const std::array<std::size_t, 2>& bucket_count_per_player,
    HUNLFlatValueLayout layout) {
    return build(graph, bucket_count_per_player, nullptr, layout);
}

HUNLFlatInfosetTable HUNLFlatInfosetTable::build(
    const HUNLFlatSolveGraph& graph,
    const std::array<std::size_t, 2>& bucket_count_per_player,
    const HUNLFlatBucketMap* bucket_map,
    HUNLFlatValueLayout layout) {
    validate_infoset_table_inputs(graph, bucket_count_per_player, bucket_map);
    HUNLFlatInfosetTable table;
    table.layout_ = layout;
    table.meta_.reserve(graph.infosets.size());

    std::uint32_t running_offset = 0;
    std::uint32_t running_bucket_offset = 0;
    for (const auto& infoset : graph.infosets) {
        if (infoset.player < 0 || infoset.player > 1) {
            throw std::logic_error("flat infoset table requires player-owned infosets");
        }

        std::size_t bucket_count = bucket_count_per_player[static_cast<std::size_t>(infoset.player)];
        if (bucket_map != nullptr) {
            bucket_count = bucket_map->bucket_count(infoset.id);
        }
        if (bucket_count == 0) {
            throw std::logic_error("HUNLFlatInfosetTable bucket_count must be positive");
        }
        if (infoset.action_count == 0) {
            throw std::logic_error("HUNLFlatInfosetTable infoset action_count must be positive");
        }
        const auto value_count =
            static_cast<std::uint32_t>(bucket_count * static_cast<std::size_t>(infoset.action_count));
        if (value_count == 0) {
            throw std::logic_error("HUNLFlatInfosetTable value_count must be positive");
        }

        table.meta_.push_back(HUNLFlatInfosetTableMeta{
            infoset.id,
            running_offset,
            value_count,
            running_bucket_offset,
            static_cast<std::uint32_t>(bucket_count),
            static_cast<std::uint32_t>(bucket_count),
            0,
            0,
            infoset.player,
            infoset.action_count,
        });
        running_offset += value_count;
        running_bucket_offset += static_cast<std::uint32_t>(bucket_count);
    }

    table.regret_sum_.assign(running_offset, 0.0);
    table.strategy_sum_.assign(running_offset, 0.0);
    table.current_strategy_.assign(running_offset, 0.0);
    return table;
}

const std::vector<HUNLFlatInfosetTableMeta>& HUNLFlatInfosetTable::meta() const noexcept {
    return meta_;
}

std::vector<HUNLFlatInfosetTableMeta>& HUNLFlatInfosetTable::meta_mut() noexcept {
    return meta_;
}

std::size_t HUNLFlatInfosetTable::infoset_count() const noexcept {
    return meta_.size();
}

HUNLFlatValueLayout HUNLFlatInfosetTable::layout() const noexcept {
    return layout_;
}

const double* HUNLFlatInfosetTable::regret(InfosetId id) const {
    return regret_sum_.data() + meta_for(id).offset;
}

double* HUNLFlatInfosetTable::regret_mut(InfosetId id) {
    return regret_sum_.data() + meta_for(id).offset;
}

const double* HUNLFlatInfosetTable::strategy_sum(InfosetId id) const {
    return strategy_sum_.data() + meta_for(id).offset;
}

double* HUNLFlatInfosetTable::strategy_sum_mut(InfosetId id) {
    return strategy_sum_.data() + meta_for(id).offset;
}

const double* HUNLFlatInfosetTable::current_strategy(InfosetId id) const {
    return current_strategy_.data() + meta_for(id).offset;
}

double* HUNLFlatInfosetTable::current_strategy_mut(InfosetId id) {
    return current_strategy_.data() + meta_for(id).offset;
}

std::size_t HUNLFlatInfosetTable::row_value_count(InfosetId id) const {
    return meta_for(id).value_count;
}

std::size_t HUNLFlatInfosetTable::total_value_count() const noexcept {
    return regret_sum_.size();
}

std::size_t HUNLFlatInfosetTable::total_bucket_count() const noexcept {
    if (meta_.empty()) {
        return 0;
    }
    const auto& last = meta_.back();
    return static_cast<std::size_t>(last.bucket_offset + last.bucket_count);
}

std::size_t HUNLFlatInfosetTable::value_index(InfosetId id, std::size_t hand_idx, std::size_t action_idx) const {
    const auto& meta = meta_for(id);
    if (hand_idx >= meta.bucket_count) {
        throw std::out_of_range("HUNLFlatInfosetTable bucket_idx out of range");
    }
    if (action_idx >= meta.action_count) {
        throw std::out_of_range("HUNLFlatInfosetTable action_idx out of range");
    }

    if (layout_ == HUNLFlatValueLayout::InfosetHandAction) {
        return meta.offset + hand_idx * static_cast<std::size_t>(meta.action_count) + action_idx;
    }
    return meta.offset + action_idx * static_cast<std::size_t>(meta.bucket_count) + hand_idx;
}

HUNLFlatRange HUNLFlatInfosetTable::infoset_value_range(HUNLFlatRange infoset_range) const {
    if (infoset_range.begin > infoset_range.end || infoset_range.end > meta_.size()) {
        throw std::out_of_range("HUNLFlatInfosetTable infoset range out of bounds");
    }
    if (infoset_range.begin == infoset_range.end) {
        return {};
    }

    const auto begin = meta_[infoset_range.begin].offset;
    const auto& last = meta_[infoset_range.end - 1];
    return HUNLFlatRange{begin, last.offset + last.value_count};
}

HUNLFlatRange HUNLFlatInfosetTable::infoset_bucket_range(InfosetId id) const {
    const auto& meta = meta_for(id);
    return HUNLFlatRange{meta.bucket_offset, meta.bucket_offset + meta.bucket_count};
}

const HUNLFlatInfosetTableMeta& HUNLFlatInfosetTable::meta_for(InfosetId id) const {
    if (id.value >= meta_.size()) {
        throw std::out_of_range("HUNLFlatInfosetTable invalid InfosetId");
    }
    return meta_[id.value];
}

HUNLFlatInfosetTableMeta& HUNLFlatInfosetTable::meta_for(InfosetId id) {
    if (id.value >= meta_.size()) {
        throw std::out_of_range("HUNLFlatInfosetTable invalid InfosetId");
    }
    return meta_[id.value];
}

}  // namespace core
