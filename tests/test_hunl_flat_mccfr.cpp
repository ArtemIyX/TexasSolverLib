#include "solver/hunl_flat_expected_value.hpp"
#include "solver/hunl_flat_mccfr.hpp"
#include "test_harness.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

namespace {

core::HUNLFlatSolveGraph make_public_chance_conflict_graph() {
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
    graph.infoset_debug_keys = {"pcs-shared-infoset"};
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
    graph.node_meta[0].street = core::Street::Flop;

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

    graph.node_meta[3] = make_terminal_meta(1.0);
    graph.node_meta[4] = make_terminal_meta(3.0);
    graph.node_meta[5] = make_terminal_meta(4.0);
    graph.node_meta[6] = make_terminal_meta(0.0);

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

core::HUNLFlatSolveGraph make_external_sampling_graph() {
    core::HUNLFlatSolveGraph graph;
    graph.root = 0;
    graph.max_depth = 3;
    graph.max_actions = 2;

    graph.children = {
        1, 2,
        3, 4,
        5, 6,
        7, 8,
        9, 10,
        11, 12,
        13, 14,
    };
    graph.chance_outcomes = {
        core::HUNLFlatChanceOutcome{0, 0.5, 1},
        core::HUNLFlatChanceOutcome{1, 0.5, 2},
    };

    const auto player1_infoset = core::InfosetId{0};
    const auto player0_infoset = core::InfosetId{1};
    graph.infosets.push_back(core::HUNLFlatInfoset{
        player1_infoset,
        0,
        2,
        {},
        0,
        1,
        core::Street::Flop,
        2,
    });
    graph.infosets.push_back(core::HUNLFlatInfoset{
        player0_infoset,
        2,
        4,
        {},
        1,
        0,
        core::Street::Flop,
        2,
    });
    graph.infoset_debug_keys = {"player1-shared", "player0-shared"};
    graph.infoset_nodes = {1, 2, 3, 4, 5, 6};

    auto make_terminal_meta = [](double value) {
        core::HUNLFlatNodeMeta meta;
        meta.type = core::HUNLFlatNodeType::TerminalFold;
        meta.terminal_utility = {value, -value};
        meta.terminal_kind = core::TerminalKind::fold(1, 1);
        return meta;
    };

    graph.node_meta.resize(15);
    graph.node_meta[0].child_begin = 0;
    graph.node_meta[0].child_count = 2;
    graph.node_meta[0].chance_begin = 0;
    graph.node_meta[0].chance_count = 2;
    graph.node_meta[0].type = core::HUNLFlatNodeType::Chance;
    graph.node_meta[0].street = core::Street::Flop;

    for (std::uint32_t node_idx : {1U, 2U}) {
        auto& meta = graph.node_meta[node_idx];
        meta.child_begin = node_idx == 1 ? 2 : 4;
        meta.child_count = 2;
        meta.infoset_id = player1_infoset;
        meta.player = 1;
        meta.type = core::HUNLFlatNodeType::Decision;
        meta.street = core::Street::Flop;
        meta.action_count = 2;
        meta.has_infoset = true;
    }

    for (std::uint32_t node_idx : {3U, 4U, 5U, 6U}) {
        auto& meta = graph.node_meta[node_idx];
        meta.child_begin = 6 + (node_idx - 3U) * 2U;
        meta.child_count = 2;
        meta.infoset_id = player0_infoset;
        meta.player = 0;
        meta.type = core::HUNLFlatNodeType::Decision;
        meta.street = core::Street::Flop;
        meta.action_count = 2;
        meta.has_infoset = true;
    }

    graph.node_meta[7] = make_terminal_meta(3.0);
    graph.node_meta[8] = make_terminal_meta(1.0);
    graph.node_meta[9] = make_terminal_meta(2.0);
    graph.node_meta[10] = make_terminal_meta(0.0);
    graph.node_meta[11] = make_terminal_meta(4.0);
    graph.node_meta[12] = make_terminal_meta(2.0);
    graph.node_meta[13] = make_terminal_meta(1.0);
    graph.node_meta[14] = make_terminal_meta(-1.0);

    graph.depth_order = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14};
    graph.depth_slices = {
        core::HUNLFlatSlice{0, 1},
        core::HUNLFlatSlice{1, 2},
        core::HUNLFlatSlice{3, 4},
        core::HUNLFlatSlice{7, 8},
    };
    graph.node_depths = {0, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 3, 3, 3, 3};
    graph.forward_order = graph.depth_order;
    graph.reverse_order = {14, 13, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1, 0};
    graph.street_order = graph.depth_order;
    graph.street_slices[static_cast<std::size_t>(core::Street::Flop)] =
        core::HUNLFlatSlice{0, static_cast<std::uint32_t>(graph.node_meta.size())};
    return graph;
}

double root_value(const core::HUNLFlatMCCFR& solver) {
    const auto table = solver.export_average_strategy_table();
    const auto terminal_values = core::build_flat_terminal_value_table(solver.graph());
    return core::compute_flat_expected_value(solver.graph(), table.view(), &terminal_values)[0];
}

}  // namespace

