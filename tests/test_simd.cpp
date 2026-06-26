#include "util/simd.hpp"
#include "solver/solver.hpp"
#include "test_harness.hpp"

#include <algorithm>
#include <cmath>
#include <vector>

namespace {

constexpr double TOL = 1e-12;

const std::vector<double>* find_strategy(const core::SolveOutput& output, const std::string& key) {
    const auto it = std::find_if(
        output.average_strategy.begin(),
        output.average_strategy.end(),
        [&key](const auto& entry) { return entry.first == key; });
    return it == output.average_strategy.end() ? nullptr : &it->second;
}

}

TEST_CASE(simd_discount_regrets_vector_matches_scalar) {
    std::vector<double> base;
    base.reserve(3978);
    for (int i = 0; i < 3978; ++i) {
        const auto x = static_cast<double>(i) * 0.137 - 273.4;
        if (i % 7 == 0) {
            base.push_back(0.0);
        } else if (i % 3 == 0) {
            base.push_back(-x);
        } else {
            base.push_back(x);
        }
    }

    auto via_dispatch = base;
    auto via_scalar = base;
    core::discount_regrets(via_dispatch.data(), via_dispatch.size(), 0.51, 0.27);
    core::discount_regrets_scalar(via_scalar.data(), via_scalar.size(), 0.51, 0.27);

    for (std::size_t i = 0; i < via_dispatch.size(); ++i) {
        EXPECT_TRUE(std::abs(via_dispatch[i] - via_scalar[i]) < TOL);
    }
}

TEST_CASE(simd_discount_strategy_sum_vector_matches_scalar) {
    std::vector<double> base;
    base.reserve(3978);
    for (int i = 0; i < 3978; ++i) {
        base.push_back(std::abs(std::sin(static_cast<double>(i))) * 1000.0);
    }

    auto via_dispatch = base;
    auto via_scalar = base;
    core::discount_strategy_sum(via_dispatch.data(), via_dispatch.size(), 0.875);
    core::discount_strategy_sum_scalar(via_scalar.data(), via_scalar.size(), 0.875);

    for (std::size_t i = 0; i < via_dispatch.size(); ++i) {
        EXPECT_TRUE(std::abs(via_dispatch[i] - via_scalar[i]) < TOL);
    }
}

TEST_CASE(simd_compute_strategy_row_matches_scalar) {
    const std::vector<double> regrets = {-3.0, 0.0, 7.0, 2.0, -1.0};
    std::vector<double> via_dispatch(regrets.size(), 0.0);
    std::vector<double> via_scalar(regrets.size(), 0.0);

    core::compute_strategy_row(regrets.data(), via_dispatch.data(), regrets.size());
    core::compute_strategy_row_scalar(regrets.data(), via_scalar.data(), regrets.size());

    for (std::size_t i = 0; i < regrets.size(); ++i) {
        EXPECT_TRUE(std::abs(via_dispatch[i] - via_scalar[i]) < TOL);
    }
}

TEST_CASE(simd_kuhn_solve_is_deterministic_under_current_backend) {
    const auto first = core::solve_kuhn(1000, 1.5, 0.0, 2.0);
    const auto second = core::solve_kuhn(1000, 1.5, 0.0, 2.0);

    EXPECT_EQ(first.average_strategy.size(), second.average_strategy.size());
    for (const auto& [key, probs] : first.average_strategy) {
        const auto* other = find_strategy(second, key);
        EXPECT_TRUE(other != nullptr);
        EXPECT_EQ(probs.size(), other->size());
        for (std::size_t i = 0; i < probs.size(); ++i) {
            EXPECT_EQ(probs[i], (*other)[i]);
        }
    }
}


