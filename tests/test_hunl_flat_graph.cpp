#include "games/hunl_flat_graph.hpp"
#include "test_harness.hpp"

#include <algorithm>
#include <cstdint>
#include <memory>
#include <unordered_map>

TEST_CASE(hunl_flat_graph_builds_from_tree_with_stable_indices) {
    const auto config = std::make_shared<const core::HUNLConfig>(core::default_tiny_subgame());
    const auto tree = core::HUNLTree::build(config);
    const auto graph = core::HUNLFlatSolveGraph::build(tree);

    EXPECT_EQ(graph.root, tree.root);
    EXPECT_EQ(graph.max_depth, tree.max_depth);
    EXPECT_EQ(graph.max_actions, tree.max_actions);
    EXPECT_EQ(static_cast<std::size_t>(graph.nodes.size()), static_cast<std::size_t>(tree.nodes.size()));
    EXPECT_EQ(static_cast<std::size_t>(graph.node_meta.size()), static_cast<std::size_t>(tree.nodes.size()));
    EXPECT_TRUE(!graph.infosets.empty());
}

TEST_CASE(hunl_flat_graph_preserves_decision_and_chance_layout) {
    const auto config = std::make_shared<const core::HUNLConfig>(core::default_tiny_subgame());
    const auto tree = core::HUNLTree::build(config);
    const auto graph = core::HUNLFlatSolveGraph::build(tree);

    bool saw_decision = false;
    bool saw_fold = false;
    bool saw_showdown = false;

    for (std::size_t node_idx = 0; node_idx < tree.nodes.size(); ++node_idx) {
        const auto& tree_node = tree.nodes[node_idx];
        const auto& flat_node = graph.nodes[node_idx];

        EXPECT_EQ(flat_node.player, tree_node.player);
        EXPECT_EQ(flat_node.street, tree_node.street);
        EXPECT_EQ(flat_node.contributions, tree_node.contrib);

        if (tree_node.terminal_kind.tag == core::TerminalKindTag::Fold) {
            EXPECT_EQ(flat_node.type, core::HUNLFlatNodeType::TerminalFold);
            saw_fold = true;
            continue;
        }
        if (tree_node.terminal_kind.tag == core::TerminalKindTag::Showdown) {
            EXPECT_EQ(flat_node.type, core::HUNLFlatNodeType::TerminalShowdown);
            saw_showdown = true;
            continue;
        }
        if (!tree_node.chance_children.empty()) {
            EXPECT_EQ(flat_node.type, core::HUNLFlatNodeType::Chance);
            EXPECT_EQ(static_cast<std::size_t>(flat_node.child_count), static_cast<std::size_t>(tree_node.chance_children.size()));
            EXPECT_EQ(static_cast<std::size_t>(flat_node.chance_count), static_cast<std::size_t>(tree_node.chance_outcomes.size()));
            for (std::size_t i = 0; i < tree_node.chance_children.size(); ++i) {
                EXPECT_EQ(graph.children[flat_node.child_begin + i], tree_node.chance_children[i]);
                const auto& outcome = graph.chance_outcomes[flat_node.chance_begin + i];
                EXPECT_EQ(outcome.child, tree_node.chance_children[i]);
                EXPECT_EQ(outcome.action, tree_node.chance_outcomes[i].first);
                EXPECT_NEAR(outcome.probability, tree_node.chance_outcomes[i].second, 1e-12);
            }
            continue;
        }

        EXPECT_EQ(flat_node.type, core::HUNLFlatNodeType::Decision);
        EXPECT_EQ(static_cast<std::size_t>(flat_node.child_count), static_cast<std::size_t>(tree_node.children.size()));
        EXPECT_EQ(static_cast<std::size_t>(flat_node.action_count), static_cast<std::size_t>(tree_node.legal_actions.size()));
        saw_decision = true;

        for (std::size_t i = 0; i < tree_node.children.size(); ++i) {
            EXPECT_EQ(graph.children[flat_node.child_begin + i], tree_node.children[i]);
            EXPECT_EQ(graph.actions[flat_node.action_begin + i], tree_node.legal_actions[i]);
        }
    }

    EXPECT_TRUE(saw_decision);
    EXPECT_TRUE(saw_fold);
    EXPECT_TRUE(saw_showdown);
}

