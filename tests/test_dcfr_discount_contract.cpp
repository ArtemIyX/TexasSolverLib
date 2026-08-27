#include "games/kuhn.hpp"
#include "preflop/preflop_rvr.hpp"
#include "solver/generic/dcfr.hpp"
#include "solver/generic/dcfr_vector.hpp"
#include "solver/hunl/flat/hunl_flat_dcfr.hpp"
#include "solver/hunl/flat/hunl_flat_mccfr.hpp"
#include "solver/generic/parallel_dcfr.hpp"
#include "test_harness.hpp"

#include <cmath>
#include <limits>
#include <stdexcept>

namespace {

constexpr double TOL = 1e-12;

texas::solver::dcfr::detail::InfosetAccumTable table_with_values(
    double positive,
    double negative,
    double strategy) {
    texas::solver::dcfr::detail::InfosetAccumTable table;
    table.begin_dcfr_iteration(1U, texas::DCFRConfig{});
    auto row = table.ensure(texas::InfosetId{0U}, 3U);
    row.regret_sum[0] = positive;
    row.regret_sum[1] = negative;
    row.regret_sum[2] = 0.0;
    row.strategy_sum[0] = strategy;
    row.strategy_sum[1] = strategy;
    row.strategy_sum[2] = strategy;
    return table;
}

texas::HUNLFlatSolveGraph empty_flat_graph() {
    return {};
}

texas::BettingTree one_decision_tree(std::uint8_t player) {
    texas::BettingTree tree;
    texas::FlatNode root;
    root.tag = texas::FlatNodeTag::Decision;
    root.player = player;
    root.actions = {0, 1};
    root.children = {1U, 2U};
    tree.nodes = {root, texas::FlatNode{}, texas::FlatNode{}};
    return tree;
}

bool strategies_differ(
    const texas::SolveOutput& lhs,
    const texas::SolveOutput& rhs) {
    if (lhs.average_strategy.size() != rhs.average_strategy.size()) {
        return true;
    }
    for (std::size_t row = 0; row < lhs.average_strategy.size(); ++row) {
        if (lhs.average_strategy[row].first != rhs.average_strategy[row].first ||
            lhs.average_strategy[row].second.size() !=
                rhs.average_strategy[row].second.size()) {
            return true;
        }
        for (std::size_t action = 0;
             action < lhs.average_strategy[row].second.size();
             ++action) {
            if (std::fabs(
                    lhs.average_strategy[row].second[action] -
                    rhs.average_strategy[row].second[action]) > TOL) {
                return true;
            }
        }
    }
    return false;
}

}  // namespace

TEST_CASE(dcfr_scales_iteration_one_match_definition) {
    const auto scales = texas::dcfr_iteration_scales(1U, 1.5, 0.0, 2.0);
    EXPECT_NEAR(scales.positive_regret, 0.5, TOL);
    EXPECT_NEAR(scales.negative_regret, 0.5, TOL);
    EXPECT_NEAR(scales.strategy_sum, 0.25, TOL);
}

TEST_CASE(dcfr_scales_iteration_two_positive_regret) {
    const auto scales = texas::dcfr_iteration_scales(2U, 1.0, 0.0, 2.0);
    EXPECT_NEAR(scales.positive_regret, 2.0 / 3.0, TOL);
}

TEST_CASE(dcfr_scales_iteration_two_negative_regret) {
    const auto scales = texas::dcfr_iteration_scales(2U, 1.5, 0.0, 2.0);
    EXPECT_NEAR(scales.negative_regret, 0.5, TOL);
}

TEST_CASE(dcfr_scales_zero_gamma_preserves_strategy_sum) {
    const auto scales = texas::dcfr_iteration_scales(9U, 1.5, 0.0, 0.0);
    EXPECT_NEAR(scales.strategy_sum, 1.0, TOL);
}

TEST_CASE(dcfr_scales_large_finite_exponents_remain_finite) {
    const auto scales = texas::dcfr_iteration_scales(
        std::numeric_limits<std::uint32_t>::max(),
        std::numeric_limits<double>::max(),
        std::numeric_limits<double>::max(),
        std::numeric_limits<double>::max());
    EXPECT_TRUE(std::isfinite(scales.positive_regret));
    EXPECT_TRUE(std::isfinite(scales.negative_regret));
    EXPECT_TRUE(std::isfinite(scales.strategy_sum));
}

TEST_CASE(dcfr_scales_reject_iteration_zero) {
    EXPECT_THROW(
        texas::dcfr_iteration_scales(0U, 1.5, 0.0, 2.0),
        std::invalid_argument);
}

