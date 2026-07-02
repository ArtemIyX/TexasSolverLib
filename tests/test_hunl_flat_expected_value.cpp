#include "games/hunl.hpp"
#include "games/hunl_flat_graph.hpp"
#include "games/hunl_tree.hpp"
#include "solver/hunl_flat_dcfr.hpp"
#include "solver/hunl_flat_expected_value.hpp"
#include "solver/solver.hpp"
#include "test_harness.hpp"

#include <array>
#include <memory>
#include <unordered_map>
#include <vector>

namespace {

core::HUNLFlatSolveGraph make_shared_infoset_chance_graph() {
    core::HUNLFlatSolveGraph graph;
    graph.root = 0;
    graph.max_depth = 2;
    graph.max_actions = 2;

    graph.children = {1, 2, 3, 4, 5, 6};
    graph.chance_outcomes = {
        core::HUNLFlatChanceOutcome{0, 0.5, 1},
        core::HUNLFlatChanceOutcome{1, 0.5, 2},
    };

    const auto shared_infoset = core::InfosetId{0};
    graph.infosets.push_back(core::HUNLFlatInfoset{
        shared_infoset,
        0,
        2,
        {},
        0,
        0,
        core::Street::Flop,
        2,
    });
    graph.infoset_debug_keys = {"shared-infoset"};
    graph.infoset_nodes = {1, 2};

    auto make_terminal_meta = [](double value) {
        core::HUNLFlatNodeMeta meta;
        meta.type = core::HUNLFlatNodeType::TerminalFold;
        meta.terminal_utility = {value, -value};
        meta.terminal_kind = core::TerminalKind::fold(1, 1);
        return meta;
    };

    graph.node_meta.resize(7);

    graph.node_meta[0].child_begin = 0;
    graph.node_meta[0].child_count = 2;
    graph.node_meta[0].chance_begin = 0;
    graph.node_meta[0].chance_count = 2;
    graph.node_meta[0].type = core::HUNLFlatNodeType::Chance;

    for (std::uint32_t node_idx : {1U, 2U}) {
        auto& meta = graph.node_meta[node_idx];
        meta.child_begin = node_idx == 1 ? 2 : 4;
        meta.child_count = 2;
        meta.infoset_id = shared_infoset;
        meta.player = 0;
        meta.type = core::HUNLFlatNodeType::Decision;
        meta.street = core::Street::Flop;
        meta.action_count = 2;
        meta.has_infoset = true;
    }

    graph.node_meta[3] = make_terminal_meta(3.0);
    graph.node_meta[4] = make_terminal_meta(-1.0);
    graph.node_meta[5] = make_terminal_meta(2.0);
    graph.node_meta[6] = make_terminal_meta(-2.0);

    graph.depth_order = {0, 1, 2, 3, 4, 5, 6};
    graph.depth_slices = {
        core::HUNLFlatSlice{0, 1},
        core::HUNLFlatSlice{1, 2},
        core::HUNLFlatSlice{3, 4},
    };
    graph.node_depths = {0, 1, 1, 2, 2, 2, 2};
    graph.forward_order = graph.depth_order;
    graph.reverse_order = {6, 5, 4, 3, 2, 1, 0};
    graph.street_order = graph.depth_order;
    graph.street_slices[static_cast<std::size_t>(core::Street::Flop)] =
        core::HUNLFlatSlice{0, static_cast<std::uint32_t>(graph.node_meta.size())};
    return graph;
}

}