TEST_CASE(hunl_flat_graph_assigns_stable_infoset_ids_and_groups) {
    const auto config = std::make_shared<const core::HUNLConfig>(core::default_tiny_subgame());
    const auto tree = core::HUNLTree::build(config);
    const auto graph = core::HUNLFlatSolveGraph::build(tree);

    std::unordered_map<std::string, std::uint32_t> expected_ids_by_key;
    std::uint32_t expected_next_id = 0;

    for (std::uint32_t node_idx = 0; node_idx < graph.nodes.size(); ++node_idx) {
        const auto& node = graph.nodes[node_idx];
        if (node.type != core::HUNLFlatNodeType::Decision) {
            EXPECT_TRUE(!node.has_infoset);
            continue;
        }

        EXPECT_TRUE(node.has_infoset);
        const auto& infoset = graph.infosets[node.infoset_id.value];

        const auto [it, inserted] =
            expected_ids_by_key.emplace(infoset.key, node.infoset_id.value);
        if (inserted) {
            EXPECT_EQ(node.infoset_id.value, expected_next_id);
            ++expected_next_id;
        } else {
            EXPECT_EQ(node.infoset_id.value, it->second);
        }

        EXPECT_TRUE(node.infoset_id.value < graph.infosets.size());
        EXPECT_EQ(infoset.id.value, node.infoset_id.value);
        EXPECT_EQ(infoset.action_count, node.action_count);
        EXPECT_EQ(infoset.player, node.player);

        bool found_node = false;
        for (std::uint32_t i = 0; i < infoset.node_count; ++i) {
            if (graph.infoset_nodes[infoset.node_begin + i] == node_idx) {
                found_node = true;
                break;
            }
        }
        EXPECT_TRUE(found_node);
    }

    EXPECT_EQ(static_cast<std::size_t>(expected_ids_by_key.size()), static_cast<std::size_t>(graph.infosets.size()));
}

TEST_CASE(hunl_flat_graph_precomputes_compact_node_metadata) {
    const auto config = std::make_shared<const core::HUNLConfig>(core::default_tiny_subgame());
    const auto tree = core::HUNLTree::build(config);
    const auto graph = core::HUNLFlatSolveGraph::build(tree);

    for (std::uint32_t node_idx = 0; node_idx < graph.nodes.size(); ++node_idx) {
        const auto& node = graph.nodes[node_idx];
        const auto& meta = graph.node_meta[node_idx];

        EXPECT_EQ(meta.type, node.type);
        EXPECT_EQ(meta.player, node.player);
        EXPECT_EQ(meta.street, node.street);
        EXPECT_EQ(meta.contributions, node.contributions);
        EXPECT_EQ(meta.child_begin, node.child_begin);
        EXPECT_EQ(meta.child_count, node.child_count);
        EXPECT_EQ(meta.action_begin, node.action_begin);
        EXPECT_EQ(meta.chance_begin, node.chance_begin);
        EXPECT_EQ(meta.chance_count, node.chance_count);
        EXPECT_EQ(meta.action_count, node.action_count);
        EXPECT_EQ(meta.infoset_id.value, node.infoset_id.value);
        EXPECT_EQ(meta.has_infoset, node.has_infoset);
        EXPECT_EQ(meta.terminal_kind.tag, node.terminal_kind.tag);
        EXPECT_EQ(meta.terminal_kind.winner, node.terminal_kind.winner);
        EXPECT_EQ(meta.terminal_kind.contribution_loss, node.terminal_kind.contribution_loss);
        EXPECT_EQ(meta.terminal_kind.board_complete, node.terminal_kind.board_complete);

        for (std::uint32_t i = 0; i < meta.child_count; ++i) {
            EXPECT_TRUE(meta.child_begin + i < graph.children.size());
        }
        for (std::uint32_t i = 0; i < meta.chance_count; ++i) {
            EXPECT_TRUE(meta.chance_begin + i < graph.chance_outcomes.size());
        }

        if (meta.type == core::HUNLFlatNodeType::Decision) {
            EXPECT_TRUE(meta.has_infoset);
            EXPECT_TRUE(meta.infoset_id.value < graph.infosets.size());
        } else {
            EXPECT_TRUE(!meta.has_infoset);
        }
    }
}

