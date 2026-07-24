#include "solver/hunl_flat_expected_value.hpp"
#include "solver/hunl_flat_mccfr.hpp"
#include "solver/hunl_sampled_scheduler.hpp"
#include "test_harness.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <memory>
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

core::HUNLFlatMCCFRConfig active_delta_config(
    core::HUNLFlatSamplingMode mode = core::HUNLFlatSamplingMode::External) {
    core::HUNLFlatMCCFRConfig config;
    config.mode = mode;
    config.seed = 0xA11CEULL;
    config.traversals_per_iteration = 2;
    config.batch_size = 1;
    return config;
}

void expect_active_delta_batch(
    core::HUNLFlatSamplingMode mode,
    std::size_t workers = 1,
    bool sparse_storage = false) {
    auto config = active_delta_config(mode);
    config.use_sparse_storage = sparse_storage;
    core::HUNLFlatMCCFR solver(
        make_external_sampling_graph(), {1, 1}, config,
        core::HUNLFlatValueLayout::InfosetActionHand, workers);
    solver.run_iteration();
    const auto usage = solver.memory_usage();
    EXPECT_TRUE(solver.profile().active_infoset_samples > 0U);
    EXPECT_TRUE(usage.worker_scratch_bytes > 0U);
    EXPECT_TRUE(usage.worker_scratch_bytes < usage.total_bytes());
}

core::HUNLFlatSolveGraph make_root_decision_graph() {
    core::HUNLFlatSolveGraph graph;
    graph.root = 0;
    graph.max_depth = 1;
    graph.max_actions = 2;

    graph.children = {1, 2};
    graph.actions = {core::ACTION_CHECK, core::ACTION_BET_75};

    const auto root_infoset = core::InfosetId{0};
    graph.infosets.push_back(core::HUNLFlatInfoset{
        root_infoset,
        0,
        1,
        {},
        0,
        0,
        core::Street::Flop,
        2,
    });
    graph.infoset_debug_keys = {"root-decision"};
    graph.infoset_nodes = {0};

    auto make_terminal_meta = [](double value) {
        core::HUNLFlatNodeMeta meta;
        meta.type = core::HUNLFlatNodeType::TerminalFold;
        meta.terminal_utility = {value, -value};
        meta.terminal_kind = core::TerminalKind::fold(1, 1);
        return meta;
    };

    graph.node_meta.resize(3);
    graph.node_meta[0].child_begin = 0;
    graph.node_meta[0].child_count = 2;
    graph.node_meta[0].infoset_id = root_infoset;
    graph.node_meta[0].player = 0;
    graph.node_meta[0].type = core::HUNLFlatNodeType::Decision;
    graph.node_meta[0].street = core::Street::Flop;
    graph.node_meta[0].action_count = 2;
    graph.node_meta[0].has_infoset = true;

    graph.node_meta[1] = make_terminal_meta(1.0);
    graph.node_meta[2] = make_terminal_meta(-1.0);
    graph.node_meta[2].contributions[0] = 75;

    graph.depth_order = {0, 1, 2};
    graph.depth_slices = {
        core::HUNLFlatSlice{0, 1},
        core::HUNLFlatSlice{1, 2},
    };
    graph.node_depths = {0, 1, 1};
    graph.forward_order = graph.depth_order;
    graph.reverse_order = {2, 1, 0};
    graph.street_order = graph.depth_order;
    graph.street_slices[static_cast<std::size_t>(core::Street::Flop)] =
        core::HUNLFlatSlice{0, static_cast<std::uint32_t>(graph.node_meta.size())};
    return graph;
}

void expect_matching_deadline_batch_state(
    core::HUNLFlatSamplingMode mode,
    bool use_sparse_storage,
    std::size_t workers,
    bool update_both_players) {
    auto normal_graph = make_external_sampling_graph();
    auto batch_graph = make_external_sampling_graph();

    core::HUNLFlatMCCFRConfig config;
    config.mode = mode;
    config.seed = 0x5EEDU;
    config.traversals_per_iteration = 8;
    config.batch_size = 2;
    config.update_both_players = update_both_players;
    config.use_sparse_storage = use_sparse_storage;

    core::HUNLFlatMCCFR normal(normal_graph, {1, 1}, config, core::HUNLFlatValueLayout::InfosetActionHand, workers);
    core::HUNLFlatMCCFR batch_limited(batch_graph, {1, 1}, config, core::HUNLFlatValueLayout::InfosetActionHand, workers);

    constexpr std::uint32_t iteration_count = 3;
    const auto player_batches_per_iteration = update_both_players ? 2ULL : 1ULL;
    const auto subbatches_per_player_batch =
        static_cast<std::uint64_t>(config.traversals_per_iteration / config.batch_size);
    const auto expected_batches =
        static_cast<std::uint64_t>(iteration_count) * player_batches_per_iteration * subbatches_per_player_batch;

    normal.run_iterations(iteration_count);
    EXPECT_EQ(batch_limited.run_batches(expected_batches), expected_batches);

    EXPECT_EQ(normal.iterations(), iteration_count);
    EXPECT_EQ(batch_limited.iterations(), iteration_count);
    EXPECT_EQ(normal.profile().strategy_snapshot_rebuilds, iteration_count * player_batches_per_iteration);
    EXPECT_EQ(batch_limited.profile().strategy_snapshot_rebuilds, iteration_count * player_batches_per_iteration);
    EXPECT_EQ(normal.profile().traversals, batch_limited.profile().traversals);
    EXPECT_EQ(normal.total_counters().nodes_visited, batch_limited.total_counters().nodes_visited);

    const auto normal_export = normal.export_average_strategy();
    const auto batch_export = batch_limited.export_average_strategy();
    EXPECT_EQ(normal_export.size(), batch_export.size());
    for (const auto& [key, values] : normal_export) {
        const auto it = batch_export.find(key);
        EXPECT_TRUE(it != batch_export.end());
        EXPECT_EQ(values.size(), it->second.size());
        for (std::size_t i = 0; i < values.size(); ++i) {
            EXPECT_NEAR(values[i], it->second[i], 1e-12);
        }
    }

    const auto normal_root = normal.export_root_average_strategy();
    const auto batch_root = batch_limited.export_root_average_strategy();
    EXPECT_EQ(normal_root.actions.size(), batch_root.actions.size());
    for (std::size_t i = 0; i < normal_root.actions.size(); ++i) {
        EXPECT_EQ(normal_root.actions[i].action_index, batch_root.actions[i].action_index);
        EXPECT_NEAR(normal_root.actions[i].probability, batch_root.actions[i].probability, 1e-12);
    }
}

}  // namespace

#define DEADLINE_BATCH_EQUIVALENCE_CASE(name, mode_value, sparse_value, workers_value, both_players_value) \
    TEST_CASE(name) {                                                                         \
        expect_matching_deadline_batch_state(                                                \
            mode_value, sparse_value, workers_value, both_players_value);                    \
    }

