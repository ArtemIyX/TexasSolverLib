#include "games/hunl_flat_graph.hpp"

#include <stdexcept>
#include <utility>

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

    for (const auto& node : tree.nodes) {
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

            flat_node.child_begin = static_cast<std::uint32_t>(graph.children.size());
            flat_node.child_count = static_cast<std::uint32_t>(node.children.size());
            flat_node.action_begin = static_cast<std::uint32_t>(graph.actions.size());
            flat_node.action_count = static_cast<std::uint8_t>(node.legal_actions.size());

            for (std::size_t i = 0; i < node.children.size(); ++i) {
                graph.children.push_back(node.children[i]);
                graph.actions.push_back(node.legal_actions[i]);
            }
        }

        graph.nodes.push_back(std::move(flat_node));
    }

    return graph;
}

HUNLFlatSolveGraph HUNLFlatSolveGraph::build(std::shared_ptr<const HUNLConfig> config) {
    return build(HUNLTree::build(std::move(config)));
}

}  // namespace core
