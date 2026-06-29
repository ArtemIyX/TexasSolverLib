#include "solver/hunl_flat_dcfr.hpp"
#include "test_harness.hpp"

#include <array>
#include <memory>

TEST_CASE(hunl_flat_dcfr_runs_explicit_stage_iteration) {
    const auto config = std::make_shared<const core::HUNLConfig>(core::default_tiny_subgame());
    const auto graph = core::HUNLFlatSolveGraph::build(config);
    core::HUNLFlatDCFR solver(
        graph,
        {2, 3},
        core::HUNLFlatValueLayout::InfosetActionHand);

    solver.run_iterations(2);

    EXPECT_EQ(solver.iterations(), 2U);
    EXPECT_TRUE(solver.profile().strategy_seconds >= 0.0);
    EXPECT_TRUE(solver.profile().reach_seconds >= 0.0);
    EXPECT_TRUE(solver.profile().terminal_seconds >= 0.0);
    EXPECT_TRUE(solver.profile().backward_seconds >= 0.0);
    EXPECT_TRUE(solver.profile().regret_seconds >= 0.0);
    EXPECT_TRUE(solver.profile().average_strategy_seconds >= 0.0);
}

TEST_CASE(hunl_flat_dcfr_strategy_stage_writes_normalized_rows) {
    const auto config = std::make_shared<const core::HUNLConfig>(core::default_tiny_subgame());
    const auto graph = core::HUNLFlatSolveGraph::build(config);
    core::HUNLFlatDCFR solver(
        graph,
        {2, 2},
        core::HUNLFlatValueLayout::InfosetActionHand);

    auto& table = solver.infoset_table_mut();
    for (const auto& meta : table.meta()) {
        auto* regret = table.regret_mut(meta.id);
        for (std::size_t i = 0; i < meta.value_count; ++i) {
            regret[i] = 0.0;
        }
        if (meta.action_count >= 2 && meta.hand_count >= 1) {
            regret[0] = 2.0;
            regret[meta.hand_count] = 1.0;
        }
    }

    solver.run_iteration();

    for (const auto& meta : table.meta()) {
        const auto* strategy = table.current_strategy(meta.id);
        for (std::size_t h = 0; h < meta.hand_count; ++h) {
            double sum = 0.0;
            for (std::size_t a = 0; a < meta.action_count; ++a) {
                const auto idx = a * static_cast<std::size_t>(meta.hand_count) + h;
                EXPECT_TRUE(strategy[idx] >= 0.0);
                sum += strategy[idx];
            }
            EXPECT_NEAR(sum, 1.0, 1e-12);
        }
    }
}

TEST_CASE(hunl_flat_dcfr_exports_average_strategy_by_infoset_key) {
    const auto config = std::make_shared<const core::HUNLConfig>(core::default_tiny_subgame());
    const auto graph = core::HUNLFlatSolveGraph::build(config);
    core::HUNLFlatDCFR solver(
        graph,
        {2, 2},
        core::HUNLFlatValueLayout::InfosetActionHand);

    solver.run_iteration();
    const auto exported = solver.export_average_strategy();

    EXPECT_EQ(exported.size(), graph.infosets.size());
    for (const auto& infoset : graph.infosets) {
        const auto it = exported.find(infoset.key);
        EXPECT_TRUE(it != exported.end());
        EXPECT_EQ(it->second.size(),
                  static_cast<std::size_t>(infoset.action_count) *
                      solver.infoset_table().meta()[infoset.id.value].hand_count);
    }
}