TEST_CASE(hunl_flat_graph_precomputes_stage_friendly_traversal_orders) {
    const auto config = std::make_shared<const core::HUNLConfig>(core::default_tiny_subgame());
    const auto tree = core::HUNLTree::build(config);
    const auto graph = core::HUNLFlatSolveGraph::build(tree);

    EXPECT_EQ(graph.forward_order.size(), graph.nodes.size());
    EXPECT_EQ(graph.reverse_order.size(), graph.nodes.size());
    EXPECT_EQ(graph.node_depths.size(), graph.nodes.size());
    EXPECT_EQ(graph.depth_order.size(), graph.nodes.size());
    EXPECT_EQ(graph.street_order.size(), graph.nodes.size());

    std::vector<std::uint32_t> forward_pos(graph.nodes.size(), 0);
    for (std::uint32_t i = 0; i < graph.forward_order.size(); ++i) {
        forward_pos[graph.forward_order[i]] = i;
    }

    std::vector<bool> seen_forward(graph.nodes.size(), false);
    for (const auto node_idx : graph.forward_order) {
        EXPECT_TRUE(node_idx < graph.nodes.size());
        EXPECT_TRUE(!seen_forward[node_idx]);
        seen_forward[node_idx] = true;
    }

    for (std::uint32_t node_idx = 0; node_idx < graph.node_meta.size(); ++node_idx) {
        const auto& meta = graph.node_meta[node_idx];
        for (std::uint32_t i = 0; i < meta.child_count; ++i) {
            const auto child = graph.children[meta.child_begin + i];
            EXPECT_TRUE(forward_pos[node_idx] < forward_pos[child]);
            EXPECT_TRUE(graph.node_depths[child] > graph.node_depths[node_idx]);
        }
    }

    auto sorted_forward = graph.forward_order;
    auto sorted_reverse = graph.reverse_order;
    std::sort(sorted_forward.begin(), sorted_forward.end());
    std::sort(sorted_reverse.begin(), sorted_reverse.end());
    for (std::uint32_t i = 0; i < graph.nodes.size(); ++i) {
        EXPECT_EQ(sorted_forward[i], i);
        EXPECT_EQ(sorted_reverse[i], i);
    }

    std::uint32_t depth_total = 0;
    for (std::size_t depth = 0; depth < graph.depth_slices.size(); ++depth) {
        const auto slice = graph.depth_slices[depth];
        depth_total += slice.count;
        for (std::uint32_t i = 0; i < slice.count; ++i) {
            const auto node_idx = graph.depth_order[slice.begin + i];
            EXPECT_EQ(graph.node_depths[node_idx], depth);
        }

        std::uint32_t ranged_total = 0;
        std::uint32_t cursor = slice.begin;
        for (const auto& range : graph.depth_worker_ranges[depth]) {
            EXPECT_EQ(range.begin, cursor);
            EXPECT_TRUE(range.begin < range.end);
            EXPECT_TRUE(range.end <= slice.begin + slice.count);
            ranged_total += range.end - range.begin;
            cursor = range.end;
        }
        EXPECT_EQ(ranged_total, slice.count);
        EXPECT_EQ(cursor, slice.begin + slice.count);
    }
    EXPECT_EQ(depth_total, static_cast<std::uint32_t>(graph.nodes.size()));

    std::uint32_t street_total = 0;
    for (std::size_t street = 0; street < graph.street_slices.size(); ++street) {
        const auto slice = graph.street_slices[street];
        street_total += slice.count;
        for (std::uint32_t i = 0; i < slice.count; ++i) {
            const auto node_idx = graph.street_order[slice.begin + i];
            EXPECT_EQ(static_cast<std::size_t>(graph.node_meta[node_idx].street), street);
        }
    }
    EXPECT_EQ(street_total, static_cast<std::uint32_t>(graph.nodes.size()));
}
