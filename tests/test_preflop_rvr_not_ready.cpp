#include "preflop/preflop_rvr.hpp"
#include "test_harness.hpp"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace {

void expect_facade_not_ready(
    std::uint32_t iterations,
    double alpha,
    double beta,
    double gamma) {
    core::HUNLConfig config;
    core::PreflopEquityTable table;
    EXPECT_THROW(
        core::solve_hunl_preflop_rvr(
            config,
            table,
            iterations,
            alpha,
            beta,
            gamma),
        core::PreflopRvrNotReady);
}

void expect_count_overload_not_ready(
    std::size_t decision_nodes,
    std::uint32_t iterations,
    std::size_t reach_size) {
    core::Class169VectorDCFR solver(core::PREFLOP_NUM_CLASSES, 1.5, 0.0, 2.0);
    const std::vector<double> reach(reach_size, 1.0);
    const auto iteration_before = solver.iteration();
    EXPECT_THROW(
        solver.solve(decision_nodes, iterations, reach, reach),
        core::PreflopRvrNotReady);
    EXPECT_EQ(solver.iteration(), iteration_before);
    EXPECT_TRUE(solver.average_strategy().empty());
}

}  // namespace

TEST_CASE(preflop_rvr_facade_rejects_zero_iterations) {
    expect_facade_not_ready(0U, 1.5, 0.0, 2.0);
}

TEST_CASE(preflop_rvr_facade_rejects_one_iteration) {
    expect_facade_not_ready(1U, 1.5, 0.0, 2.0);
}

TEST_CASE(preflop_rvr_facade_rejects_two_iterations) {
    expect_facade_not_ready(2U, 1.5, 0.0, 2.0);
}

TEST_CASE(preflop_rvr_facade_rejects_medium_iteration_count) {
    expect_facade_not_ready(100U, 1.5, 0.0, 2.0);
}

TEST_CASE(preflop_rvr_facade_rejects_blueprint_iteration_count) {
    expect_facade_not_ready(100000U, 1.5, 0.0, 2.0);
}

TEST_CASE(preflop_rvr_facade_rejects_alternate_alpha) {
    expect_facade_not_ready(10U, 0.75, 0.0, 2.0);
}

TEST_CASE(preflop_rvr_facade_rejects_alternate_beta) {
    expect_facade_not_ready(10U, 1.5, 1.0, 2.0);
}

TEST_CASE(preflop_rvr_facade_rejects_alternate_gamma) {
    expect_facade_not_ready(10U, 1.5, 0.0, 3.0);
}

TEST_CASE(preflop_rvr_facade_rejects_zero_exponents) {
    expect_facade_not_ready(10U, 0.0, 0.0, 0.0);
}

TEST_CASE(preflop_rvr_facade_rejects_large_finite_exponents) {
    expect_facade_not_ready(10U, 100.0, 100.0, 100.0);
}

TEST_CASE(preflop_rvr_count_overload_rejects_zero_nodes) {
    expect_count_overload_not_ready(0U, 1U, core::PREFLOP_NUM_CLASSES);
}

TEST_CASE(preflop_rvr_count_overload_rejects_one_node) {
    expect_count_overload_not_ready(1U, 1U, core::PREFLOP_NUM_CLASSES);
}

TEST_CASE(preflop_rvr_count_overload_rejects_two_nodes) {
    expect_count_overload_not_ready(2U, 1U, core::PREFLOP_NUM_CLASSES);
}

TEST_CASE(preflop_rvr_count_overload_rejects_many_nodes) {
    expect_count_overload_not_ready(1000U, 1U, core::PREFLOP_NUM_CLASSES);
}

TEST_CASE(preflop_rvr_count_overload_rejects_zero_iterations) {
    expect_count_overload_not_ready(10U, 0U, core::PREFLOP_NUM_CLASSES);
}

TEST_CASE(preflop_rvr_count_overload_rejects_many_iterations) {
    expect_count_overload_not_ready(10U, 100000U, core::PREFLOP_NUM_CLASSES);
}

TEST_CASE(preflop_rvr_count_overload_rejects_empty_reaches) {
    expect_count_overload_not_ready(10U, 1U, 0U);
}

TEST_CASE(preflop_rvr_count_overload_rejects_short_reaches) {
    expect_count_overload_not_ready(10U, 1U, 1U);
}

TEST_CASE(preflop_rvr_count_overload_rejects_class_sized_reaches) {
    expect_count_overload_not_ready(10U, 1U, core::PREFLOP_NUM_CLASSES);
}

TEST_CASE(preflop_rvr_count_overload_rejects_oversized_reaches) {
    expect_count_overload_not_ready(10U, 1U, core::PREFLOP_NUM_CLASSES + 1U);
}
