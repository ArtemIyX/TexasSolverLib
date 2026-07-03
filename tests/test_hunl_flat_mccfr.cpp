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

core::HUNLFlatSolveGraph make_sparse_sampling_visibility_graph() {
    core::HUNLFlatSolveGraph graph;
    graph.root = 0;
    graph.max_depth = 2;
    graph.max_actions = 2;

    graph.children = {3, 4, 3, 4};
    graph.chance_outcomes = {
        core::HUNLFlatChanceOutcome{0, 0.5, 1},
        core::HUNLFlatChanceOutcome{1, 0.5, 2},
    };

    const auto infoset_a = core::InfosetId{0};
    const auto infoset_b = core::InfosetId{1};
    graph.infosets.push_back(core::HUNLFlatInfoset{
        infoset_a,
        0,
        1,
        {},
        0,
        0,
        core::Street::Flop,
        2,
    });
    graph.infosets.push_back(core::HUNLFlatInfoset{
        infoset_b,
        1,
        1,
        {},
        1,
        0,
        core::Street::Flop,
        2,
    });
    graph.infoset_debug_keys = {"sparse-a", "sparse-b"};
    graph.infoset_nodes = {1, 2};

    auto make_terminal_meta = [](double value) {
        core::HUNLFlatNodeMeta meta;
        meta.type = core::HUNLFlatNodeType::TerminalFold;
        meta.terminal_utility = {value, -value};
        meta.terminal_kind = core::TerminalKind::fold(1, 1);
        return meta;
    };

    graph.node_meta.resize(5);
    graph.node_meta[0].child_begin = 0;
    graph.node_meta[0].child_count = 2;
    graph.node_meta[0].chance_begin = 0;
    graph.node_meta[0].chance_count = 2;
    graph.node_meta[0].type = core::HUNLFlatNodeType::Chance;
    graph.node_meta[0].street = core::Street::Flop;

    for (std::uint32_t node_idx : {1U, 2U}) {
        auto& meta = graph.node_meta[node_idx];
        meta.child_begin = node_idx == 1 ? 0 : 2;
        meta.child_count = 2;
        meta.infoset_id = node_idx == 1 ? infoset_a : infoset_b;
        meta.player = 0;
        meta.type = core::HUNLFlatNodeType::Decision;
        meta.street = core::Street::Flop;
        meta.action_count = 2;
        meta.has_infoset = true;
    }

    graph.node_meta[3] = make_terminal_meta(2.0);
    graph.node_meta[4] = make_terminal_meta(-1.0);

    graph.depth_order = {0, 1, 2, 3, 4};
    graph.depth_slices = {
        core::HUNLFlatSlice{0, 1},
        core::HUNLFlatSlice{1, 2},
        core::HUNLFlatSlice{3, 2},
    };
    graph.node_depths = {0, 1, 1, 2, 2};
    graph.forward_order = graph.depth_order;
    graph.reverse_order = {4, 3, 2, 1, 0};
    graph.street_order = graph.depth_order;
    graph.street_slices[static_cast<std::size_t>(core::Street::Flop)] =
        core::HUNLFlatSlice{0, static_cast<std::uint32_t>(graph.node_meta.size())};
    return graph;
}

