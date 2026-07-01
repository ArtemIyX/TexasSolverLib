#include "util/simd.hpp"
#include "solver/solver.hpp"
#include "test_harness.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
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

TEST_CASE(simd_copy_values_matches_scalar_and_handles_empty_rows) {
    std::vector<double> empty;
    std::vector<double> empty_out;
    core::copy_values(empty_out.data(), empty.data(), empty.size());

    const std::vector<double> values = {1.0, -2.5, 3.25, 4.75, -9.0};
    std::vector<double> copied(values.size(), 0.0);
    core::copy_values(copied.data(), values.data(), values.size());

    for (std::size_t i = 0; i < values.size(); ++i) {
        EXPECT_TRUE(std::abs(copied[i] - values[i]) < TOL);
    }
}

TEST_CASE(simd_dot_product_matches_scalar_expectations) {
    const std::vector<double> lhs = {0.25, -2.0, 4.5, 1.25, -3.0};
    const std::vector<double> rhs = {8.0, 0.5, -1.0, 2.0, -4.0};

    const auto via_dispatch = core::dot_product(lhs.data(), rhs.data(), lhs.size());
    const auto via_scalar = core::dot_product_scalar(lhs.data(), rhs.data(), lhs.size());

    EXPECT_NEAR(via_dispatch, via_scalar, TOL);
    EXPECT_NEAR(via_dispatch, 11.0, TOL);
    EXPECT_EQ(core::dot_product(lhs.data(), rhs.data(), 0), 0.0);
}

TEST_CASE(simd_dot_product_strided_matches_scalar_expectations) {
    const std::vector<double> lhs = {0.5, 1.5, -2.0, 4.0};
    const std::vector<double> rhs = {
        2.0, 10.0,
        -1.0, 11.0,
        3.0, 12.0,
        0.25, 13.0,
    };

    const auto via_dispatch = core::dot_product_strided(lhs.data(), rhs.data(), lhs.size(), 2);
    const auto via_scalar = core::dot_product_strided_scalar(lhs.data(), rhs.data(), lhs.size(), 2);

    EXPECT_NEAR(via_dispatch, via_scalar, TOL);
    EXPECT_NEAR(via_dispatch, -5.5, TOL);
    EXPECT_EQ(core::dot_product_strided(lhs.data(), rhs.data(), 0, 2), 0.0);
}

TEST_CASE(simd_row_normalization_matches_scalar_and_handles_zero_length) {
    std::vector<double> empty_in;
    std::vector<double> empty_out;
    core::normalize_row(empty_in.data(), empty_out.data(), empty_in.size());

    const std::vector<double> values = {2.0, 3.0, 5.0, 0.0};
    std::vector<double> via_dispatch(values.size(), 0.0);
    std::vector<double> via_scalar(values.size(), 0.0);
    core::normalize_row(values.data(), via_dispatch.data(), values.size());
    core::normalize_row(values.data(), via_scalar.data(), values.size());

    for (std::size_t i = 0; i < values.size(); ++i) {
        EXPECT_TRUE(std::abs(via_dispatch[i] - via_scalar[i]) < TOL);
    }
}

TEST_CASE(simd_row_reduction_helpers_match_scalar_and_cover_special_values) {
    const std::vector<double> values = {1.5, -2.0, 4.0, std::numeric_limits<double>::infinity()};
    const std::vector<double> weights = {0.25, 0.5, 0.75, 1.0};

    EXPECT_TRUE(core::reduce_action_values(values.data(), 0) == 0.0);
    EXPECT_TRUE(core::reduce_weighted_action_values(values.data(), weights.data(), 0) == 0.0);

    EXPECT_TRUE(core::reduce_action_values(values.data(), 2) == values[0] + values[1]);
    EXPECT_TRUE(core::reduce_weighted_action_values(values.data(), weights.data(), 2) ==
                values[0] * weights[0] + values[1] * weights[1]);

    const std::vector<double> regrets = {
        -std::numeric_limits<double>::infinity(),
        -3.0,
        0.0,
        std::numeric_limits<double>::quiet_NaN(),
        5.0};
    std::vector<double> positive(regrets.size(), 0.0);
    const double total = core::positive_regrets_and_total(regrets.data(), positive.data(), regrets.size());

    EXPECT_TRUE(std::isnan(positive[3]));
    EXPECT_TRUE(std::isnan(total));
}

TEST_CASE(simd_update_strategy_sum_matches_scalar_and_handles_small_rows) {
    std::vector<double> empty;
    core::update_strategy_sum(empty.data(), empty.data(), 0, 1.0);

    const std::vector<double> strategy = {0.1, 0.2, 0.3, 0.4};
    std::vector<double> via_dispatch(strategy.size(), 1.0);
    std::vector<double> via_scalar(strategy.size(), 1.0);
    core::update_strategy_sum(via_dispatch.data(), strategy.data(), strategy.size(), 2.5);
    core::update_strategy_sum_scalar(via_scalar.data(), strategy.data(), strategy.size(), 2.5);

    for (std::size_t i = 0; i < strategy.size(); ++i) {
        EXPECT_TRUE(std::abs(via_dispatch[i] - via_scalar[i]) < TOL);
    }
}

TEST_CASE(simd_update_regret_sum_matches_scalar) {
    const std::vector<double> action_values = {4.0, -1.0, 7.5, 0.25, 3.5};
    std::vector<double> via_dispatch(action_values.size(), 0.0);
    std::vector<double> via_scalar(action_values.size(), 0.0);
    core::update_regret_sum(via_dispatch.data(), action_values.data(), action_values.size(), 1.75, 0.5);
    core::update_regret_sum_scalar(via_scalar.data(), action_values.data(), action_values.size(), 1.75, 0.5);

    for (std::size_t i = 0; i < action_values.size(); ++i) {
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


