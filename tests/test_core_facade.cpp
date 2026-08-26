#include "core/lib.hpp"
#include "test_harness.hpp"

TEST_CASE(core_facade_exposes_supported_entrypoints_without_internal_solver_types) {
    const auto kuhn = &texas::core::lib::solve_kuhn;
    const auto leduc = &texas::core::lib::solve_leduc;
    const auto postflop = &texas::core::lib::solve_hunl_postflop;
    const auto sampled = &texas::core::lib::solve_hunl_postflop_sampled;

    EXPECT_TRUE(kuhn != nullptr);
    EXPECT_TRUE(leduc != nullptr);
    EXPECT_TRUE(postflop != nullptr);
    EXPECT_TRUE(sampled != nullptr);
}