core::HUNLFlatSolveGraph make_wide_average_strategy_graph() {
    core::HUNLFlatSolveGraph graph;
    graph.root = 0;
    graph.max_depth = 2;
    graph.max_actions = 8;

    graph.children = {
        1, 2,
        3, 4, 5, 6, 7, 8, 9, 10,
        11, 12, 13, 14, 15, 16, 17, 18,
    };
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
        8,
    });
    graph.infoset_debug_keys = {"wide-player0"};
    graph.infoset_nodes = {1, 2};

    auto make_terminal_meta = [](double value) {
        core::HUNLFlatNodeMeta meta;
        meta.type = core::HUNLFlatNodeType::TerminalFold;
        meta.terminal_utility = {value, -value};
        meta.terminal_kind = core::TerminalKind::fold(1, 1);
        return meta;
    };

    graph.node_meta.resize(19);
    graph.node_meta[0].child_begin = 0;
    graph.node_meta[0].child_count = 2;
    graph.node_meta[0].chance_begin = 0;
    graph.node_meta[0].chance_count = 2;
    graph.node_meta[0].type = core::HUNLFlatNodeType::Chance;
    graph.node_meta[0].street = core::Street::Flop;

    for (std::uint32_t node_idx : {1U, 2U}) {
        auto& meta = graph.node_meta[node_idx];
        meta.child_begin = node_idx == 1 ? 2 : 10;
        meta.child_count = 8;
        meta.infoset_id = shared_infoset;
        meta.player = 0;
        meta.type = core::HUNLFlatNodeType::Decision;
        meta.street = core::Street::Flop;
        meta.action_count = 8;
        meta.has_infoset = true;
    }

    const std::array<double, 8> first_branch = {5.0, 4.0, 3.0, 2.0, 1.0, 0.0, -1.0, -2.0};
    const std::array<double, 8> second_branch = {4.0, 3.0, 2.0, 1.0, 0.0, -1.0, -2.0, -3.0};
    for (std::size_t i = 0; i < first_branch.size(); ++i) {
        graph.node_meta[3U + static_cast<std::uint32_t>(i)] = make_terminal_meta(first_branch[i]);
        graph.node_meta[11U + static_cast<std::uint32_t>(i)] = make_terminal_meta(second_branch[i]);
    }

    graph.depth_order = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18};
    graph.depth_slices = {
        core::HUNLFlatSlice{0, 1},
        core::HUNLFlatSlice{1, 2},
        core::HUNLFlatSlice{3, 16},
    };
    graph.node_depths = {0, 1, 1, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2};
    graph.forward_order = graph.depth_order;
    graph.reverse_order = {18, 17, 16, 15, 14, 13, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1, 0};
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