TEST_CASE(dcfr_new_row_skips_nonexistent_past_discounts) {
    texas::solver::dcfr::detail::InfosetAccumTable table;
    table.begin_dcfr_iteration(8U, texas::DCFRConfig{});
    auto row = table.ensure(texas::InfosetId{0U}, 1U);
    row.regret_sum[0] = 12.0;
    table.begin_dcfr_iteration(8U, texas::DCFRConfig{});
    EXPECT_NEAR(table.view(texas::InfosetId{0U}, 1U).regret_sum[0], 12.0, TOL);
}

TEST_CASE(dcfr_positive_regret_is_discounted_before_next_iteration) {
    auto table = table_with_values(12.0, -12.0, 12.0);
    table.begin_dcfr_iteration(2U, texas::DCFRConfig{1.0, 0.0, 2.0});
    EXPECT_NEAR(
        table.view(texas::InfosetId{0U}, 3U).regret_sum[0],
        8.0,
        TOL);
}

TEST_CASE(dcfr_negative_regret_uses_beta_scale) {
    auto table = table_with_values(12.0, -12.0, 12.0);
    table.begin_dcfr_iteration(2U, texas::DCFRConfig{1.0, 1.0, 2.0});
    EXPECT_NEAR(
        table.view(texas::InfosetId{0U}, 3U).regret_sum[1],
        -8.0,
        TOL);
}

TEST_CASE(dcfr_strategy_sum_uses_gamma_scale) {
    auto table = table_with_values(12.0, -12.0, 12.0);
    table.begin_dcfr_iteration(2U, texas::DCFRConfig{1.0, 0.0, 2.0});
    EXPECT_NEAR(
        table.view(texas::InfosetId{0U}, 3U).strategy_sum[0],
        12.0 * 4.0 / 9.0,
        TOL);
}

TEST_CASE(dcfr_zero_regret_remains_zero) {
    auto table = table_with_values(12.0, -12.0, 12.0);
    table.begin_dcfr_iteration(2U, texas::DCFRConfig{1.0, 0.0, 2.0});
    EXPECT_NEAR(
        table.view(texas::InfosetId{0U}, 3U).regret_sum[2],
        0.0,
        TOL);
}

TEST_CASE(dcfr_delayed_discount_applies_every_missing_iteration_once) {
    auto table = table_with_values(12.0, -12.0, 12.0);
    const texas::DCFRConfig config{1.0, 0.0, 0.0};
    table.begin_dcfr_iteration(3U, config);
    EXPECT_NEAR(
        table.view(texas::InfosetId{0U}, 3U).regret_sum[0],
        12.0 * (2.0 / 3.0) * (3.0 / 4.0),
        TOL);
}

TEST_CASE(dcfr_same_iteration_does_not_discount_twice) {
    auto table = table_with_values(12.0, -12.0, 12.0);
    const texas::DCFRConfig config{1.0, 0.0, 0.0};
    table.begin_dcfr_iteration(2U, config);
    const auto once = table.view(texas::InfosetId{0U}, 3U).regret_sum[0];
    table.begin_dcfr_iteration(2U, config);
    EXPECT_NEAR(
        table.view(texas::InfosetId{0U}, 3U).regret_sum[0],
        once,
        TOL);
}

TEST_CASE(dcfr_table_rejects_backwards_iteration) {
    texas::solver::dcfr::detail::InfosetAccumTable table;
    table.begin_dcfr_iteration(3U, texas::DCFRConfig{});
    EXPECT_THROW(
        table.begin_dcfr_iteration(2U, texas::DCFRConfig{}),
        std::invalid_argument);
}

TEST_CASE(dcfr_table_rejects_changed_action_count_on_ensure) {
    texas::solver::dcfr::detail::InfosetAccumTable table;
    (void)table.ensure(texas::InfosetId{0U}, 2U);
    EXPECT_THROW(
        table.ensure(texas::InfosetId{0U}, 3U),
        std::invalid_argument);
}

TEST_CASE(dcfr_table_rejects_changed_action_count_on_view) {
    texas::solver::dcfr::detail::InfosetAccumTable table;
    (void)table.ensure(texas::InfosetId{0U}, 2U);
    EXPECT_THROW(
        table.view(texas::InfosetId{0U}, 3U),
        std::invalid_argument);
}

TEST_CASE(dcfr_table_clear_resets_discount_cursor) {
    texas::solver::dcfr::detail::InfosetAccumTable table;
    table.begin_dcfr_iteration(9U, texas::DCFRConfig{});
    table.clear();
    table.begin_dcfr_iteration(1U, texas::DCFRConfig{});
    EXPECT_TRUE(table.empty());
}

