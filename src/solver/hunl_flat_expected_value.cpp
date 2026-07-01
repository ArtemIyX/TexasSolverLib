#include "solver/hunl_flat_expected_value.hpp"

#include <algorithm>
#include <stdexcept>

namespace core {

namespace {

using StrategyMap = std::unordered_map<std::string, std::vector<double>>;

double clamp_probability(double value) {
    return std::max(0.0, value);
}

std::vector<double> fallback_probabilities(std::size_t action_count) {
    if (action_count == 0) {
        return {};
    }
    return std::vector<double>(action_count, 1.0 / static_cast<double>(action_count));
}

std::vector<double> averaged_action_probabilities(
    const HUNLFlatInfosetTableMeta& meta,
    HUNLFlatValueLayout layout,
    const double* row) {
    const auto action_count = static_cast<std::size_t>(meta.action_count);
    auto probs = fallback_probabilities(action_count);
    if (row == nullptr) {
        return probs;
    }

    if (meta.bucket_count == 0 || action_count == 0) {
        return probs;
    }

    std::fill(probs.begin(), probs.end(), 0.0);
    for (std::size_t bucket = 0; bucket < meta.bucket_count; ++bucket) {
        double row_total = 0.0;
        for (std::size_t action = 0; action < action_count; ++action) {
            const auto value = clamp_probability(
                row[layout == HUNLFlatValueLayout::InfosetActionHand
                        ? action * static_cast<std::size_t>(meta.bucket_count) + bucket
                        : bucket * action_count + action]);
            probs[action] += value;
            row_total += value;
        }
        if (row_total == 0.0) {
            for (std::size_t action = 0; action < action_count; ++action) {
                probs[action] += 1.0;
            }
        }
    }

    double total = 0.0;
    for (const auto prob : probs) {
        total += prob;
    }
    if (total > 0.0) {
        for (auto& prob : probs) {
            prob /= total;
        }
        return probs;
    }
    return fallback_probabilities(action_count);
}

}  // namespace

HUNLFlatAverageStrategyView HUNLFlatAverageStrategyTable::view() const {
    HUNLFlatAverageStrategyView out;
    out.meta = &meta;
    out.layout = layout;
    out.rows_by_infoset.resize(meta.size(), nullptr);
    for (const auto& infoset_meta : meta) {
        out.rows_by_infoset[infoset_meta.id.value] = values.data() + infoset_meta.offset;
    }
    return out;
}

HUNLFlatAverageStrategyTable build_flat_average_strategy_table(
    const HUNLFlatSolveGraph& graph,
    const std::unordered_map<std::string, std::vector<double>>& average_strategy,
    HUNLFlatValueLayout layout) {
    HUNLFlatAverageStrategyTable out;
    out.layout = layout;
    out.meta.reserve(graph.infosets.size());

    std::uint32_t running_offset = 0;
    for (const auto& infoset : graph.infosets) {
        const auto it = average_strategy.find(infoset.key);
        const auto bucket_count = it != average_strategy.end() && infoset.action_count > 0
            ? static_cast<std::uint32_t>(it->second.size() / static_cast<std::size_t>(infoset.action_count))
            : 1U;
        const auto value_count = static_cast<std::uint32_t>(
            static_cast<std::size_t>(bucket_count) * static_cast<std::size_t>(infoset.action_count));
        out.meta.push_back(HUNLFlatInfosetTableMeta{
            infoset.id,
            running_offset,
            value_count,
            0,
            bucket_count,
            bucket_count,
            0,
            0,
            infoset.player,
            infoset.action_count,
        });
        running_offset += value_count;
    }

    out.values.assign(running_offset, 0.0);
    for (const auto& infoset : graph.infosets) {
        const auto& meta = out.meta[infoset.id.value];
        auto* dst = out.values.data() + meta.offset;
        const auto it = average_strategy.find(infoset.key);
        if (it == average_strategy.end()) {
            const auto fallback = fallback_probabilities(infoset.action_count);
            for (std::size_t bucket = 0; bucket < meta.bucket_count; ++bucket) {
                for (std::size_t action = 0; action < infoset.action_count; ++action) {
                    const auto idx = layout == HUNLFlatValueLayout::InfosetActionHand
                        ? action * static_cast<std::size_t>(meta.bucket_count) + bucket
                        : bucket * static_cast<std::size_t>(infoset.action_count) + action;
                    dst[idx] = fallback[action];
                }
            }
            continue;
        }

        const auto& src = it->second;
        if (src.size() == meta.value_count) {
            std::copy(src.begin(), src.end(), dst);
            continue;
        }

        if (src.size() == infoset.action_count) {
            for (std::size_t bucket = 0; bucket < meta.bucket_count; ++bucket) {
                for (std::size_t action = 0; action < infoset.action_count; ++action) {
                    const auto idx = layout == HUNLFlatValueLayout::InfosetActionHand
                        ? action * static_cast<std::size_t>(meta.bucket_count) + bucket
                        : bucket * static_cast<std::size_t>(infoset.action_count) + action;
                    dst[idx] = src[action];
                }
            }
        }
    }

    return out;
}

HUNLFlatTerminalValueTable build_flat_terminal_value_table(const HUNLFlatSolveGraph& graph) {
    HUNLFlatTerminalValueTable out(graph.nodes.size(), {0.0, 0.0});
    for (std::size_t node_idx = 0; node_idx < graph.node_meta.size(); ++node_idx) {
        const auto& meta = graph.node_meta[node_idx];
        if (meta.type == HUNLFlatNodeType::TerminalFold ||
            meta.type == HUNLFlatNodeType::TerminalShowdown ||
            meta.type == HUNLFlatNodeType::DepthLimited) {
            out[node_idx] = meta.terminal_utility;
        }
    }
    return out;
}

std::array<double, 2> compute_flat_expected_value(
    const HUNLFlatSolveGraph& graph,
    const HUNLFlatAverageStrategyView& average_strategy,
    const HUNLFlatTerminalValueTable* terminal_values) {
    if (graph.nodes.empty()) {
        return {0.0, 0.0};
    }
    if (graph.root >= graph.nodes.size()) {
        throw std::out_of_range("compute_flat_expected_value root out of bounds");
    }
    if (average_strategy.meta == nullptr) {
        throw std::invalid_argument("compute_flat_expected_value strategy view requires meta");
    }
    if (average_strategy.rows_by_infoset.size() < graph.infosets.size()) {
        throw std::invalid_argument("compute_flat_expected_value strategy rows_by_infoset too small");
    }
    if (average_strategy.meta->size() < graph.infosets.size()) {
        throw std::invalid_argument("compute_flat_expected_value strategy meta too small");
    }
    if (terminal_values != nullptr && terminal_values->size() < graph.nodes.size()) {
        throw std::invalid_argument("compute_flat_expected_value terminal cache too small");
    }

    std::vector<std::array<double, 2>> node_values(graph.nodes.size(), {0.0, 0.0});
    const auto& strategy_meta = *average_strategy.meta;

    for (const auto node_idx : graph.reverse_order) {
        const auto& meta = graph.node_meta.at(node_idx);
        switch (meta.type) {
            case HUNLFlatNodeType::TerminalFold:
            case HUNLFlatNodeType::TerminalShowdown:
            case HUNLFlatNodeType::DepthLimited:
                node_values[node_idx] = terminal_values != nullptr
                    ? terminal_values->at(node_idx)
                    : meta.terminal_utility;
                break;

            case HUNLFlatNodeType::Chance: {
                std::array<double, 2> value = {0.0, 0.0};
                for (std::size_t i = 0; i < meta.chance_count; ++i) {
                    const auto& outcome = graph.chance_outcomes.at(meta.chance_begin + i);
                    const auto& child = node_values.at(outcome.child);
                    value[0] += outcome.probability * child[0];
                    value[1] += outcome.probability * child[1];
                }
                node_values[node_idx] = value;
                break;
            }

            case HUNLFlatNodeType::Decision: {
                if (!meta.has_infoset || meta.infoset_id.value >= graph.infosets.size()) {
                    throw std::logic_error("compute_flat_expected_value decision node missing infoset");
                }

                const auto& infoset_meta = strategy_meta.at(meta.infoset_id.value);
                const auto probs = averaged_action_probabilities(
                    infoset_meta,
                    average_strategy.layout,
                    average_strategy.rows_by_infoset[meta.infoset_id.value]);

                std::array<double, 2> value = {0.0, 0.0};
                for (std::size_t action = 0; action < meta.child_count; ++action) {
                    const auto child_idx = graph.children.at(meta.child_begin + action);
                    const auto& child = node_values.at(child_idx);
                    const auto prob = action < probs.size() ? probs[action] : 0.0;
                    value[0] += prob * child[0];
                    value[1] += prob * child[1];
                }
                node_values[node_idx] = value;
                break;
            }
        }
    }

    return node_values[graph.root];
}

std::array<double, 2> compute_flat_expected_value(
    const HUNLFlatSolveGraph& graph,
    const std::unordered_map<std::string, std::vector<double>>& average_strategy) {
    const auto table = build_flat_average_strategy_table(graph, average_strategy);
    const auto terminal_values = build_flat_terminal_value_table(graph);
    return compute_flat_expected_value(graph, table.view(), &terminal_values);
}

}  // namespace core
