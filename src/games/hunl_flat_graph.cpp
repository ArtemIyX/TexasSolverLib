#include "games/hunl_flat_graph.hpp"

#include <cstdint>
#include <stdexcept>
#include <unordered_map>
#include <utility>
#include <vector>

namespace core {

namespace {

HUNLFlatNodeType classify_flat_node_type(const HUNLTreeNode& node) {
    switch (node.terminal_kind.tag) {
        case TerminalKindTag::Fold:
            return HUNLFlatNodeType::TerminalFold;
        case TerminalKindTag::Showdown:
            return HUNLFlatNodeType::TerminalShowdown;
        case TerminalKindTag::NonTerminal:
            break;
    }

    if (!node.chance_children.empty()) {
        return HUNLFlatNodeType::Chance;
    }
    return HUNLFlatNodeType::Decision;
}

}  // namespace

HUNLFlatSolveGraph HUNLFlatSolveGraph::build(const HUNLTree& tree) {
    HUNLFlatSolveGraph graph;
    graph.nodes.reserve(tree.nodes.size());
    graph.children.reserve(tree.nodes.size() * 2);
    graph.actions.reserve(tree.nodes.size() * 2);
    graph.root = tree.root;
    graph.max_depth = tree.max_depth;
    graph.max_actions = tree.max_actions;
    graph.config = tree.config;

    std::size_t total_chance_outcomes = 0;
    for (const auto& node : tree.nodes) {
        total_chance_outcomes += node.chance_outcomes.size();
    }
    graph.chance_outcomes.reserve(total_chance_outcomes);

    std::unordered_map<std::string, InfosetId> infoset_ids_by_key;
    std::vector<std::vector<std::uint32_t>> infoset_node_lists;
    std::vector<std::uint8_t> infoset_action_counts;
    std::vector<std::string> infoset_keys;

    for (std::uint32_t node_idx = 0; node_idx < tree.nodes.size(); ++node_idx) {
        const auto& node = tree.nodes[node_idx];
        HUNLFlatNode flat_node;
        flat_node.type = classify_flat_node_type(node);
        flat_node.player = node.player;
        flat_node.street = node.street;
        flat_node.contributions = node.contrib;
        flat_node.terminal_kind = node.terminal_kind;
        flat_node.infoset_key = node.infoset_key;

        if (flat_node.type == HUNLFlatNodeType::Chance) {
            if (node.chance_children.size() != node.chance_outcomes.size()) {
                throw std::logic_error("chance node children and outcomes must have matching sizes");
            }

            flat_node.chance_begin = static_cast<std::uint32_t>(graph.chance_outcomes.size());
            flat_node.chance_count = static_cast<std::uint32_t>(node.chance_outcomes.size());
            flat_node.child_begin = static_cast<std::uint32_t>(graph.children.size());
            flat_node.child_count = static_cast<std::uint32_t>(node.chance_children.size());

            for (std::size_t i = 0; i < node.chance_children.size(); ++i) {
                graph.children.push_back(node.chance_children[i]);
                graph.chance_outcomes.push_back(HUNLFlatChanceOutcome{
                    node.chance_outcomes[i].first,
                    node.chance_outcomes[i].second,
                    node.chance_children[i],
                });
            }
        } else if (flat_node.type == HUNLFlatNodeType::Decision) {
            if (node.children.size() != node.legal_actions.size()) {
                throw std::logic_error("decision node children and actions must have matching sizes");
            }
            if (!node.infoset_key.has_value()) {
                throw std::logic_error("decision node must have infoset_key");
            }

            flat_node.child_begin = static_cast<std::uint32_t>(graph.children.size());
            flat_node.child_count = static_cast<std::uint32_t>(node.children.size());
            flat_node.action_begin = static_cast<std::uint32_t>(graph.actions.size());
            flat_node.action_count = static_cast<std::uint8_t>(node.legal_actions.size());
            flat_node.has_infoset = true;

            const auto key_it = infoset_ids_by_key.find(*node.infoset_key);
            if (key_it == infoset_ids_by_key.end()) {
                const InfosetId id{static_cast<std::uint32_t>(infoset_keys.size())};
                infoset_ids_by_key.emplace(*node.infoset_key, id);
                infoset_node_lists.push_back({});
                infoset_action_counts.push_back(flat_node.action_count);
                infoset_keys.push_back(*node.infoset_key);
                flat_node.infoset_id = id;
            } else {
                flat_node.infoset_id = key_it->second;
                const auto action_count = infoset_action_counts[flat_node.infoset_id.value];
                if (action_count != flat_node.action_count) {
                    throw std::logic_error("infoset nodes must agree on action_count");
                }
            }
            infoset_node_lists[flat_node.infoset_id.value].push_back(node_idx);

            for (std::size_t i = 0; i < node.children.size(); ++i) {
                graph.children.push_back(node.children[i]);
                graph.actions.push_back(node.legal_actions[i]);
            }
        }

        graph.nodes.push_back(std::move(flat_node));
    }

    graph.infosets.reserve(infoset_keys.size());
    for (std::uint32_t infoset_index = 0; infoset_index < infoset_keys.size(); ++infoset_index) {
        const auto& node_list = infoset_node_lists[infoset_index];
        HUNLFlatInfoset infoset;
        infoset.id = InfosetId{infoset_index};
        infoset.node_begin = static_cast<std::uint32_t>(graph.infoset_nodes.size());
        infoset.node_count = static_cast<std::uint32_t>(node_list.size());
        infoset.action_count = infoset_action_counts[infoset_index];
        infoset.key = infoset_keys[infoset_index];
        graph.infoset_nodes.insert(graph.infoset_nodes.end(), node_list.begin(), node_list.end());
        graph.infosets.push_back(std::move(infoset));
    }

    return graph;
}

HUNLFlatSolveGraph HUNLFlatSolveGraph::build(std::shared_ptr<const HUNLConfig> config) {
    return build(HUNLTree::build(std::move(config)));
}

}  // namespace core