TEST_CASE(dcfr_table_discounts_all_active_rows) {
    texas::solver::dcfr::detail::InfosetAccumTable table;
    table.begin_dcfr_iteration(1U, texas::DCFRConfig{});
    table.ensure(texas::InfosetId{0U}, 1U).regret_sum[0] = 6.0;
    table.ensure(texas::InfosetId{3U}, 1U).regret_sum[0] = 9.0;
    table.begin_dcfr_iteration(2U, texas::DCFRConfig{1.0, 0.0, 0.0});
    EXPECT_NEAR(table.view(texas::InfosetId{0U}, 1U).regret_sum[0], 4.0, TOL);
    EXPECT_NEAR(table.view(texas::InfosetId{3U}, 1U).regret_sum[0], 6.0, TOL);
}

TEST_CASE(dcfr_late_row_receives_only_future_discount) {
    texas::solver::dcfr::detail::InfosetAccumTable table;
    const texas::DCFRConfig config{1.0, 0.0, 0.0};
    table.begin_dcfr_iteration(3U, config);
    table.ensure(texas::InfosetId{0U}, 1U).regret_sum[0] = 10.0;
    table.begin_dcfr_iteration(4U, config);
    EXPECT_NEAR(table.view(texas::InfosetId{0U}, 1U).regret_sum[0], 8.0, TOL);
}

TEST_CASE(dcfr_alpha_changes_row_evolution) {
    auto low = table_with_values(12.0, -12.0, 12.0);
    auto high = table_with_values(12.0, -12.0, 12.0);
    low.begin_dcfr_iteration(4U, texas::DCFRConfig{0.5, 0.0, 2.0});
    high.begin_dcfr_iteration(4U, texas::DCFRConfig{4.0, 0.0, 2.0});
    EXPECT_TRUE(
        std::fabs(
            low.view(texas::InfosetId{0U}, 3U).regret_sum[0] -
            high.view(texas::InfosetId{0U}, 3U).regret_sum[0]) > TOL);
}

TEST_CASE(dcfr_beta_changes_row_evolution) {
    auto low = table_with_values(12.0, -12.0, 12.0);
    auto high = table_with_values(12.0, -12.0, 12.0);
    low.begin_dcfr_iteration(4U, texas::DCFRConfig{1.5, 0.0, 2.0});
    high.begin_dcfr_iteration(4U, texas::DCFRConfig{1.5, 4.0, 2.0});
    EXPECT_TRUE(
        std::fabs(
            low.view(texas::InfosetId{0U}, 3U).regret_sum[1] -
            high.view(texas::InfosetId{0U}, 3U).regret_sum[1]) > TOL);
}

TEST_CASE(dcfr_gamma_changes_row_evolution) {
    auto low = table_with_values(12.0, -12.0, 12.0);
    auto high = table_with_values(12.0, -12.0, 12.0);
    low.begin_dcfr_iteration(4U, texas::DCFRConfig{1.5, 0.0, 0.0});
    high.begin_dcfr_iteration(4U, texas::DCFRConfig{1.5, 0.0, 8.0});
    EXPECT_TRUE(
        std::fabs(
            low.view(texas::InfosetId{0U}, 3U).strategy_sum[0] -
            high.view(texas::InfosetId{0U}, 3U).strategy_sum[0]) > TOL);
}

TEST_CASE(dcfr_validation_rejects_nan_alpha) {
    EXPECT_THROW(
        texas::validate_dcfr_config_values(
            std::numeric_limits<double>::quiet_NaN(), 0.0, 2.0),
        std::invalid_argument);
}

TEST_CASE(dcfr_validation_rejects_infinite_beta) {
    EXPECT_THROW(
        texas::validate_dcfr_config_values(
            1.5, std::numeric_limits<double>::infinity(), 2.0),
        std::invalid_argument);
}

TEST_CASE(dcfr_validation_rejects_nan_beta) {
    EXPECT_THROW(
        texas::validate_dcfr_config_values(
            1.5, std::numeric_limits<double>::quiet_NaN(), 2.0),
        std::invalid_argument);
}

TEST_CASE(dcfr_validation_rejects_negative_beta) {
    EXPECT_THROW(
        texas::validate_dcfr_config_values(1.5, -0.01, 2.0),
        std::invalid_argument);
}

TEST_CASE(dcfr_validation_rejects_infinite_gamma) {
    EXPECT_THROW(
        texas::validate_dcfr_config_values(
            1.5, 0.0, std::numeric_limits<double>::infinity()),
        std::invalid_argument);
}