double variance_from_moments(std::uint64_t count, double sum, double sq_sum) {
    if (count == 0U) {
        return 0.0;
    }
    const auto mean = sum / static_cast<double>(count);
    return std::max(0.0, sq_sum / static_cast<double>(count) - mean * mean);
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

TEST_CASE(hunl_flat_mccfr_static_partition_multiworker_matches_single_worker_with_strict_tolerance) {
    const auto single_graph = make_external_sampling_graph();
    const auto multi_graph = make_external_sampling_graph();

    core::HUNLFlatMCCFRConfig config;
    config.mode = core::HUNLFlatSamplingMode::External;
    config.seed = 77;
    config.traversals_per_iteration = 32;

    core::HUNLFlatMCCFR single_worker(single_graph, {1, 1}, config, core::HUNLFlatValueLayout::InfosetActionHand, 1);
    core::HUNLFlatMCCFR four_workers(multi_graph, {1, 1}, config, core::HUNLFlatValueLayout::InfosetActionHand, 4);

    single_worker.run_iterations(40);
    four_workers.run_iterations(40);

    const auto single_exported = single_worker.export_average_strategy();
    const auto multi_exported = four_workers.export_average_strategy();
    EXPECT_EQ(single_exported.size(), multi_exported.size());
    for (const auto& [key, values] : single_exported) {
        const auto it = multi_exported.find(key);
        EXPECT_TRUE(it != multi_exported.end());
        EXPECT_EQ(values.size(), it->second.size());
        for (std::size_t i = 0; i < values.size(); ++i) {
            EXPECT_NEAR(values[i], it->second[i], 1e-9);
        }
    }
}

TEST_CASE(hunl_flat_mccfr_multiworker_profile_exposes_merge_time_and_worker_breakdown) {
    const auto graph = make_external_sampling_graph();

    core::HUNLFlatMCCFRConfig config;
    config.mode = core::HUNLFlatSamplingMode::External;
    config.seed = 91;
    config.traversals_per_iteration = 16;

    core::HUNLFlatMCCFR solver(graph, {1, 1}, config, core::HUNLFlatValueLayout::InfosetActionHand, 3);
    solver.run_iterations(5);

    EXPECT_EQ(solver.worker_count(), 3U);
    EXPECT_EQ(solver.profile().workers.size(), 3U);
    EXPECT_TRUE(solver.profile().traversals == 5ULL * 16ULL * 2ULL);
    EXPECT_TRUE(solver.profile().merge_seconds >= 0.0);
    EXPECT_TRUE(solver.profile().traverse_seconds >= 0.0);

    std::uint64_t total_worker_traversals = 0;
    for (const auto& worker : solver.profile().workers) {
        EXPECT_TRUE(worker.traverse_seconds >= 0.0);
        EXPECT_TRUE(worker.merge_seconds >= 0.0);
        total_worker_traversals += worker.traversals;
    }
    EXPECT_EQ(total_worker_traversals, solver.profile().traversals);
}

TEST_CASE(hunl_flat_mccfr_multiworker_same_seed_is_deterministic) {
    const auto first_graph = make_external_sampling_graph();
    const auto second_graph = make_external_sampling_graph();

    core::HUNLFlatMCCFRConfig config;
    config.mode = core::HUNLFlatSamplingMode::External;
    config.seed = 123;
    config.traversals_per_iteration = 24;

    core::HUNLFlatMCCFR first(first_graph, {1, 1}, config, core::HUNLFlatValueLayout::InfosetActionHand, 4);
    core::HUNLFlatMCCFR second(second_graph, {1, 1}, config, core::HUNLFlatValueLayout::InfosetActionHand, 4);

    first.run_iterations(20);
    second.run_iterations(20);

    const auto first_exported = first.export_average_strategy();
    const auto second_exported = second.export_average_strategy();
    EXPECT_EQ(first_exported.size(), second_exported.size());
    for (const auto& [key, values] : first_exported) {
        const auto it = second_exported.find(key);
        EXPECT_TRUE(it != second_exported.end());
        EXPECT_EQ(values.size(), it->second.size());
        for (std::size_t i = 0; i < values.size(); ++i) {
            EXPECT_NEAR(values[i], it->second[i], 1e-12);
        }
    }
}

TEST_CASE(hunl_flat_mccfr_profile_accumulates_across_iterations_and_players) {
    const auto graph = make_external_sampling_graph();

    core::HUNLFlatMCCFRConfig config;
    config.mode = core::HUNLFlatSamplingMode::External;
    config.seed = 211;
    config.traversals_per_iteration = 10;
    config.update_both_players = true;

    core::HUNLFlatMCCFR solver(graph, {1, 1}, config, core::HUNLFlatValueLayout::InfosetActionHand, 2);
    solver.run_iterations(7);

    EXPECT_EQ(solver.iterations(), 7U);
    EXPECT_EQ(solver.profile().traversals, 7ULL * 10ULL * 2ULL);
    EXPECT_TRUE(solver.profile().strategy_seconds >= 0.0);
    EXPECT_TRUE(solver.profile().traverse_seconds >= 0.0);
    EXPECT_TRUE(solver.profile().merge_seconds >= 0.0);
}

TEST_CASE(hunl_flat_mccfr_default_layout_keeps_nonzero_offset_infoset_rows_normalized) {
    const auto graph = make_external_sampling_graph();

    core::HUNLFlatMCCFRConfig config;
    config.mode = core::HUNLFlatSamplingMode::External;
    config.seed = 57;
    config.traversals_per_iteration = 20;

    core::HUNLFlatMCCFR solver(graph, {2, 2}, config, core::HUNLFlatValueLayout::InfosetHandAction, 1);
    solver.run_iterations(40);

    const auto exported = solver.export_average_strategy();
    const auto key = std::string(solver.graph().infoset_key(core::InfosetId{1}));
    const auto it = exported.find(key);
    EXPECT_TRUE(it != exported.end());
    EXPECT_EQ(it->second.size(), 4U);
    EXPECT_TRUE(std::isfinite(it->second[0]));
    EXPECT_TRUE(std::isfinite(it->second[1]));
    EXPECT_TRUE(std::isfinite(it->second[2]));
    EXPECT_TRUE(std::isfinite(it->second[3]));
    EXPECT_NEAR(it->second[0] + it->second[1], 1.0, 1e-9);
    EXPECT_NEAR(it->second[2] + it->second[3], 1.0, 1e-9);
}

TEST_CASE(hunl_flat_mccfr_sparse_storage_allocates_rows_on_first_visit_and_exports_uniform_unvisited_rows) {
    const auto graph = make_sparse_sampling_visibility_graph();

    core::HUNLFlatMCCFRConfig config;
    config.mode = core::HUNLFlatSamplingMode::PublicChance;
    config.seed = 1;
    config.traversals_per_iteration = 1;
    config.update_both_players = false;
    config.use_sparse_storage = true;

    core::HUNLFlatMCCFR solver(graph, {2, 2}, config, core::HUNLFlatValueLayout::InfosetActionHand, 1);
    EXPECT_TRUE(solver.using_sparse_storage());
    EXPECT_EQ(solver.sparse_storage().row_count(), 0U);

    solver.run_iteration();

    EXPECT_EQ(solver.sparse_storage().row_count(), 1U);
    EXPECT_TRUE(solver.sparse_storage().memory_estimate().sparse_rows == 1U);

    const auto exported = solver.export_average_strategy();
    const auto it_a = exported.find("sparse-a");
    const auto it_b = exported.find("sparse-b");
    EXPECT_TRUE(it_a != exported.end());
    EXPECT_TRUE(it_b != exported.end());

    const auto visited = solver.sparse_storage().has_row(core::InfosetId{0}) ? it_a : it_b;
    const auto unvisited = solver.sparse_storage().has_row(core::InfosetId{0}) ? it_b : it_a;
    EXPECT_TRUE(visited->second[0] + visited->second[1] > 0.0);
    EXPECT_NEAR(unvisited->second[0], 0.5, 1e-9);
    EXPECT_NEAR(unvisited->second[1], 0.5, 1e-9);
    EXPECT_NEAR(unvisited->second[2], 0.5, 1e-9);
    EXPECT_NEAR(unvisited->second[3], 0.5, 1e-9);
}

TEST_CASE(hunl_flat_mccfr_sparse_storage_seeded_runs_are_deterministic) {
    const auto first_graph = make_external_sampling_graph();
    const auto second_graph = make_external_sampling_graph();

    core::HUNLFlatMCCFRConfig config;
    config.mode = core::HUNLFlatSamplingMode::External;
    config.seed = 404;
    config.traversals_per_iteration = 32;
    config.use_sparse_storage = true;

    core::HUNLFlatMCCFR first(first_graph, {2, 2}, config, core::HUNLFlatValueLayout::InfosetActionHand, 4);
    core::HUNLFlatMCCFR second(second_graph, {2, 2}, config, core::HUNLFlatValueLayout::InfosetActionHand, 4);

    first.run_iterations(30);
    second.run_iterations(30);

    EXPECT_EQ(first.sparse_storage().row_count(), second.sparse_storage().row_count());
    const auto first_exported = first.export_average_strategy();
    const auto second_exported = second.export_average_strategy();
    EXPECT_EQ(first_exported.size(), second_exported.size());
    for (const auto& [key, values] : first_exported) {
        const auto it = second_exported.find(key);
        EXPECT_TRUE(it != second_exported.end());
        EXPECT_EQ(values.size(), it->second.size());
        for (std::size_t i = 0; i < values.size(); ++i) {
            EXPECT_NEAR(values[i], it->second[i], 1e-12);
        }
    }
}

TEST_CASE(hunl_flat_mccfr_sparse_mode_can_keep_dense_validation_backend) {
    const auto graph = make_public_chance_conflict_graph();

    core::HUNLFlatMCCFRConfig config;
    config.mode = core::HUNLFlatSamplingMode::PublicChance;
    config.seed = 12;
    config.traversals_per_iteration = 4;
    config.update_both_players = false;
    config.use_sparse_storage = true;
    config.keep_dense_validation_backend = true;

    core::HUNLFlatMCCFR solver(graph, {1, 1}, config);
    EXPECT_TRUE(solver.using_sparse_storage());
    EXPECT_EQ(solver.infoset_table().meta().size(), graph.infosets.size());
}

TEST_CASE(hunl_flat_mccfr_average_strategy_sampling_converges_in_direction_of_best_action) {
    const auto graph = make_external_sampling_graph();

    core::HUNLFlatMCCFRConfig config;
    config.mode = core::HUNLFlatSamplingMode::AverageStrategy;
    config.seed = 505;
    config.traversals_per_iteration = 64;
    config.as_epsilon = 0.1;
    config.as_tau = 1.0;
    config.as_beta = 10.0;

    core::HUNLFlatMCCFR solver(graph, {1, 1}, config);
    solver.run_iterations(240);

    const auto exported = solver.export_average_strategy();
    const auto key = std::string(solver.graph().infoset_key(core::InfosetId{1}));
    const auto it = exported.find(key);
    EXPECT_TRUE(it != exported.end());
    EXPECT_TRUE(it->second.size() >= 2U);
    EXPECT_TRUE(it->second[0] > it->second[1]);
    EXPECT_TRUE(solver.total_counters().as_actions_considered > 0U);
    EXPECT_TRUE(solver.total_counters().as_actions_sampled > 0U);
}

TEST_CASE(hunl_flat_mccfr_average_strategy_sampling_reduces_action_expansions_on_wide_menu) {
    const auto external_graph = make_wide_average_strategy_graph();
    const auto average_graph = make_wide_average_strategy_graph();

    core::HUNLFlatMCCFRConfig external_config;
    external_config.mode = core::HUNLFlatSamplingMode::External;
    external_config.seed = 808;
    external_config.traversals_per_iteration = 64;

    core::HUNLFlatMCCFRConfig average_config = external_config;
    average_config.mode = core::HUNLFlatSamplingMode::AverageStrategy;
    average_config.as_epsilon = 0.1;
    average_config.as_tau = 0.0;
    average_config.as_beta = 0.1;

    core::HUNLFlatMCCFR external_solver(external_graph, {1, 1}, external_config);
    core::HUNLFlatMCCFR average_solver(average_graph, {1, 1}, average_config);

    external_solver.run_iterations(40);
    average_solver.run_iterations(40);

    EXPECT_TRUE(
        average_solver.total_counters().traversing_player_action_expansions <
        external_solver.total_counters().traversing_player_action_expansions);
    EXPECT_TRUE(average_solver.total_counters().as_actions_considered > 0U);
    EXPECT_TRUE(
        average_solver.total_counters().as_actions_sampled <
        average_solver.total_counters().as_actions_considered);
    EXPECT_TRUE(average_solver.average_strategy_sampling_ratio() > 0.0);
    EXPECT_TRUE(average_solver.average_strategy_sampling_ratio() < 1.0);
}

TEST_CASE(hunl_flat_mccfr_average_strategy_sampling_forces_at_least_one_action_when_needed) {
    const auto graph = make_wide_average_strategy_graph();

    core::HUNLFlatMCCFRConfig config;
    config.mode = core::HUNLFlatSamplingMode::AverageStrategy;
    config.seed = 909;
    config.traversals_per_iteration = 32;
    config.as_epsilon = 0.05;
    config.as_tau = 0.0;
    config.as_beta = 0.1;

    core::HUNLFlatMCCFR solver(graph, {1, 1}, config);
    solver.run_iterations(100);

    EXPECT_TRUE(solver.total_counters().as_actions_considered > 0U);
    EXPECT_TRUE(solver.total_counters().as_actions_sampled >= 32ULL * 100ULL);
    EXPECT_TRUE(
        solver.total_counters().as_forced_at_least_one_count <=
        solver.total_counters().as_actions_considered);
}

TEST_CASE(hunl_flat_mccfr_sampled_dcfr_keeps_small_external_game_stable) {
    const auto graph = make_external_sampling_graph();

    core::HUNLFlatMCCFRConfig config;
    config.mode = core::HUNLFlatSamplingMode::External;
    config.seed = 1001;
    config.traversals_per_iteration = 32;
    config.use_discounting = true;

    core::HUNLFlatMCCFR solver(graph, {1, 1}, config);
    solver.run_iterations(120);

    const auto exported = solver.export_average_strategy();
    const auto key = std::string(solver.graph().infoset_key(core::InfosetId{1}));
    const auto it = exported.find(key);
    EXPECT_TRUE(it != exported.end());
    EXPECT_TRUE(it->second.size() >= 2U);
    EXPECT_TRUE(std::isfinite(it->second[0]));
    EXPECT_TRUE(std::isfinite(it->second[1]));
    EXPECT_TRUE(it->second[0] > it->second[1]);
    EXPECT_TRUE(solver.profile().discount_seconds >= 0.0);
}

TEST_CASE(hunl_flat_mccfr_sampled_dcfr_tracks_sparse_last_discount_iter_for_visited_rows_only) {
    const auto graph = make_sparse_sampling_visibility_graph();

    core::HUNLFlatMCCFRConfig config;
    config.mode = core::HUNLFlatSamplingMode::PublicChance;
    config.seed = 1;
    config.traversals_per_iteration = 1;
    config.update_both_players = false;
    config.use_sparse_storage = true;
    config.use_discounting = true;

    core::HUNLFlatMCCFR solver(graph, {2, 2}, config, core::HUNLFlatValueLayout::InfosetActionHand, 1);
    solver.run_iteration();

    EXPECT_EQ(solver.sparse_storage().row_count(), 1U);
    const auto visited_id = solver.sparse_storage().has_row(core::InfosetId{0}) ? core::InfosetId{0} : core::InfosetId{1};
    const auto unvisited_id = visited_id.value == 0 ? core::InfosetId{1} : core::InfosetId{0};
    const auto* visited_meta = solver.sparse_storage().meta_for(visited_id);
    const auto* unvisited_meta = solver.sparse_storage().meta_for(unvisited_id);
    EXPECT_TRUE(visited_meta != nullptr);
    EXPECT_EQ(visited_meta->last_discount_iter, 1U);
    EXPECT_TRUE(unvisited_meta == nullptr);
}

TEST_CASE(hunl_flat_mccfr_sampled_dcfr_matches_vanilla_direction_on_small_game) {
    const auto vanilla_graph = make_external_sampling_graph();
    const auto dcfr_graph = make_external_sampling_graph();

    core::HUNLFlatMCCFRConfig vanilla_config;
    vanilla_config.mode = core::HUNLFlatSamplingMode::External;
    vanilla_config.seed = 2002;
    vanilla_config.traversals_per_iteration = 64;

    core::HUNLFlatMCCFRConfig dcfr_config = vanilla_config;
    dcfr_config.use_discounting = true;

    core::HUNLFlatMCCFR vanilla_solver(vanilla_graph, {1, 1}, vanilla_config);
    core::HUNLFlatMCCFR dcfr_solver(dcfr_graph, {1, 1}, dcfr_config);

    vanilla_solver.run_iterations(180);
    dcfr_solver.run_iterations(180);

    const auto vanilla_exported = vanilla_solver.export_average_strategy();
    const auto dcfr_exported = dcfr_solver.export_average_strategy();
    const auto key = std::string(vanilla_solver.graph().infoset_key(core::InfosetId{1}));
    const auto vanilla_it = vanilla_exported.find(key);
    const auto dcfr_it = dcfr_exported.find(key);
    EXPECT_TRUE(vanilla_it != vanilla_exported.end());
    EXPECT_TRUE(dcfr_it != dcfr_exported.end());
    EXPECT_TRUE(vanilla_it->second[0] > vanilla_it->second[1]);
    EXPECT_TRUE(dcfr_it->second[0] > dcfr_it->second[1]);
    EXPECT_TRUE(dcfr_solver.profile().discount_seconds <= dcfr_solver.profile().traverse_seconds + 1e-12);
}

TEST_CASE(hunl_flat_mccfr_variance_reduction_reduces_sampled_estimator_variance_on_public_chance_graph) {
    const auto graph = make_public_chance_conflict_graph();

    core::HUNLFlatMCCFRConfig config;
    config.mode = core::HUNLFlatSamplingMode::PublicChance;
    config.seed = 31337;
    config.traversals_per_iteration = 32;
    config.update_both_players = false;
    config.baseline_mode = core::HUNLFlatBaselineMode::MovingAverage;

    core::HUNLFlatMCCFR solver(graph, {1, 1}, config);
    solver.run_iterations(120);

    const auto& counters = solver.total_counters();
    const auto raw_variance =
        variance_from_moments(counters.variance_samples, counters.raw_estimate_sum, counters.raw_estimate_sq_sum);
    const auto corrected_variance = variance_from_moments(
        counters.variance_samples,
        counters.corrected_estimate_sum,
        counters.corrected_estimate_sq_sum);

    EXPECT_TRUE(counters.variance_samples > 0U);
    EXPECT_TRUE(raw_variance > 0.0);
    EXPECT_TRUE(corrected_variance < raw_variance);
    EXPECT_NEAR(raw_variance, solver.raw_estimator_variance(), 1e-12);
    EXPECT_NEAR(corrected_variance, solver.corrected_estimator_variance(), 1e-12);
}

TEST_CASE(hunl_flat_mccfr_variance_reduction_baselines_stay_sparse_and_deterministic) {
    const auto first_graph = make_sparse_sampling_visibility_graph();
    const auto second_graph = make_sparse_sampling_visibility_graph();

    core::HUNLFlatMCCFRConfig config;
    config.mode = core::HUNLFlatSamplingMode::PublicChance;
    config.seed = 5150;
    config.traversals_per_iteration = 1;
    config.update_both_players = false;
    config.use_sparse_storage = true;
    config.baseline_mode = core::HUNLFlatBaselineMode::MovingAverage;

    core::HUNLFlatMCCFR first(first_graph, {2, 2}, config, core::HUNLFlatValueLayout::InfosetActionHand, 1);
    core::HUNLFlatMCCFR second(second_graph, {2, 2}, config, core::HUNLFlatValueLayout::InfosetActionHand, 1);

    first.run_iterations(8);
    second.run_iterations(8);

    const auto first_baseline = first.baseline_stats();
    const auto second_baseline = second.baseline_stats();
    EXPECT_EQ(first_baseline.infoset_rows, 1U);
    EXPECT_EQ(first_baseline.node_rows, 1U);
    EXPECT_TRUE(first_baseline.bytes > 0U);
    EXPECT_EQ(first_baseline.infoset_rows, second_baseline.infoset_rows);
    EXPECT_EQ(first_baseline.node_rows, second_baseline.node_rows);
    EXPECT_EQ(first_baseline.bytes, second_baseline.bytes);

    const auto first_exported = first.export_average_strategy();
    const auto second_exported = second.export_average_strategy();
    EXPECT_EQ(first_exported.size(), second_exported.size());
    for (const auto& [key, values] : first_exported) {
        const auto it = second_exported.find(key);
        EXPECT_TRUE(it != second_exported.end());
        EXPECT_EQ(values.size(), it->second.size());
        for (std::size_t i = 0; i < values.size(); ++i) {
            EXPECT_NEAR(values[i], it->second[i], 1e-12);
        }
    }
}