TEST_CASE(hunl_flat_expected_value_matches_generic_recursive_path_for_action_rows) {
    const auto config = std::make_shared<const core::HUNLConfig>(core::default_tiny_subgame());
    const auto tree = core::HUNLTree::build(config);
    const auto graph = core::HUNLFlatSolveGraph::build(config);
    std::unordered_map<std::string, std::vector<double>> flat_strategy;
    std::unordered_map<std::string, std::vector<double>> generic_strategy;

    for (const auto& infoset : graph.infosets) {
        std::vector<double> probs(infoset.action_count, 0.0);
        if (!probs.empty()) {
            probs[0] = 1.0;
        }
        flat_strategy.emplace(std::string(graph.infoset_key(infoset)), probs);
    }

    for (const auto& node : tree.nodes) {
        if (!node.infoset_key.has_value() || node.legal_actions.empty()) {
            continue;
        }
        std::vector<double> probs(node.legal_actions.size(), 0.0);
        probs[0] = 1.0;
        generic_strategy.emplace(*node.infoset_key, std::move(probs));
    }

    const auto strategy_table = core::build_flat_average_strategy_table(graph, flat_strategy);
    const auto terminal_values = core::build_flat_terminal_value_table(graph);
    const auto flat_value = core::compute_flat_expected_value(graph, strategy_table.view(), &terminal_values);
    const auto generic_value = core::detail::expected_value(core::HUNLState::initial(config), generic_strategy);

    EXPECT_NEAR(flat_value[0], generic_value[0], 1e-12);
    EXPECT_NEAR(flat_value[1], generic_value[1], 1e-12);
}

TEST_CASE(hunl_flat_expected_value_averages_bucketed_export_rows_without_dynamic_state_walks) {
    const auto graph = make_shared_infoset_chance_graph();
    std::unordered_map<std::string, std::vector<double>> strategy;
    strategy.emplace("shared-infoset", std::vector<double>{1.0, 0.0, 0.0, 1.0});

    const auto strategy_table = core::build_flat_average_strategy_table(graph, strategy);
    const auto terminal_values = core::build_flat_terminal_value_table(graph);
    const auto value = core::compute_flat_expected_value(graph, strategy_table.view(), &terminal_values);

    EXPECT_NEAR(value[0], 0.5, 1e-12);
    EXPECT_NEAR(value[1], -0.5, 1e-12);
}

TEST_CASE(hunl_flat_dcfr_exports_infoset_indexed_average_strategy_table) {
    const auto config = std::make_shared<const core::HUNLConfig>(core::default_tiny_subgame());
    const auto graph = core::HUNLFlatSolveGraph::build(config);
    core::HUNLFlatDCFR solver(
        graph,
        {2, 2},
        core::HUNLFlatValueLayout::InfosetHandAction);

    solver.run_iteration();
    const auto table = solver.export_average_strategy_table();
    const auto view = table.view();

    EXPECT_TRUE(table.meta.size() == graph.infosets.size());
    EXPECT_TRUE(view.rows_by_infoset.size() == graph.infosets.size());
    EXPECT_TRUE(table.layout == core::HUNLFlatValueLayout::InfosetHandAction);

    for (const auto& infoset : graph.infosets) {
        const auto& meta = table.meta.at(infoset.id.value);
        EXPECT_TRUE(view.rows_by_infoset[infoset.id.value] != nullptr);
        EXPECT_TRUE(meta.action_count == infoset.action_count);
        EXPECT_TRUE(meta.value_count == meta.bucket_count * static_cast<std::uint32_t>(meta.action_count));
    }
}

TEST_CASE(hunl_flat_expected_value_benchmark_fast_path_matches_full_flat_value) {
    const auto config = std::make_shared<const core::HUNLConfig>(core::default_tiny_subgame());
    const auto graph = core::HUNLFlatSolveGraph::build(config);
    std::unordered_map<std::string, std::vector<double>> flat_strategy;

    for (const auto& infoset : graph.infosets) {
        std::vector<double> probs(infoset.action_count, 0.0);
        if (!probs.empty()) {
            probs[0] = 1.0;
        }
        flat_strategy.emplace(std::string(graph.infoset_key(infoset)), probs);
    }

    const auto strategy_table = core::build_flat_average_strategy_table(graph, flat_strategy);
    const auto terminal_values = core::build_flat_terminal_value_table(graph);
    const auto terminal_values_p0 = core::build_flat_terminal_value_table_p0_for_benchmark(graph);
    const auto full_value = core::compute_flat_expected_value(graph, strategy_table.view(), &terminal_values);
    const auto fast_value =
        core::compute_flat_expected_value_p0_benchmark(graph, strategy_table.view(), terminal_values_p0);

    EXPECT_NEAR(fast_value, full_value[0], 1e-12);
}
