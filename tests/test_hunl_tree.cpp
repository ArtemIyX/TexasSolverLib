#include "games/hunl_tree.hpp"
#include "test_harness.hpp"

#include <memory>

TEST_CASE(hunl_tree_builds_river_subgame_with_terminals) {
    const auto config = std::make_shared<const core::HUNLConfig>(core::default_tiny_subgame());
    const auto tree = core::HUNLTree::build(config);
    const auto& root = tree.nodes.at(tree.root);

    EXPECT_EQ(root.player, 1);
    EXPECT_TRUE(!root.legal_actions.empty());
    EXPECT_TRUE(tree.max_actions >= root.num_actions);

    bool fold_seen = false;
    bool showdown_seen = false;
    for (const auto& node : tree.nodes) {
        fold_seen = fold_seen || node.terminal_kind.tag == core::TerminalKindTag::Fold;
        showdown_seen = showdown_seen || node.terminal_kind.tag == core::TerminalKindTag::Showdown;
    }
    EXPECT_TRUE(fold_seen);
    EXPECT_TRUE(showdown_seen);
}

TEST_CASE(hunl_tree_node_count_is_bounded) {
    const auto config = std::make_shared<const core::HUNLConfig>(core::default_tiny_subgame());
    const auto tree = core::HUNLTree::build(config);
    EXPECT_TRUE(tree.nodes.size() > 5U);
    EXPECT_TRUE(tree.nodes.size() < 100000U);
}

TEST_CASE(hunl_tree_depth_limit_marks_cutoff_nodes_as_distinct_leaves) {
    auto config = core::default_tiny_subgame();
    config.depth_limit_plies = 1;
    const auto tree = core::HUNLTree::build(std::make_shared<const core::HUNLConfig>(config));

    bool depth_limited_seen = false;
    for (const auto& node : tree.nodes) {
        depth_limited_seen = depth_limited_seen || node.depth_limited_leaf;
        if (node.depth_limited_leaf) {
            EXPECT_EQ(node.terminal_kind.tag, core::TerminalKindTag::NonTerminal);
            EXPECT_TRUE(node.children.empty());
            EXPECT_TRUE(node.chance_children.empty());
        }
    }

    EXPECT_TRUE(depth_limited_seen);
}