TEST_CASE(hunl_flat_mccfr_same_seed_produces_identical_output) {
    const auto graph_a = make_public_chance_conflict_graph();
    const auto graph_b = make_public_chance_conflict_graph();

    core::HUNLFlatMCCFRConfig config;
    config.mode = core::HUNLFlatSamplingMode::PublicChance;
    config.seed = 17;
    config.traversals_per_iteration = 64;

    core::HUNLFlatMCCFR first(graph_a, {1, 1}, config);
    core::HUNLFlatMCCFR second(graph_b, {1, 1}, config);

    first.run_iterations(40);
    second.run_iterations(40);

    const auto exported_first = first.export_average_strategy();
    const auto exported_second = second.export_average_strategy();
    EXPECT_EQ(exported_first.size(), exported_second.size());
    for (const auto& [key, values] : exported_first) {
        const auto it = exported_second.find(key);
        EXPECT_TRUE(it != exported_second.end());
        EXPECT_EQ(values.size(), it->second.size());
        for (std::size_t i = 0; i < values.size(); ++i) {
            EXPECT_NEAR(values[i], it->second[i], 1e-12);
        }
    }
}

TEST_CASE(hunl_flat_mccfr_public_chance_sampling_moves_toward_exact_value_with_more_samples) {
    const auto low_graph = make_public_chance_conflict_graph();
    const auto high_graph = make_public_chance_conflict_graph();

    core::HUNLFlatMCCFRConfig low_config;
    low_config.mode = core::HUNLFlatSamplingMode::PublicChance;
    low_config.seed = 9;
    low_config.traversals_per_iteration = 4;

    core::HUNLFlatMCCFRConfig high_config = low_config;
    high_config.traversals_per_iteration = 64;

    core::HUNLFlatMCCFR low_sample_solver(low_graph, {1, 1}, low_config);
    core::HUNLFlatMCCFR high_sample_solver(high_graph, {1, 1}, high_config);

    low_sample_solver.run_iterations(25);
    high_sample_solver.run_iterations(250);

    constexpr double exact_root_value = 2.5;
    const auto low_error = std::abs(root_value(low_sample_solver) - exact_root_value);
    const auto high_error = std::abs(root_value(high_sample_solver) - exact_root_value);

    EXPECT_TRUE(high_error < low_error);
    EXPECT_TRUE(high_error < 0.2);
}

TEST_CASE(hunl_flat_mccfr_exports_average_strategy_in_exact_solver_shape) {
    const auto graph = make_public_chance_conflict_graph();

    core::HUNLFlatMCCFRConfig config;
    config.mode = core::HUNLFlatSamplingMode::PublicChance;
    config.seed = 5;
    config.traversals_per_iteration = 32;

    core::HUNLFlatMCCFR solver(graph, {2, 2}, config, core::HUNLFlatValueLayout::InfosetActionHand);
    solver.run_iterations(20);

    const auto exported = solver.export_average_strategy();
    EXPECT_EQ(exported.size(), graph.infosets.size());
    for (const auto& infoset : graph.infosets) {
        const auto it = exported.find(std::string(graph.infoset_key(infoset)));
        EXPECT_TRUE(it != exported.end());
        EXPECT_EQ(
            it->second.size(),
            static_cast<std::size_t>(infoset.action_count) *
                solver.infoset_table().meta().at(infoset.id.value).bucket_count);
    }
}

