#include "solver/hunl_flat_state.hpp"

#include <algorithm>
#include <stdexcept>

namespace core {

namespace {

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

}  // namespace

void HUNLFlatWorkerScratch::reset_values() noexcept {
    std::fill(terminal_values.begin(), terminal_values.end(), 0.0);
    std::fill(node_values.begin(), node_values.end(), 0.0);
    std::fill(action_values.begin(), action_values.end(), 0.0);
    std::fill(player0_reach.begin(), player0_reach.end(), 0.0);
    std::fill(player1_reach.begin(), player1_reach.end(), 0.0);
    std::fill(chance_reach.begin(), chance_reach.end(), 0.0);
    std::fill(bucket_reach.begin(), bucket_reach.end(), 0.0);
    dirty_nodes.clear();
    dirty_buckets.clear();
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
    terminal_values.assign(node_count, 0.0);
    node_values.assign(node_count, 0.0);
    action_values.assign(edge_count, 0.0);
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
}

HUNLFlatParallelPlan HUNLFlatParallelPlan::build(const HUNLFlatSolveGraph& graph, std::size_t worker_count) {
    HUNLFlatInfosetTable empty_table;
    (void)empty_table;
    HUNLFlatParallelPlan plan;
    const auto workers = std::max<std::size_t>(1, worker_count);
    const auto infoset_ranges = partition_range(static_cast<std::uint32_t>(graph.infosets.size()), workers);
    const auto node_ranges = partition_range(static_cast<std::uint32_t>(graph.nodes.size()), workers);

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
    const auto node_ranges = partition_range(static_cast<std::uint32_t>(graph.nodes.size()), workers);

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

        if (assignment.infoset_range.begin < assignment.infoset_range.end) {
            const auto bucket_begin =
                infoset_table.infoset_bucket_range(graph.infosets[assignment.infoset_range.begin].id).begin;
            const auto bucket_end =
                infoset_table.infoset_bucket_range(graph.infosets[assignment.infoset_range.end - 1].id).end;
            assignment.bucket_range = HUNLFlatRange{bucket_begin, bucket_end};
            assignment.value_range = infoset_table.infoset_value_range(assignment.infoset_range);
        }

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
