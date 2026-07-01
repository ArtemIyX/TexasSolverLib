#include "solver/hunl_flat_expected_value.hpp"

#include <algorithm>
#include <stdexcept>

namespace core {

namespace {

using StrategyMap = std::unordered_map<std::string, std::vector<double>>;

double clamp_probability(double value) {
    return std::max(0.0, value);
}

std::vector<const std::vector<double>*> build_strategy_rows(
    const HUNLFlatSolveGraph& graph,
    const StrategyMap& average_strategy) {
    std::vector<const std::vector<double>*> rows(graph.infosets.size(), nullptr);
    for (const auto& infoset : graph.infosets) {
        const auto it = average_strategy.find(infoset.key);
        if (it != average_strategy.end()) {
            rows[infoset.id.value] = &it->second;
        }
    }
    return rows;
}

std::vector<double> averaged_action_probabilities(
    const HUNLFlatInfoset& infoset,
    const std::vector<double>* row) {
    const auto action_count = static_cast<std::size_t>(infoset.action_count);
    if (action_count == 0) {
        return {};
    }

    std::vector<double> probs(action_count, 1.0 / static_cast<double>(action_count));
    if (row == nullptr) {
        return probs;
    }

    if (row->size() == action_count) {
        double total = 0.0;
        for (std::size_t action = 0; action < action_count; ++action) {
            probs[action] = clamp_probability((*row)[action]);
            total += probs[action];
        }
        if (total > 0.0) {
            for (auto& prob : probs) {
                prob /= total;
            }
        }
        return probs;
    }

    if (row->size() % action_count != 0) {
        return probs;
    }

    const auto bucket_count = row->size() / action_count;
    if (bucket_count == 0) {
        return probs;
    }

    std::fill(probs.begin(), probs.end(), 0.0);
    for (std::size_t bucket = 0; bucket < bucket_count; ++bucket) {
        const auto bucket_offset = bucket * action_count;
        double row_total = 0.0;
        for (std::size_t action = 0; action < action_count; ++action) {
            const auto value = clamp_probability((*row)[bucket_offset + action]);
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
    for (auto& prob : probs) {
        total += prob;
    }
    if (total > 0.0) {
        for (auto& prob : probs) {
            prob /= total;
        }
    } else {
        const auto uniform = 1.0 / static_cast<double>(action_count);
        std::fill(probs.begin(), probs.end(), uniform);
    }
    return probs;
}

}  // namespace

std::array<double, 2> compute_flat_expected_value(
    const HUNLFlatSolveGraph& graph,
    const std::unordered_map<std::string, std::vector<double>>& average_strategy) {
    if (graph.nodes.empty()) {
        return {0.0, 0.0};
    }
    if (graph.root >= graph.nodes.size()) {
        throw std::out_of_range("compute_flat_expected_value root out of bounds");
    }

    const auto strategy_rows = build_strategy_rows(graph, average_strategy);
    std::vector<std::array<double, 2>> node_values(graph.nodes.size(), {0.0, 0.0});

    for (const auto node_idx : graph.reverse_order) {
        const auto& meta = graph.node_meta.at(node_idx);
        switch (meta.type) {
            case HUNLFlatNodeType::TerminalFold:
            case HUNLFlatNodeType::TerminalShowdown:
            case HUNLFlatNodeType::DepthLimited:
                node_values[node_idx] = meta.terminal_utility;
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

                const auto& infoset = graph.infosets[meta.infoset_id.value];
                const auto probs =
                    averaged_action_probabilities(infoset, strategy_rows[meta.infoset_id.value]);

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

}  // namespace core
