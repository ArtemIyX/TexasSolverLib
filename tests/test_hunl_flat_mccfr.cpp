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