TEST_CASE(hunl_flat_mccfr_external_sampling_converges_in_direction_of_exact_strategy) {
    const auto exact_graph = make_external_sampling_graph();
    const auto external_graph = make_external_sampling_graph();

    core::HUNLFlatMCCFRConfig exact_config;
    exact_config.mode = core::HUNLFlatSamplingMode::Exact;
    exact_config.seed = 3;
    exact_config.traversals_per_iteration = 1;

    core::HUNLFlatMCCFRConfig external_config;
    external_config.mode = core::HUNLFlatSamplingMode::External;
    external_config.seed = 3;
    external_config.traversals_per_iteration = 64;

    core::HUNLFlatMCCFR exact_solver(exact_graph, {1, 1}, exact_config);
    core::HUNLFlatMCCFR external_solver(external_graph, {1, 1}, external_config);

    exact_solver.run_iterations(60);
    external_solver.run_iterations(240);

    const auto exact_exported = exact_solver.export_average_strategy();
    const auto external_exported = external_solver.export_average_strategy();
    const auto key = std::string(exact_solver.graph().infoset_key(core::InfosetId{1}));

    const auto exact_it = exact_exported.find(key);
    const auto external_it = external_exported.find(key);
    EXPECT_TRUE(exact_it != exact_exported.end());
    EXPECT_TRUE(external_it != external_exported.end());
    EXPECT_TRUE(exact_it->second.size() >= 2U);
    EXPECT_TRUE(external_it->second.size() >= 2U);
    EXPECT_TRUE(exact_it->second[0] > exact_it->second[1]);
    EXPECT_TRUE(external_it->second[0] > external_it->second[1]);
}

TEST_CASE(hunl_flat_mccfr_external_sampling_visits_fewer_nodes_than_public_chance_sampling) {
    const auto public_graph = make_external_sampling_graph();
    const auto external_graph = make_external_sampling_graph();

    core::HUNLFlatMCCFRConfig public_config;
    public_config.mode = core::HUNLFlatSamplingMode::PublicChance;
    public_config.seed = 21;
    public_config.traversals_per_iteration = 8;

    core::HUNLFlatMCCFRConfig external_config = public_config;
    external_config.mode = core::HUNLFlatSamplingMode::External;

    core::HUNLFlatMCCFR public_solver(public_graph, {1, 1}, public_config);
    core::HUNLFlatMCCFR external_solver(external_graph, {1, 1}, external_config);

    public_solver.run_iteration();
    external_solver.run_iteration();

    EXPECT_TRUE(
        external_solver.last_iteration_counters().nodes_visited <
        public_solver.last_iteration_counters().nodes_visited);
    EXPECT_TRUE(external_solver.last_iteration_counters().sampled_opponent_actions > 0U);
    EXPECT_TRUE(external_solver.last_iteration_counters().traversing_player_action_expansions > 0U);
}

TEST_CASE(hunl_flat_mccfr_external_sampling_keeps_regret_and_strategy_rows_finite) {
    const auto graph = make_external_sampling_graph();

    core::HUNLFlatMCCFRConfig config;
    config.mode = core::HUNLFlatSamplingMode::External;
    config.seed = 33;
    config.traversals_per_iteration = 32;

    core::HUNLFlatMCCFR solver(graph, {2, 2}, config);
    solver.run_iterations(120);

    for (const auto& meta : solver.infoset_table().meta()) {
        for (std::size_t offset = 0; offset < meta.value_count; ++offset) {
            EXPECT_TRUE(std::isfinite(solver.infoset_table().regret_value(meta.id, offset)));
            EXPECT_TRUE(std::isfinite(solver.infoset_table().strategy_sum_value(meta.id, offset)));
            EXPECT_TRUE(std::isfinite(solver.infoset_table().current_strategy_value(meta.id, offset)));
        }
    }
}
