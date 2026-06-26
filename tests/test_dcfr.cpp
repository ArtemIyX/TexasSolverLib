#include "solver/dcfr.hpp"
#include "games/kuhn.hpp"
#include "test_harness.hpp"

#include <limits>
#include <stdexcept>

TEST_CASE(dcfr_alpha_guard_rejects_non_positive_and_non_finite_alpha) {
    EXPECT_THROW(core::DCFRSolver<core::KuhnState>(core::DCFRConfig{0.0, 0.0, 2.0}), std::invalid_argument);
    EXPECT_THROW(core::DCFRSolver<core::KuhnState>(core::DCFRConfig{-0.5, 0.0, 2.0}), std::invalid_argument);
    EXPECT_THROW(core::DCFRSolver<core::KuhnState>(core::DCFRConfig{std::numeric_limits<double>::infinity(), 0.0, 2.0}), std::invalid_argument);
}

TEST_CASE(dcfr_alpha_warn_band_is_allowed) {
    core::DCFRSolver<core::KuhnState> solver(core::DCFRConfig{0.3, 0.0, 2.0});
    const auto output = solver.solve(1);
    EXPECT_EQ(output.iterations, 1U);
}