TEST_CASE(dcfr_validation_rejects_nan_gamma) {
    EXPECT_THROW(
        texas::validate_dcfr_config_values(
            1.5, 0.0, std::numeric_limits<double>::quiet_NaN()),
        std::invalid_argument);
}

TEST_CASE(dcfr_validation_rejects_negative_gamma) {
    EXPECT_THROW(
        texas::validate_dcfr_config_values(1.5, 0.0, -0.01),
        std::invalid_argument);
}

TEST_CASE(dcfr_recursive_constructor_rejects_nonfinite_beta) {
    EXPECT_THROW(
        texas::DCFRSolver<texas::KuhnState>(
            texas::DCFRConfig{
                1.5, std::numeric_limits<double>::quiet_NaN(), 2.0}),
        std::invalid_argument);
}

TEST_CASE(dcfr_parallel_constructor_rejects_nonfinite_gamma) {
    EXPECT_THROW(
        texas::ParallelDCFRSolver<texas::KuhnState>(
            texas::DCFRConfig{
                1.5, 0.0, std::numeric_limits<double>::infinity()}),
        std::invalid_argument);
}

TEST_CASE(dcfr_vector_constructor_rejects_nonfinite_beta) {
    EXPECT_THROW(
        texas::VectorDCFR::new_solver(
            texas::BettingTree{},
            {1U, 1U},
            1.5,
            std::numeric_limits<double>::quiet_NaN(),
            2.0),
        std::invalid_argument);
}

TEST_CASE(dcfr_class169_constructor_rejects_nonfinite_gamma) {
    EXPECT_THROW(
        texas::Class169VectorDCFR(
            texas::PREFLOP_NUM_CLASSES,
            1.5,
            0.0,
            std::numeric_limits<double>::infinity()),
        std::invalid_argument);
}

TEST_CASE(dcfr_flat_constructor_rejects_nonfinite_beta) {
    EXPECT_THROW(
        texas::HUNLFlatDCFR(
            empty_flat_graph(),
            {1U, 1U},
            texas::HUNLFlatValueLayout::InfosetActionHand,
            1U,
            1.5,
            std::numeric_limits<double>::quiet_NaN(),
            2.0),
        std::invalid_argument);
}

TEST_CASE(dcfr_mccfr_constructor_rejects_nonfinite_gamma) {
    texas::HUNLFlatMCCFRConfig config;
    config.dcfr_gamma = std::numeric_limits<double>::infinity();
    EXPECT_THROW(
        texas::HUNLFlatMCCFR(empty_flat_graph(), {1U, 1U}, config),
        std::invalid_argument);
}

TEST_CASE(dcfr_vector_opponent_traversal_does_not_update_row) {
    const auto tree = one_decision_tree(1U);
    auto solver = texas::VectorDCFR::new_solver(
        tree, {1U, 1U}, 1.5, 0.0, 2.0);
    const auto terminal = [](
                              std::size_t node,
                              std::size_t) {
        return std::vector<double>{node == 1U ? 1.0 : -1.0};
    };
    (void)solver.traverse(tree, 0U, 0U, {1.0}, {1.0}, terminal);
    const auto& row = *solver.infosets[0];
    EXPECT_NEAR(row.regret[0], 0.0, TOL);
    EXPECT_NEAR(row.regret[1], 0.0, TOL);
    EXPECT_NEAR(row.strategy_sum[0] + row.strategy_sum[1], 0.0, TOL);
}

TEST_CASE(dcfr_vector_owner_traversal_updates_row) {
    const auto tree = one_decision_tree(0U);
    auto solver = texas::VectorDCFR::new_solver(
        tree, {1U, 1U}, 1.5, 0.0, 2.0);
    const auto terminal = [](
                              std::size_t node,
                              std::size_t) {
        return std::vector<double>{node == 1U ? 1.0 : -1.0};
    };
    (void)solver.traverse(tree, 0U, 0U, {1.0}, {1.0}, terminal);
    const auto& row = *solver.infosets[0];
    EXPECT_TRUE(std::fabs(row.regret[0]) > TOL);
    EXPECT_NEAR(row.strategy_sum[0] + row.strategy_sum[1], 1.0, TOL);
}

TEST_CASE(dcfr_recursive_output_is_parameter_sensitive) {
    texas::DCFRSolver<texas::KuhnState> low(
        texas::DCFRConfig{0.5, 0.0, 0.0});
    texas::DCFRSolver<texas::KuhnState> high(
        texas::DCFRConfig{4.0, 4.0, 8.0});
    EXPECT_TRUE(strategies_differ(low.solve(20U), high.solve(20U)));
}