DEADLINE_BATCH_EQUIVALENCE_CASE(hunl_flat_mccfr_deadline_batches_match_normal_exact_dense_one_worker_one_player, core::HUNLFlatSamplingMode::Exact, false, 1U, false)
DEADLINE_BATCH_EQUIVALENCE_CASE(hunl_flat_mccfr_deadline_batches_match_normal_exact_dense_one_worker_both_players, core::HUNLFlatSamplingMode::Exact, false, 1U, true)
DEADLINE_BATCH_EQUIVALENCE_CASE(hunl_flat_mccfr_deadline_batches_match_normal_exact_dense_three_workers_one_player, core::HUNLFlatSamplingMode::Exact, false, 3U, false)
DEADLINE_BATCH_EQUIVALENCE_CASE(hunl_flat_mccfr_deadline_batches_match_normal_exact_dense_three_workers_both_players, core::HUNLFlatSamplingMode::Exact, false, 3U, true)
DEADLINE_BATCH_EQUIVALENCE_CASE(hunl_flat_mccfr_deadline_batches_match_normal_exact_sparse_one_worker_one_player, core::HUNLFlatSamplingMode::Exact, true, 1U, false)
DEADLINE_BATCH_EQUIVALENCE_CASE(hunl_flat_mccfr_deadline_batches_match_normal_exact_sparse_one_worker_both_players, core::HUNLFlatSamplingMode::Exact, true, 1U, true)
DEADLINE_BATCH_EQUIVALENCE_CASE(hunl_flat_mccfr_deadline_batches_match_normal_exact_sparse_three_workers_one_player, core::HUNLFlatSamplingMode::Exact, true, 3U, false)
DEADLINE_BATCH_EQUIVALENCE_CASE(hunl_flat_mccfr_deadline_batches_match_normal_exact_sparse_three_workers_both_players, core::HUNLFlatSamplingMode::Exact, true, 3U, true)
DEADLINE_BATCH_EQUIVALENCE_CASE(hunl_flat_mccfr_deadline_batches_match_normal_public_chance_dense_one_worker_one_player, core::HUNLFlatSamplingMode::PublicChance, false, 1U, false)
DEADLINE_BATCH_EQUIVALENCE_CASE(hunl_flat_mccfr_deadline_batches_match_normal_public_chance_dense_one_worker_both_players, core::HUNLFlatSamplingMode::PublicChance, false, 1U, true)
DEADLINE_BATCH_EQUIVALENCE_CASE(hunl_flat_mccfr_deadline_batches_match_normal_public_chance_dense_three_workers_one_player, core::HUNLFlatSamplingMode::PublicChance, false, 3U, false)
DEADLINE_BATCH_EQUIVALENCE_CASE(hunl_flat_mccfr_deadline_batches_match_normal_public_chance_dense_three_workers_both_players, core::HUNLFlatSamplingMode::PublicChance, false, 3U, true)
DEADLINE_BATCH_EQUIVALENCE_CASE(hunl_flat_mccfr_deadline_batches_match_normal_public_chance_sparse_one_worker_one_player, core::HUNLFlatSamplingMode::PublicChance, true, 1U, false)
DEADLINE_BATCH_EQUIVALENCE_CASE(hunl_flat_mccfr_deadline_batches_match_normal_public_chance_sparse_one_worker_both_players, core::HUNLFlatSamplingMode::PublicChance, true, 1U, true)
DEADLINE_BATCH_EQUIVALENCE_CASE(hunl_flat_mccfr_deadline_batches_match_normal_public_chance_sparse_three_workers_one_player, core::HUNLFlatSamplingMode::PublicChance, true, 3U, false)
DEADLINE_BATCH_EQUIVALENCE_CASE(hunl_flat_mccfr_deadline_batches_match_normal_public_chance_sparse_three_workers_both_players, core::HUNLFlatSamplingMode::PublicChance, true, 3U, true)
DEADLINE_BATCH_EQUIVALENCE_CASE(hunl_flat_mccfr_deadline_batches_match_normal_external_dense_one_worker_one_player, core::HUNLFlatSamplingMode::External, false, 1U, false)
DEADLINE_BATCH_EQUIVALENCE_CASE(hunl_flat_mccfr_deadline_batches_match_normal_external_dense_one_worker_both_players, core::HUNLFlatSamplingMode::External, false, 1U, true)
DEADLINE_BATCH_EQUIVALENCE_CASE(hunl_flat_mccfr_deadline_batches_match_normal_external_dense_three_workers_one_player, core::HUNLFlatSamplingMode::External, false, 3U, false)
DEADLINE_BATCH_EQUIVALENCE_CASE(hunl_flat_mccfr_deadline_batches_match_normal_external_dense_three_workers_both_players, core::HUNLFlatSamplingMode::External, false, 3U, true)
DEADLINE_BATCH_EQUIVALENCE_CASE(hunl_flat_mccfr_deadline_batches_match_normal_external_sparse_one_worker_one_player, core::HUNLFlatSamplingMode::External, true, 1U, false)
DEADLINE_BATCH_EQUIVALENCE_CASE(hunl_flat_mccfr_deadline_batches_match_normal_external_sparse_one_worker_both_players, core::HUNLFlatSamplingMode::External, true, 1U, true)
DEADLINE_BATCH_EQUIVALENCE_CASE(hunl_flat_mccfr_deadline_batches_match_normal_external_sparse_three_workers_one_player, core::HUNLFlatSamplingMode::External, true, 3U, false)
DEADLINE_BATCH_EQUIVALENCE_CASE(hunl_flat_mccfr_deadline_batches_match_normal_external_sparse_three_workers_both_players, core::HUNLFlatSamplingMode::External, true, 3U, true)

#undef DEADLINE_BATCH_EQUIVALENCE_CASE

void expect_mccfr_memory_report(
    core::HUNLFlatSamplingMode mode,
    bool sparse,
    std::size_t workers,
    std::uint32_t batch_size,
    core::HUNLFlatBaselineMode baseline) {
    auto graph = make_external_sampling_graph();
    core::HUNLFlatMCCFRConfig config;
    config.mode = mode;
    config.traversals_per_iteration = 8;
    config.batch_size = batch_size;
    config.use_sparse_storage = sparse;
    config.baseline_mode = baseline;
    core::HUNLFlatMCCFR solver(graph, {1, 1}, config, core::HUNLFlatValueLayout::InfosetActionHand, workers);
    const auto usage = solver.memory_usage();
    const auto snapshot = solver.export_root_snapshot();
    EXPECT_EQ(snapshot.memory_used_bytes, usage.total_bytes());
    EXPECT_TRUE(usage.graph_bytes > 0U);
    EXPECT_TRUE(usage.infoset_metadata_bytes > 0U);
    EXPECT_TRUE(usage.policy_cache_bytes > 0U);
    EXPECT_TRUE(usage.traversal_metadata_bytes > 0U);
    EXPECT_TRUE(usage.worker_scratch_bytes > 0U);
    EXPECT_TRUE(usage.total_bytes() >= usage.graph_bytes + usage.worker_scratch_bytes);
    if (sparse) {
        EXPECT_EQ(solver.sparse_storage().row_count(), 0U);
    } else {
        EXPECT_TRUE(usage.central_storage_bytes > 0U);
    }
}

