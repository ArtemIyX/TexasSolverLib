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
    EXPECT_EQ(static_cast<std::size_t>(graph.node_meta.size()), static_cast<std::size_t>(tree.nodes.size()));
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
        const auto& flat_node = graph.node_meta[node_idx];

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

    for (std::uint32_t node_idx = 0; node_idx < graph.node_meta.size(); ++node_idx) {
        const auto& node = graph.node_meta[node_idx];
        if (node.type != core::HUNLFlatNodeType::Decision) {
            EXPECT_TRUE(!node.has_infoset);
            continue;
        }

        EXPECT_TRUE(node.has_infoset);
        const auto& infoset = graph.infosets[node.infoset_id.value];

        const auto [it, inserted] =
            expected_ids_by_key.emplace(std::string(graph.infoset_key(infoset)), node.infoset_id.value);
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

    for (std::uint32_t node_idx = 0; node_idx < graph.node_meta.size(); ++node_idx) {
        const auto& node = graph.node_meta[node_idx];
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
        EXPECT_NEAR(meta.terminal_utility[0], node.terminal_utility[0], 1e-12);
        EXPECT_NEAR(meta.terminal_utility[1], node.terminal_utility[1], 1e-12);

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

    EXPECT_EQ(graph.forward_order.size(), graph.node_meta.size());
    EXPECT_EQ(graph.reverse_order.size(), graph.node_meta.size());
    EXPECT_EQ(graph.node_depths.size(), graph.node_meta.size());
    EXPECT_EQ(graph.depth_order.size(), graph.node_meta.size());
    EXPECT_EQ(graph.street_order.size(), graph.node_meta.size());

    std::vector<std::uint32_t> forward_pos(graph.node_meta.size(), 0);
    for (std::uint32_t i = 0; i < graph.forward_order.size(); ++i) {
        forward_pos[graph.forward_order[i]] = i;
    }

    std::vector<bool> seen_forward(graph.node_meta.size(), false);
    for (const auto node_idx : graph.forward_order) {
        EXPECT_TRUE(node_idx < graph.node_meta.size());
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
    for (std::uint32_t i = 0; i < graph.node_meta.size(); ++i) {
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
    EXPECT_EQ(depth_total, static_cast<std::uint32_t>(graph.node_meta.size()));

    std::uint32_t street_total = 0;
    for (std::size_t street = 0; street < graph.street_slices.size(); ++street) {
        const auto slice = graph.street_slices[street];
        street_total += slice.count;
        for (std::uint32_t i = 0; i < slice.count; ++i) {
            const auto node_idx = graph.street_order[slice.begin + i];
            EXPECT_EQ(static_cast<std::size_t>(graph.node_meta[node_idx].street), street);
        }
    }
    EXPECT_EQ(street_total, static_cast<std::uint32_t>(graph.node_meta.size()));
}

TEST_CASE(hunl_flat_graph_precomputes_compact_terminal_tables) {
    const auto config = std::make_shared<const core::HUNLConfig>(core::default_tiny_subgame());
    const auto graph = core::HUNLFlatSolveGraph::build(config);

    EXPECT_EQ(graph.terminal_nodes.size(), graph.terminal_node_values.size());
    EXPECT_EQ(graph.fold_terminal_nodes.size(), graph.fold_terminal_values.size());
    EXPECT_EQ(graph.showdown_terminal_nodes.size(), graph.showdown_terminal_values.size());

    for (std::size_t i = 0; i < graph.terminal_nodes.size(); ++i) {
        const auto node_idx = graph.terminal_nodes[i];
        EXPECT_TRUE(node_idx < graph.node_meta.size());
        const auto& meta = graph.node_meta[node_idx];
        EXPECT_TRUE(
            meta.type == core::HUNLFlatNodeType::TerminalFold ||
            meta.type == core::HUNLFlatNodeType::TerminalShowdown);
        EXPECT_NEAR(graph.terminal_node_values[i], meta.terminal_utility[0], 1e-12);
    }
}

TEST_CASE(hunl_flat_graph_direct_builder_matches_tree_builder_on_small_graphs) {
    auto config_value = core::default_tiny_subgame();
    config_value.depth_limit_plies = 3;
    const auto config = std::make_shared<const core::HUNLConfig>(config_value);

    const auto tree = core::HUNLTree::build(config);
    const auto expected = core::HUNLFlatSolveGraph::build(tree);
    const auto actual = core::HUNLFlatSolveGraph::build(config);

    EXPECT_EQ(actual.root, expected.root);
    EXPECT_EQ(actual.max_depth, expected.max_depth);
    EXPECT_EQ(actual.max_actions, expected.max_actions);
    EXPECT_EQ(actual.children, expected.children);
    EXPECT_EQ(actual.actions, expected.actions);
    EXPECT_EQ(actual.terminal_nodes, expected.terminal_nodes);
    EXPECT_EQ(actual.terminal_node_values, expected.terminal_node_values);
    EXPECT_EQ(actual.fold_terminal_nodes, expected.fold_terminal_nodes);
    EXPECT_EQ(actual.fold_terminal_values, expected.fold_terminal_values);
    EXPECT_EQ(actual.showdown_terminal_nodes, expected.showdown_terminal_nodes);
    EXPECT_EQ(actual.showdown_terminal_values, expected.showdown_terminal_values);
    EXPECT_EQ(actual.forward_order, expected.forward_order);
    EXPECT_EQ(actual.reverse_order, expected.reverse_order);
    EXPECT_EQ(actual.node_depths, expected.node_depths);
    EXPECT_EQ(actual.depth_order, expected.depth_order);
    EXPECT_EQ(actual.street_order, expected.street_order);
    EXPECT_EQ(actual.infoset_debug_keys, expected.infoset_debug_keys);
    EXPECT_EQ(actual.infoset_nodes, expected.infoset_nodes);
    EXPECT_EQ(actual.chance_outcomes.size(), expected.chance_outcomes.size());
    EXPECT_EQ(actual.infosets.size(), expected.infosets.size());
    EXPECT_EQ(actual.node_meta.size(), expected.node_meta.size());

    EXPECT_EQ(actual.depth_slices.size(), expected.depth_slices.size());
    for (std::size_t i = 0; i < actual.depth_slices.size(); ++i) {
        EXPECT_EQ(actual.depth_slices[i].begin, expected.depth_slices[i].begin);
        EXPECT_EQ(actual.depth_slices[i].count, expected.depth_slices[i].count);
    }

    EXPECT_EQ(actual.depth_worker_ranges.size(), expected.depth_worker_ranges.size());
    for (std::size_t depth = 0; depth < actual.depth_worker_ranges.size(); ++depth) {
        EXPECT_EQ(actual.depth_worker_ranges[depth].size(), expected.depth_worker_ranges[depth].size());
        for (std::size_t i = 0; i < actual.depth_worker_ranges[depth].size(); ++i) {
            EXPECT_EQ(actual.depth_worker_ranges[depth][i].begin, expected.depth_worker_ranges[depth][i].begin);
            EXPECT_EQ(actual.depth_worker_ranges[depth][i].end, expected.depth_worker_ranges[depth][i].end);
        }
    }

    for (std::size_t i = 0; i < actual.street_slices.size(); ++i) {
        EXPECT_EQ(actual.street_slices[i].begin, expected.street_slices[i].begin);
        EXPECT_EQ(actual.street_slices[i].count, expected.street_slices[i].count);
    }

    for (std::size_t i = 0; i < actual.chance_outcomes.size(); ++i) {
        const auto& lhs = actual.chance_outcomes[i];
        const auto& rhs = expected.chance_outcomes[i];
        EXPECT_EQ(lhs.action, rhs.action);
        EXPECT_EQ(lhs.child, rhs.child);
        EXPECT_NEAR(lhs.probability, rhs.probability, 1e-12);
    }

    for (std::size_t i = 0; i < actual.infosets.size(); ++i) {
        const auto& lhs = actual.infosets[i];
        const auto& rhs = expected.infosets[i];
        EXPECT_EQ(lhs.id.value, rhs.id.value);
        EXPECT_EQ(lhs.node_begin, rhs.node_begin);
        EXPECT_EQ(lhs.node_count, rhs.node_count);
        EXPECT_EQ(lhs.board.cards, rhs.board.cards);
        EXPECT_EQ(lhs.board.count, rhs.board.count);
        EXPECT_EQ(lhs.debug_key_index, rhs.debug_key_index);
        EXPECT_EQ(lhs.player, rhs.player);
        EXPECT_EQ(lhs.street, rhs.street);
        EXPECT_EQ(lhs.action_count, rhs.action_count);
    }

    for (std::size_t i = 0; i < actual.node_meta.size(); ++i) {
        const auto& lhs = actual.node_meta[i];
        const auto& rhs = expected.node_meta[i];
        EXPECT_EQ(lhs.child_begin, rhs.child_begin);
        EXPECT_EQ(lhs.child_count, rhs.child_count);
        EXPECT_EQ(lhs.action_begin, rhs.action_begin);
        EXPECT_EQ(lhs.chance_begin, rhs.chance_begin);
        EXPECT_EQ(lhs.chance_count, rhs.chance_count);
        EXPECT_EQ(lhs.infoset_id.value, rhs.infoset_id.value);
        EXPECT_EQ(lhs.contributions, rhs.contributions);
        EXPECT_NEAR(lhs.terminal_utility[0], rhs.terminal_utility[0], 1e-12);
        EXPECT_NEAR(lhs.terminal_utility[1], rhs.terminal_utility[1], 1e-12);
        EXPECT_EQ(lhs.board.cards, rhs.board.cards);
        EXPECT_EQ(lhs.board.count, rhs.board.count);
        EXPECT_EQ(lhs.terminal_kind.tag, rhs.terminal_kind.tag);
        EXPECT_EQ(lhs.terminal_kind.winner, rhs.terminal_kind.winner);
        EXPECT_EQ(lhs.terminal_kind.contribution_loss, rhs.terminal_kind.contribution_loss);
        EXPECT_EQ(lhs.terminal_kind.board_complete, rhs.terminal_kind.board_complete);
        EXPECT_EQ(lhs.player, rhs.player);
        EXPECT_EQ(lhs.type, rhs.type);
        EXPECT_EQ(lhs.street, rhs.street);
        EXPECT_EQ(lhs.action_count, rhs.action_count);
        EXPECT_EQ(lhs.has_infoset, rhs.has_infoset);
    }
}

TEST_CASE(hunl_flat_graph_collapses_bucketed_public_chance_but_preserves_exact_expansion) {
    core::HUNLConfig explicit_cfg;
    explicit_cfg.starting_stack = 1000;
    explicit_cfg.starting_street = core::Street::Flop;
    explicit_cfg.initial_board = {
        core::card_to_int(14, 0), core::card_to_int(13, 0), core::card_to_int(7, 0)};
    explicit_cfg.initial_pot = 2000;
    explicit_cfg.initial_contributions = {1000, 1000};
    explicit_cfg.initial_hole_cards = std::array<std::array<std::uint8_t, 2>, 2>{{
        {core::card_to_int(12, 1), core::card_to_int(11, 2)},
        {core::card_to_int(10, 1), core::card_to_int(9, 2)},
    }};
    explicit_cfg.flat_solve_mode = core::HUNLFlatSolveMode::ExplicitHand;

    auto bucketed_cfg = explicit_cfg;
    bucketed_cfg.flat_solve_mode = core::HUNLFlatSolveMode::Auto;
    bucketed_cfg.abstraction_path = "test-abstraction.bin";

    const auto explicit_graph =
        core::HUNLFlatSolveGraph::build(std::make_shared<const core::HUNLConfig>(explicit_cfg));
    const auto bucketed_graph =
        core::HUNLFlatSolveGraph::build(std::make_shared<const core::HUNLConfig>(bucketed_cfg));

    const auto find_turn_public_chance = [](const core::HUNLFlatSolveGraph& graph) -> std::uint32_t {
        for (std::uint32_t node_idx = 0; node_idx < graph.node_meta.size(); ++node_idx) {
            const auto& meta = graph.node_meta[node_idx];
            if (meta.type != core::HUNLFlatNodeType::Chance) {
                continue;
            }
            if (meta.street != core::Street::Turn) {
                continue;
            }
            if (meta.board.count != 3 || meta.chance_count <= 1) {
                continue;
            }
            return node_idx;
        }
        return static_cast<std::uint32_t>(graph.node_meta.size());
    };

    const auto explicit_chance_idx = find_turn_public_chance(explicit_graph);
    const auto bucketed_chance_idx = find_turn_public_chance(bucketed_graph);
    EXPECT_TRUE(explicit_chance_idx < explicit_graph.node_meta.size());
    EXPECT_TRUE(bucketed_chance_idx < bucketed_graph.node_meta.size());

    const auto& explicit_chance = explicit_graph.node_meta[explicit_chance_idx];
    const auto& bucketed_chance = bucketed_graph.node_meta[bucketed_chance_idx];
    EXPECT_EQ(explicit_chance.type, core::HUNLFlatNodeType::Chance);
    EXPECT_EQ(bucketed_chance.type, core::HUNLFlatNodeType::Chance);
    EXPECT_EQ(explicit_chance.child_count, explicit_chance.chance_count);
    EXPECT_TRUE(bucketed_chance.chance_count < explicit_chance.chance_count);
    EXPECT_EQ(bucketed_chance.child_count, bucketed_chance.chance_count);

    std::uint32_t multiplicity_sum = 0;
    double probability_sum = 0.0;
    for (std::size_t i = 0; i < bucketed_chance.chance_count; ++i) {
        const auto& outcome = bucketed_graph.chance_outcomes[bucketed_chance.chance_begin + i];
        multiplicity_sum += outcome.multiplicity;
        probability_sum += outcome.probability;
        EXPECT_TRUE(outcome.multiplicity >= 1U);
    }
    EXPECT_EQ(multiplicity_sum, explicit_chance.chance_count);
    EXPECT_NEAR(probability_sum, 1.0, 1e-12);

    for (std::size_t i = 0; i < explicit_chance.chance_count; ++i) {
        const auto& outcome = explicit_graph.chance_outcomes[explicit_chance.chance_begin + i];
        EXPECT_EQ(outcome.multiplicity, 1U);
    }
}