#define MCCFR_MEMORY_CASE(name, mode_value, sparse_value, workers_value, batch_value, baseline_value) \
    TEST_CASE(name) { expect_mccfr_memory_report(mode_value, sparse_value, workers_value, batch_value, baseline_value); }
#define MCCFR_MEMORY_FAMILY(prefix, mode_value) \
    MCCFR_MEMORY_CASE(prefix##_dense_w1_b1_none, mode_value, false, 1U, 1U, core::HUNLFlatBaselineMode::None) \
    MCCFR_MEMORY_CASE(prefix##_dense_w2_b1_none, mode_value, false, 2U, 1U, core::HUNLFlatBaselineMode::None) \
    MCCFR_MEMORY_CASE(prefix##_dense_w4_b1_none, mode_value, false, 4U, 1U, core::HUNLFlatBaselineMode::None) \
    MCCFR_MEMORY_CASE(prefix##_dense_w1_b8_none, mode_value, false, 1U, 8U, core::HUNLFlatBaselineMode::None) \
    MCCFR_MEMORY_CASE(prefix##_dense_w2_b8_none, mode_value, false, 2U, 8U, core::HUNLFlatBaselineMode::None) \
    MCCFR_MEMORY_CASE(prefix##_dense_w4_b8_none, mode_value, false, 4U, 8U, core::HUNLFlatBaselineMode::None) \
    MCCFR_MEMORY_CASE(prefix##_sparse_w1_b1_none, mode_value, true, 1U, 1U, core::HUNLFlatBaselineMode::None) \
    MCCFR_MEMORY_CASE(prefix##_sparse_w2_b1_none, mode_value, true, 2U, 1U, core::HUNLFlatBaselineMode::None) \
    MCCFR_MEMORY_CASE(prefix##_sparse_w4_b1_none, mode_value, true, 4U, 1U, core::HUNLFlatBaselineMode::None) \
    MCCFR_MEMORY_CASE(prefix##_sparse_w1_b8_none, mode_value, true, 1U, 8U, core::HUNLFlatBaselineMode::None) \
    MCCFR_MEMORY_CASE(prefix##_sparse_w2_b8_none, mode_value, true, 2U, 8U, core::HUNLFlatBaselineMode::None) \
    MCCFR_MEMORY_CASE(prefix##_sparse_w4_b8_none, mode_value, true, 4U, 8U, core::HUNLFlatBaselineMode::None) \
    MCCFR_MEMORY_CASE(prefix##_dense_w1_b1_baseline, mode_value, false, 1U, 1U, core::HUNLFlatBaselineMode::MovingAverage) \
    MCCFR_MEMORY_CASE(prefix##_dense_w2_b1_baseline, mode_value, false, 2U, 1U, core::HUNLFlatBaselineMode::MovingAverage) \
    MCCFR_MEMORY_CASE(prefix##_dense_w4_b1_baseline, mode_value, false, 4U, 1U, core::HUNLFlatBaselineMode::MovingAverage) \
    MCCFR_MEMORY_CASE(prefix##_dense_w1_b8_baseline, mode_value, false, 1U, 8U, core::HUNLFlatBaselineMode::MovingAverage) \
    MCCFR_MEMORY_CASE(prefix##_dense_w2_b8_baseline, mode_value, false, 2U, 8U, core::HUNLFlatBaselineMode::MovingAverage) \
    MCCFR_MEMORY_CASE(prefix##_dense_w4_b8_baseline, mode_value, false, 4U, 8U, core::HUNLFlatBaselineMode::MovingAverage) \
    MCCFR_MEMORY_CASE(prefix##_sparse_w1_b1_baseline, mode_value, true, 1U, 1U, core::HUNLFlatBaselineMode::MovingAverage) \
    MCCFR_MEMORY_CASE(prefix##_sparse_w2_b1_baseline, mode_value, true, 2U, 1U, core::HUNLFlatBaselineMode::MovingAverage) \
    MCCFR_MEMORY_CASE(prefix##_sparse_w4_b1_baseline, mode_value, true, 4U, 1U, core::HUNLFlatBaselineMode::MovingAverage) \
    MCCFR_MEMORY_CASE(prefix##_sparse_w1_b8_baseline, mode_value, true, 1U, 8U, core::HUNLFlatBaselineMode::MovingAverage) \
    MCCFR_MEMORY_CASE(prefix##_sparse_w2_b8_baseline, mode_value, true, 2U, 8U, core::HUNLFlatBaselineMode::MovingAverage) \
    MCCFR_MEMORY_CASE(prefix##_sparse_w4_b8_baseline, mode_value, true, 4U, 8U, core::HUNLFlatBaselineMode::MovingAverage)

MCCFR_MEMORY_FAMILY(hunl_flat_mccfr_memory_exact, core::HUNLFlatSamplingMode::Exact)
MCCFR_MEMORY_FAMILY(hunl_flat_mccfr_memory_public_chance, core::HUNLFlatSamplingMode::PublicChance)
MCCFR_MEMORY_FAMILY(hunl_flat_mccfr_memory_external, core::HUNLFlatSamplingMode::External)

#undef MCCFR_MEMORY_FAMILY
#undef MCCFR_MEMORY_CASE

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

TEST_CASE(hunl_flat_mccfr_rejects_invalid_constructor_configs) {
    {
        core::HUNLFlatMCCFRConfig config;
        config.traversals_per_iteration = 0;
        EXPECT_THROW(
            core::HUNLFlatMCCFR(make_public_chance_conflict_graph(), {1, 1}, config),
            std::invalid_argument);
    }

    {
        core::HUNLFlatMCCFRConfig config;
        config.batch_size = 0;
        EXPECT_THROW(
            core::HUNLFlatMCCFR(make_public_chance_conflict_graph(), {1, 1}, config),
            std::invalid_argument);
    }

    {
        core::HUNLFlatMCCFRConfig config;
        config.as_epsilon = 1.5;
        EXPECT_THROW(
            core::HUNLFlatMCCFR(make_public_chance_conflict_graph(), {1, 1}, config),
            std::invalid_argument);
    }

    {
        core::HUNLFlatMCCFRConfig config;
        config.baseline_mode = core::HUNLFlatBaselineMode::DepthLimitedValueTable;
        EXPECT_THROW(
            core::HUNLFlatMCCFR(make_public_chance_conflict_graph(), {1, 1}, config),
            std::invalid_argument);
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

TEST_CASE(hunl_flat_mccfr_active_delta_arena_handles_external_sampling) {
    expect_active_delta_batch(core::HUNLFlatSamplingMode::External);
}

TEST_CASE(hunl_flat_mccfr_active_delta_arena_handles_public_chance_sampling) {
    expect_active_delta_batch(core::HUNLFlatSamplingMode::PublicChance);
}

TEST_CASE(hunl_flat_mccfr_active_delta_arena_handles_average_strategy_sampling) {
    expect_active_delta_batch(core::HUNLFlatSamplingMode::AverageStrategy);
}

TEST_CASE(hunl_flat_mccfr_active_delta_arena_handles_exact_sampling) {
    expect_active_delta_batch(core::HUNLFlatSamplingMode::Exact);
}

TEST_CASE(hunl_flat_mccfr_active_delta_arena_handles_two_workers) {
    expect_active_delta_batch(core::HUNLFlatSamplingMode::External, 2U);
}

TEST_CASE(hunl_flat_mccfr_active_delta_arena_handles_four_workers) {
    expect_active_delta_batch(core::HUNLFlatSamplingMode::External, 4U);
}

TEST_CASE(hunl_flat_mccfr_active_delta_arena_handles_sparse_central_storage) {
    expect_active_delta_batch(core::HUNLFlatSamplingMode::External, 1U, true);
}

TEST_CASE(hunl_flat_mccfr_active_delta_arena_counts_only_touched_infosets) {
    auto config = active_delta_config();
    config.traversals_per_iteration = 1;
    core::HUNLFlatMCCFR solver(
        make_sparse_sampling_visibility_graph(), {1, 1}, config);
    solver.run_iteration();
    EXPECT_TRUE(solver.profile().active_infoset_samples > 0U);
    EXPECT_TRUE(solver.profile().active_infoset_samples <= solver.graph().infosets.size());
}

TEST_CASE(hunl_flat_mccfr_active_delta_arena_restarts_cleanly_each_iteration) {
    auto config = active_delta_config();
    core::HUNLFlatMCCFR solver(make_external_sampling_graph(), {1, 1}, config);
    solver.run_iteration();
    const auto first_active = solver.profile().active_infoset_samples;
    const auto first_memory = solver.memory_usage().worker_scratch_bytes;
    solver.run_iteration();
    EXPECT_TRUE(solver.profile().active_infoset_samples > first_active);
    EXPECT_TRUE(solver.memory_usage().worker_scratch_bytes >= first_memory);
}

TEST_CASE(hunl_flat_mccfr_active_delta_arena_keeps_worker_memory_accounted) {
    auto config = active_delta_config();
    config.baseline_mode = core::HUNLFlatBaselineMode::MovingAverage;
    core::HUNLFlatMCCFR solver(make_external_sampling_graph(), {1, 1}, config, core::HUNLFlatValueLayout::InfosetActionHand, 2U);
    solver.run_iteration();
    const auto usage = solver.memory_usage();
    EXPECT_TRUE(usage.worker_scratch_bytes > 0U);
    EXPECT_TRUE(usage.total_bytes() >= usage.worker_scratch_bytes + usage.graph_bytes);
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

TEST_CASE(hunl_flat_mccfr_external_profile_tracks_branch_split_and_worker_aggregates) {
    const auto graph = make_external_sampling_graph();

    core::HUNLFlatMCCFRConfig config;
    config.mode = core::HUNLFlatSamplingMode::External;
    config.seed = 141;
    config.traversals_per_iteration = 24;
    config.batch_size = 6;

    core::HUNLFlatMCCFR solver(graph, {2, 2}, config, core::HUNLFlatValueLayout::InfosetActionHand, 3);
    solver.run_iterations(12);

    const auto& profile = solver.profile();
    const auto& counters = solver.total_counters();
    EXPECT_TRUE(profile.chance_seconds >= 0.0);
    EXPECT_TRUE(profile.opponent_sampled_seconds >= 0.0);
    EXPECT_TRUE(profile.traversing_player_seconds >= 0.0);
    EXPECT_TRUE(profile.row_writeback_seconds >= 0.0);
    EXPECT_TRUE(profile.average_strategy_seconds >= 0.0);
    EXPECT_TRUE(counters.chance_nodes_visited > 0U);
    EXPECT_TRUE(counters.opponent_sampled_decisions > 0U);
    EXPECT_TRUE(counters.traversing_player_full_expansion_decisions > 0U);
    EXPECT_EQ(counters.as_decision_nodes, 0U);

    std::uint64_t worker_chance_nodes = 0;
    std::uint64_t worker_decision_nodes = 0;
    std::uint64_t worker_opponent_decisions = 0;
    std::uint64_t worker_full_expansion_decisions = 0;
    std::uint64_t worker_actions_touched = 0;
    std::uint64_t worker_as_decision_nodes = 0;
    for (const auto& worker : profile.workers) {
        worker_chance_nodes += worker.chance_nodes_visited;
        worker_decision_nodes += worker.decision_nodes_visited;
        worker_opponent_decisions += worker.opponent_sampled_decisions;
        worker_full_expansion_decisions += worker.traversing_player_full_expansion_decisions;
        worker_actions_touched += worker.decision_actions_touched;
        worker_as_decision_nodes += worker.as_decision_nodes;
    }

    EXPECT_EQ(worker_chance_nodes, profile.chance_nodes_visited);
    EXPECT_EQ(worker_decision_nodes, profile.decision_nodes_visited);
    EXPECT_EQ(worker_opponent_decisions, profile.opponent_sampled_decisions);
    EXPECT_EQ(worker_full_expansion_decisions, profile.traversing_player_full_expansion_decisions);
    EXPECT_EQ(worker_actions_touched, profile.decision_actions_touched);
    EXPECT_EQ(worker_as_decision_nodes, profile.as_decision_nodes);
}

TEST_CASE(hunl_flat_mccfr_iterative_external_dense_path_matches_recursive_dense_path) {
    const auto recursive_graph = make_external_sampling_graph();
    const auto iterative_graph = make_external_sampling_graph();

    core::HUNLFlatMCCFRConfig recursive_config;
    recursive_config.mode = core::HUNLFlatSamplingMode::External;
    recursive_config.seed = 1234;
    recursive_config.traversals_per_iteration = 64;
    recursive_config.batch_size = 16;

    core::HUNLFlatMCCFRConfig iterative_config = recursive_config;
    iterative_config.use_iterative_external_dense_traversal = true;

    core::HUNLFlatMCCFR recursive_solver(
        recursive_graph,
        {2, 2},
        recursive_config,
        core::HUNLFlatValueLayout::InfosetActionHand,
        1,
        core::HUNLFlatStoragePrecision::Float64);
    core::HUNLFlatMCCFR iterative_solver(
        iterative_graph,
        {2, 2},
        iterative_config,
        core::HUNLFlatValueLayout::InfosetActionHand,
        1,
        core::HUNLFlatStoragePrecision::Float64);

    recursive_solver.run_iterations(30);
    iterative_solver.run_iterations(30);

    const auto recursive_exported = recursive_solver.export_average_strategy();
    const auto iterative_exported = iterative_solver.export_average_strategy();
    EXPECT_EQ(recursive_exported.size(), iterative_exported.size());
    for (const auto& [key, values] : recursive_exported) {
        const auto it = iterative_exported.find(key);
        EXPECT_TRUE(it != iterative_exported.end());
        EXPECT_EQ(values.size(), it->second.size());
        for (std::size_t i = 0; i < values.size(); ++i) {
            EXPECT_NEAR(values[i], it->second[i], 1e-12);
        }
    }

    EXPECT_EQ(recursive_solver.total_counters().nodes_visited, iterative_solver.total_counters().nodes_visited);
    EXPECT_EQ(
        recursive_solver.total_counters().sampled_opponent_actions,
        iterative_solver.total_counters().sampled_opponent_actions);
}

TEST_CASE(hunl_flat_mccfr_iterative_external_flag_is_ignored_when_variance_reduction_is_enabled) {
    const auto recursive_graph = make_external_sampling_graph();
    const auto guarded_graph = make_external_sampling_graph();

    core::HUNLFlatMCCFRConfig recursive_config;
    recursive_config.mode = core::HUNLFlatSamplingMode::External;
    recursive_config.seed = 12345;
    recursive_config.traversals_per_iteration = 32;
    recursive_config.batch_size = 8;
    recursive_config.baseline_mode = core::HUNLFlatBaselineMode::MovingAverage;

    core::HUNLFlatMCCFRConfig guarded_config = recursive_config;
    guarded_config.use_iterative_external_dense_traversal = true;

    core::HUNLFlatMCCFR recursive_solver(
        recursive_graph,
        {1, 1},
        recursive_config,
        core::HUNLFlatValueLayout::InfosetActionHand,
        1,
        core::HUNLFlatStoragePrecision::Float64);
    core::HUNLFlatMCCFR guarded_solver(
        guarded_graph,
        {1, 1},
        guarded_config,
        core::HUNLFlatValueLayout::InfosetActionHand,
        1,
        core::HUNLFlatStoragePrecision::Float64);

    recursive_solver.run_iterations(20);
    guarded_solver.run_iterations(20);

    const auto recursive_exported = recursive_solver.export_average_strategy();
    const auto guarded_exported = guarded_solver.export_average_strategy();
    EXPECT_EQ(recursive_exported.size(), guarded_exported.size());
    for (const auto& [key, values] : recursive_exported) {
        const auto it = guarded_exported.find(key);
        EXPECT_TRUE(it != guarded_exported.end());
        EXPECT_EQ(values.size(), it->second.size());
        for (std::size_t i = 0; i < values.size(); ++i) {
            EXPECT_NEAR(values[i], it->second[i], 1e-12);
        }
    }
}

TEST_CASE(hunl_flat_mccfr_iterative_external_flag_is_ignored_in_sparse_mode) {
    const auto recursive_graph = make_external_sampling_graph();
    const auto guarded_graph = make_external_sampling_graph();

    core::HUNLFlatMCCFRConfig recursive_config;
    recursive_config.mode = core::HUNLFlatSamplingMode::External;
    recursive_config.seed = 2222;
    recursive_config.traversals_per_iteration = 24;
    recursive_config.use_sparse_storage = true;
    recursive_config.keep_dense_validation_backend = true;

    core::HUNLFlatMCCFRConfig guarded_config = recursive_config;
    guarded_config.use_iterative_external_dense_traversal = true;

    core::HUNLFlatMCCFR recursive_solver(
        recursive_graph,
        {2, 2},
        recursive_config,
        core::HUNLFlatValueLayout::InfosetActionHand,
        1,
        core::HUNLFlatStoragePrecision::Float64);
    core::HUNLFlatMCCFR guarded_solver(
        guarded_graph,
        {2, 2},
        guarded_config,
        core::HUNLFlatValueLayout::InfosetActionHand,
        1,
        core::HUNLFlatStoragePrecision::Float64);

    recursive_solver.run_iterations(18);
    guarded_solver.run_iterations(18);

    const auto recursive_exported = recursive_solver.export_average_strategy();
    const auto guarded_exported = guarded_solver.export_average_strategy();
    EXPECT_EQ(recursive_exported.size(), guarded_exported.size());
    for (const auto& [key, values] : recursive_exported) {
        const auto it = guarded_exported.find(key);
        EXPECT_TRUE(it != guarded_exported.end());
        EXPECT_EQ(values.size(), it->second.size());
        for (std::size_t i = 0; i < values.size(); ++i) {
            EXPECT_NEAR(values[i], it->second[i], 1e-12);
        }
    }
}

TEST_CASE(hunl_sampled_scheduler_partition_deterministic_covers_each_trajectory_exactly_once) {
    {
        const auto batches = core::HUNLSampledScheduler::partition_deterministic(10, 3);
        EXPECT_EQ(batches.size(), 3U);
        EXPECT_EQ(batches[0].worker_index, 0U);
        EXPECT_EQ(batches[1].worker_index, 1U);
        EXPECT_EQ(batches[2].worker_index, 2U);
        EXPECT_EQ(batches[0].trajectories.begin, 0U);
        EXPECT_EQ(batches[0].trajectories.end, 4U);
        EXPECT_EQ(batches[1].trajectories.begin, 4U);
        EXPECT_EQ(batches[1].trajectories.end, 7U);
        EXPECT_EQ(batches[2].trajectories.begin, 7U);
        EXPECT_EQ(batches[2].trajectories.end, 10U);
    }

    {
        const auto batches = core::HUNLSampledScheduler::partition_deterministic(2, 5);
        EXPECT_EQ(batches.size(), 2U);
        std::uint64_t covered = 0;
        for (std::size_t worker_index = 0; worker_index < batches.size(); ++worker_index) {
            const auto& batch = batches[worker_index];
            EXPECT_EQ(batch.worker_index, worker_index);
            EXPECT_EQ(batch.trajectories.begin, covered);
            EXPECT_TRUE(batch.trajectories.end >= batch.trajectories.begin);
            covered = batch.trajectories.end;
        }
        EXPECT_EQ(covered, 2U);
    }

    {
        const auto batches = core::HUNLSampledScheduler::partition_deterministic(9, 0);
        EXPECT_EQ(batches.size(), 1U);
        EXPECT_EQ(batches[0].worker_index, 0U);
        EXPECT_EQ(batches[0].trajectories.begin, 0U);
        EXPECT_EQ(batches[0].trajectories.end, 9U);
    }
}

TEST_CASE(hunl_flat_mccfr_same_seed_matches_across_worker_counts) {
    const auto single_graph = make_external_sampling_graph();
    const auto multi_graph = make_external_sampling_graph();

    core::HUNLFlatMCCFRConfig config;
    config.mode = core::HUNLFlatSamplingMode::External;
    config.seed = 321;
    config.traversals_per_iteration = 30;
    config.batch_size = 8;

    core::HUNLFlatMCCFR single(single_graph, {1, 1}, config, core::HUNLFlatValueLayout::InfosetActionHand, 1);
    core::HUNLFlatMCCFR multi(multi_graph, {1, 1}, config, core::HUNLFlatValueLayout::InfosetActionHand, 4);

    single.run_iterations(18);
    multi.run_iterations(18);

    const auto single_exported = single.export_average_strategy();
    const auto multi_exported = multi.export_average_strategy();
    EXPECT_EQ(single_exported.size(), multi_exported.size());
    for (const auto& [key, values] : single_exported) {
        const auto it = multi_exported.find(key);
        EXPECT_TRUE(it != multi_exported.end());
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

TEST_CASE(hunl_flat_mccfr_profile_tracks_player_batch_rebuilds_and_worker_dispatch_structure) {
    const auto graph = make_external_sampling_graph();

    core::HUNLFlatMCCFRConfig config;
    config.mode = core::HUNLFlatSamplingMode::External;
    config.seed = 919;
    config.traversals_per_iteration = 10;
    config.batch_size = 4;
    config.update_both_players = true;

    constexpr std::uint32_t iterations = 5;
    constexpr std::size_t workers = 3;
    core::HUNLFlatMCCFR solver(graph, {1, 1}, config, core::HUNLFlatValueLayout::InfosetActionHand, workers);
    solver.run_iterations(iterations);

    const auto player_batches =
        static_cast<std::uint64_t>(iterations) * (config.update_both_players ? 2ULL : 1ULL);
    const auto subbatches_per_player_batch =
        (static_cast<std::uint64_t>(config.traversals_per_iteration) +
         static_cast<std::uint64_t>(config.batch_size) - 1ULL) /
        static_cast<std::uint64_t>(config.batch_size);
    const auto total_subbatches = player_batches * subbatches_per_player_batch;
    const auto expected_worker_executions = total_subbatches * static_cast<std::uint64_t>(workers);

    EXPECT_EQ(solver.profile().strategy_snapshot_rebuilds, player_batches);
    EXPECT_EQ(solver.profile().worker_batch_executions, expected_worker_executions);
    EXPECT_EQ(solver.profile().worker_wakeups, expected_worker_executions);
    EXPECT_EQ(solver.profile().workers.size(), workers);
    EXPECT_TRUE(solver.profile().active_infoset_samples >= total_subbatches);

    std::uint64_t summed_batch_executions = 0;
    for (const auto& worker : solver.profile().workers) {
        summed_batch_executions += worker.batch_executions;
    }
    EXPECT_EQ(summed_batch_executions, expected_worker_executions);
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

TEST_CASE(hunl_flat_mccfr_sparse_external_with_dense_validation_backend_matches_pure_dense_output) {
    const auto dense_graph = make_external_sampling_graph();
    const auto sparse_graph = make_external_sampling_graph();

    core::HUNLFlatMCCFRConfig dense_config;
    dense_config.mode = core::HUNLFlatSamplingMode::External;
    dense_config.seed = 313;
    dense_config.traversals_per_iteration = 32;

    core::HUNLFlatMCCFRConfig sparse_config = dense_config;
    sparse_config.use_sparse_storage = true;
    sparse_config.keep_dense_validation_backend = true;

    core::HUNLFlatMCCFR dense_solver(
        dense_graph,
        {2, 2},
        dense_config,
        core::HUNLFlatValueLayout::InfosetActionHand,
        1,
        core::HUNLFlatStoragePrecision::Float64);
    core::HUNLFlatMCCFR sparse_solver(
        sparse_graph,
        {2, 2},
        sparse_config,
        core::HUNLFlatValueLayout::InfosetActionHand,
        1,
        core::HUNLFlatStoragePrecision::Float64);

    dense_solver.run_iterations(25);
    sparse_solver.run_iterations(25);

    EXPECT_TRUE(sparse_solver.using_sparse_storage());
    EXPECT_TRUE(sparse_solver.sparse_storage().row_count() > 0U);
    const auto dense_exported = dense_solver.export_average_strategy();
    const auto sparse_exported = sparse_solver.export_average_strategy();
    EXPECT_EQ(dense_exported.size(), sparse_exported.size());
    for (const auto& [key, values] : dense_exported) {
        const auto it = sparse_exported.find(key);
        EXPECT_TRUE(it != sparse_exported.end());
        EXPECT_EQ(values.size(), it->second.size());
        for (std::size_t i = 0; i < values.size(); ++i) {
            EXPECT_NEAR(values[i], it->second[i], 1e-5);
        }
    }
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
    EXPECT_TRUE(solver.total_counters().as_decision_nodes > 0U);
    EXPECT_TRUE(solver.total_counters().as_actions_considered > 0U);
    EXPECT_TRUE(solver.total_counters().as_actions_sampled > 0U);
}

TEST_CASE(hunl_flat_mccfr_average_strategy_profile_tracks_as_branch_and_worker_aggregates) {
    const auto graph = make_wide_average_strategy_graph();

    core::HUNLFlatMCCFRConfig config;
    config.mode = core::HUNLFlatSamplingMode::AverageStrategy;
    config.seed = 606;
    config.traversals_per_iteration = 48;
    config.batch_size = 12;
    config.as_epsilon = 0.1;
    config.as_tau = 0.0;
    config.as_beta = 0.1;

    core::HUNLFlatMCCFR solver(graph, {1, 1}, config, core::HUNLFlatValueLayout::InfosetActionHand, 2);
    solver.run_iterations(30);

    const auto& profile = solver.profile();
    const auto& counters = solver.total_counters();
    EXPECT_TRUE(profile.average_strategy_seconds >= 0.0);
    EXPECT_TRUE(profile.row_writeback_seconds >= 0.0);
    EXPECT_TRUE(counters.as_decision_nodes > 0U);
    EXPECT_TRUE(counters.as_actions_considered >= counters.as_actions_sampled);
    EXPECT_TRUE(counters.as_actions_sampled >= counters.as_decision_nodes);

    std::uint64_t worker_as_decisions = 0;
    std::uint64_t worker_as_considered = 0;
    std::uint64_t worker_as_sampled = 0;
    std::uint64_t worker_forced = 0;
    for (const auto& worker : profile.workers) {
        worker_as_decisions += worker.as_decision_nodes;
        worker_as_considered += worker.as_actions_considered;
        worker_as_sampled += worker.as_actions_sampled;
        worker_forced += worker.as_forced_at_least_one_count;
    }

    EXPECT_EQ(worker_as_decisions, profile.as_decision_nodes);
    EXPECT_EQ(worker_as_considered, profile.as_actions_considered);
    EXPECT_EQ(worker_as_sampled, profile.as_actions_sampled);
    EXPECT_EQ(worker_forced, profile.as_forced_at_least_one_count);
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

TEST_CASE(hunl_flat_mccfr_public_chance_profile_does_not_record_opponent_or_as_specific_counters) {
    const auto graph = make_public_chance_conflict_graph();

    core::HUNLFlatMCCFRConfig config;
    config.mode = core::HUNLFlatSamplingMode::PublicChance;
    config.seed = 515;
    config.traversals_per_iteration = 16;
    config.update_both_players = false;

    core::HUNLFlatMCCFR solver(graph, {1, 1}, config, core::HUNLFlatValueLayout::InfosetActionHand, 1);
    solver.run_iterations(20);

    EXPECT_TRUE(solver.total_counters().chance_nodes_visited > 0U);
    EXPECT_EQ(solver.total_counters().opponent_sampled_decisions, 0U);
    EXPECT_EQ(solver.total_counters().sampled_opponent_actions, 0U);
    EXPECT_EQ(solver.total_counters().as_decision_nodes, 0U);
    EXPECT_EQ(solver.profile().opponent_sampled_decisions, 0U);
    EXPECT_EQ(solver.profile().as_decision_nodes, 0U);
}

TEST_CASE(hunl_flat_mccfr_merge_profile_aggregates_worker_breakdown_exactly) {
    const auto graph = make_external_sampling_graph();

    core::HUNLFlatMCCFRConfig config;
    config.mode = core::HUNLFlatSamplingMode::External;
    config.seed = 8080;
    config.traversals_per_iteration = 20;
    config.batch_size = 5;

    core::HUNLFlatMCCFR solver(graph, {2, 2}, config, core::HUNLFlatValueLayout::InfosetActionHand, 4);
    solver.run_iterations(10);

    double worker_merge_seconds = 0.0;
    std::uint64_t worker_active_infosets = 0;
    std::uint64_t worker_batch_executions = 0;
    for (const auto& worker : solver.profile().workers) {
        worker_merge_seconds += worker.merge_seconds;
        worker_active_infosets += worker.active_infosets;
        worker_batch_executions += worker.batch_executions;
    }

    EXPECT_NEAR(worker_merge_seconds, solver.profile().merge_seconds, 1e-12);
    EXPECT_EQ(worker_active_infosets, solver.profile().active_infoset_samples);
    EXPECT_EQ(worker_batch_executions, solver.profile().worker_batch_executions);
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
    EXPECT_EQ(visited_meta->last_discount_iter, 0U);
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
    EXPECT_TRUE(first_baseline.infoset_rows <= first.graph().infosets.size());
    EXPECT_TRUE(first_baseline.node_rows > 0U);
    EXPECT_TRUE(first_baseline.node_rows <= first.graph().node_meta.size());
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

TEST_CASE(hunl_flat_mccfr_exports_root_average_strategy_without_materializing_full_map) {
    const auto graph = make_root_decision_graph();

    core::HUNLFlatMCCFRConfig config;
    config.mode = core::HUNLFlatSamplingMode::PublicChance;
    config.seed = 77;
    config.traversals_per_iteration = 16;
    config.update_both_players = false;

    core::HUNLFlatMCCFR solver(graph, {1, 1}, config);
    solver.run_iterations(12);

    const auto root = solver.export_root_average_strategy();
    EXPECT_EQ(root.actions.size(), 2U);
    EXPECT_TRUE(root.actions[0].probability >= 0.0);
    EXPECT_TRUE(root.actions[1].probability >= 0.0);
    EXPECT_NEAR(root.actions[0].probability + root.actions[1].probability, 1.0, 1e-9);
}

TEST_CASE(hunl_flat_mccfr_sparse_root_export_is_uniform_before_any_samples) {
    const auto graph = make_root_decision_graph();

    core::HUNLFlatMCCFRConfig config;
    config.mode = core::HUNLFlatSamplingMode::External;
    config.seed = 101;
    config.traversals_per_iteration = 8;
    config.update_both_players = false;
    config.use_sparse_storage = true;

    core::HUNLFlatMCCFR solver(graph, {1, 1}, config, core::HUNLFlatValueLayout::InfosetActionHand, 1);
    const auto root = solver.export_root_average_strategy();

    EXPECT_EQ(root.actions.size(), 2U);
    EXPECT_NEAR(root.actions[0].probability, 0.5, 1e-9);
    EXPECT_NEAR(root.actions[1].probability, 0.5, 1e-9);
    EXPECT_EQ(solver.sparse_storage().row_count(), 0U);
}

TEST_CASE(hunl_flat_mccfr_deadline_mode_returns_latest_clean_root_snapshot_on_immediate_timeout) {
    const auto graph = make_public_chance_conflict_graph();

    core::HUNLFlatMCCFRConfig config;
    config.mode = core::HUNLFlatSamplingMode::PublicChance;
    config.seed = 88;
    config.traversals_per_iteration = 16;
    config.batch_size = 4;
    config.update_both_players = false;
    config.use_sparse_storage = true;

    core::HUNLFlatMCCFR solver(graph, {1, 1}, config, core::HUNLFlatValueLayout::InfosetActionHand, 1);
    const auto result = solver.solve_for(std::chrono::milliseconds{0});

    EXPECT_TRUE(!result.snapshots.empty());
    EXPECT_EQ(result.batches_completed, 0U);
    EXPECT_EQ(result.latest_snapshot.strategy.actions.size(), 0U);
    EXPECT_EQ(result.latest_snapshot.unique_infosets_touched, 0U);
    EXPECT_EQ(solver.sparse_storage().row_count(), 0U);
}

TEST_CASE(hunl_flat_mccfr_deadline_snapshot_reports_uniform_entropy_and_zero_delta_without_batches) {
    const auto graph = make_public_chance_conflict_graph();

    core::HUNLFlatMCCFRConfig config;
    config.mode = core::HUNLFlatSamplingMode::PublicChance;
    config.seed = 89;
    config.traversals_per_iteration = 16;
    config.batch_size = 4;
    config.update_both_players = false;
    config.use_sparse_storage = true;

    core::HUNLFlatMCCFR solver(graph, {1, 1}, config, core::HUNLFlatValueLayout::InfosetActionHand, 1);
    const auto result = solver.solve_for(std::chrono::milliseconds{-5});

    EXPECT_EQ(result.batches_completed, 0U);
    EXPECT_EQ(result.latest_snapshot.batches_completed, 0U);
    EXPECT_EQ(result.latest_snapshot.strategy.actions.size(), 0U);
    EXPECT_NEAR(result.latest_snapshot.action_entropy, 0.0, 1e-12);
    EXPECT_NEAR(result.latest_snapshot.action_probability_delta, 0.0, 1e-12);
    EXPECT_TRUE(result.latest_snapshot.memory_used_bytes > 0U);
}

TEST_CASE(hunl_flat_mccfr_root_export_contains_stable_action_descriptors) {
    const auto graph = make_root_decision_graph();
    core::HUNLFlatMCCFRConfig config;
    config.use_sparse_storage = true;
    core::HUNLFlatMCCFR solver(graph, {1, 1}, config);
    const auto root = solver.export_root_average_strategy();
    EXPECT_EQ(root.actions.size(), 2U);
    EXPECT_EQ(root.actions[0].action_id, core::ACTION_CHECK);
    EXPECT_EQ(root.actions[0].target_contribution, 0);
    EXPECT_EQ(root.actions[1].action_id, core::ACTION_BET_75);
    EXPECT_EQ(root.actions[1].target_contribution, 75);
    EXPECT_TRUE(root.actions[0].action_menu_id != 0U);
    EXPECT_EQ(root.actions[0].action_menu_id, root.actions[1].action_menu_id);
}

TEST_CASE(hunl_flat_mccfr_worker_exception_is_rethrown_by_coordinator) {
    const auto graph = make_external_sampling_graph();
    core::HUNLFlatMCCFRConfig config;
    config.mode = core::HUNLFlatSamplingMode::External;
    config.traversals_per_iteration = 2;
    config.batch_size = 1;
    config.test_throw_worker_index = 1;
    core::HUNLFlatMCCFR solver(graph, {1, 1}, config, core::HUNLFlatValueLayout::InfosetActionHand, 2);
    EXPECT_THROW(solver.run_batches(1), std::runtime_error);
}

TEST_CASE(hunl_flat_mccfr_rejects_depth_limited_graphs_without_shared_leaf_evaluator) {
    auto config = core::benchmark_turn_subgame();
    config.depth_limit_plies = 1;
    const auto graph = core::HUNLFlatSolveGraph::build(std::make_shared<const core::HUNLConfig>(config));
    EXPECT_THROW(core::HUNLFlatMCCFR(graph, {1, 1}), std::invalid_argument);
}

TEST_CASE(hunl_flat_mccfr_deadline_batch_driver_zero_budget_does_not_create_a_strategy_snapshot) {
    const auto graph = make_external_sampling_graph();
    core::HUNLFlatMCCFRConfig config;
    config.mode = core::HUNLFlatSamplingMode::External;
    config.traversals_per_iteration = 8;
    config.batch_size = 2;

    core::HUNLFlatMCCFR solver(graph, {1, 1}, config);
    EXPECT_EQ(solver.run_batches(0), 0U);
    EXPECT_EQ(solver.profile().strategy_snapshot_rebuilds, 0U);
    EXPECT_EQ(solver.profile().traversals, 0U);
    EXPECT_EQ(solver.iterations(), 0U);
}

TEST_CASE(hunl_flat_mccfr_deadline_batch_driver_rebuilds_once_for_all_subbatches_of_one_player) {
    const auto graph = make_external_sampling_graph();
    core::HUNLFlatMCCFRConfig config;
    config.mode = core::HUNLFlatSamplingMode::External;
    config.traversals_per_iteration = 8;
    config.batch_size = 2;
    config.update_both_players = false;

    core::HUNLFlatMCCFR solver(graph, {1, 1}, config);
    solver.run_batches(4);

    EXPECT_EQ(solver.profile().strategy_snapshot_rebuilds, 1U);
    EXPECT_EQ(solver.profile().traversals, 8U);
    EXPECT_EQ(solver.iterations(), 1U);
}

TEST_CASE(hunl_flat_mccfr_deadline_batch_driver_rebuilds_once_per_traversing_player) {
    const auto graph = make_external_sampling_graph();
    core::HUNLFlatMCCFRConfig config;
    config.mode = core::HUNLFlatSamplingMode::External;
    config.traversals_per_iteration = 8;
    config.batch_size = 2;
    config.update_both_players = true;

    core::HUNLFlatMCCFR solver(graph, {1, 1}, config);
    solver.run_batches(8);

    EXPECT_EQ(solver.profile().strategy_snapshot_rebuilds, 2U);
    EXPECT_EQ(solver.profile().traversals, 16U);
    EXPECT_EQ(solver.iterations(), 1U);
}

TEST_CASE(hunl_flat_mccfr_deadline_batch_driver_reaches_deep_regret_through_nonzero_opponent_action) {
    const auto graph = make_external_sampling_graph();
    core::HUNLFlatMCCFRConfig config;
    config.mode = core::HUNLFlatSamplingMode::External;
    config.seed = 17;
    config.traversals_per_iteration = 32;
    config.batch_size = 32;
    config.update_both_players = false;

    core::HUNLFlatMCCFR solver(graph, {1, 1}, config);
    solver.run_batches(1);

    const auto& deep_meta = solver.infoset_table().meta()[1];
    const auto first_regret = solver.infoset_table().regret_value(deep_meta.id, 0U);
    const auto second_regret = solver.infoset_table().regret_value(deep_meta.id, 1U);
    EXPECT_TRUE(std::abs(first_regret) > 1e-12 || std::abs(second_regret) > 1e-12);
    EXPECT_TRUE(solver.total_counters().opponent_sampled_decisions > 0U ||
                solver.last_iteration_counters().opponent_sampled_decisions > 0U);
}

TEST_CASE(hunl_flat_mccfr_deadline_batch_driver_resumes_remaining_subbatches_without_rebuilding_snapshot) {
    const auto graph = make_external_sampling_graph();
    core::HUNLFlatMCCFRConfig config;
    config.mode = core::HUNLFlatSamplingMode::External;
    config.traversals_per_iteration = 8;
    config.batch_size = 2;
    config.update_both_players = false;

    core::HUNLFlatMCCFR solver(graph, {1, 1}, config);
    solver.run_batches(1);
    EXPECT_EQ(solver.profile().strategy_snapshot_rebuilds, 1U);
    solver.run_batches(3);

    EXPECT_EQ(solver.profile().strategy_snapshot_rebuilds, 1U);
    EXPECT_EQ(solver.iterations(), 1U);
}

TEST_CASE(hunl_flat_mccfr_deadline_batch_driver_then_normal_iteration_preserves_current_player_snapshot) {
    const auto graph = make_external_sampling_graph();
    core::HUNLFlatMCCFRConfig config;
    config.mode = core::HUNLFlatSamplingMode::External;
    config.traversals_per_iteration = 8;
    config.batch_size = 2;
    config.update_both_players = false;

    core::HUNLFlatMCCFR solver(graph, {1, 1}, config);
    solver.run_batches(1);
    solver.run_iteration();

    EXPECT_EQ(solver.profile().strategy_snapshot_rebuilds, 1U);
    EXPECT_EQ(solver.iterations(), 1U);
}

TEST_CASE(hunl_flat_mccfr_deadline_batch_driver_starts_a_new_snapshot_after_a_completed_iteration) {
    const auto graph = make_external_sampling_graph();
    core::HUNLFlatMCCFRConfig config;
    config.mode = core::HUNLFlatSamplingMode::External;
    config.traversals_per_iteration = 8;
    config.batch_size = 2;
    config.update_both_players = false;

    core::HUNLFlatMCCFR solver(graph, {1, 1}, config);
    solver.run_batches(4);
    solver.run_batches(1);

    EXPECT_EQ(solver.profile().strategy_snapshot_rebuilds, 2U);
    EXPECT_EQ(solver.iterations(), 1U);
}

TEST_CASE(hunl_flat_mccfr_positive_deadline_uses_the_shared_snapshot_state_machine) {
    const auto graph = make_external_sampling_graph();
    core::HUNLFlatMCCFRConfig config;
    config.mode = core::HUNLFlatSamplingMode::External;
    config.seed = 99;
    config.traversals_per_iteration = 1;
    config.batch_size = 1;
    config.update_both_players = false;

    core::HUNLFlatMCCFR solver(graph, {1, 1}, config);
    const auto result = solver.solve_for(std::chrono::milliseconds{25});

    EXPECT_TRUE(result.batches_completed >= 1U);
    EXPECT_TRUE(result.latest_snapshot.batches_completed >= 1U);
    EXPECT_TRUE(solver.profile().strategy_snapshot_rebuilds >= 1U);
    EXPECT_TRUE(solver.profile().opponent_sampled_decisions > 0U);
}
