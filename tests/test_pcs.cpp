#include "util/pcs.hpp"
#include "test_harness.hpp"

#include <array>
#include <cmath>

TEST_CASE(pcs_rng_is_deterministic_for_seed) {
    texas::PcsRng a(7);
    texas::PcsRng b(7);
    for (int i = 0; i < 100; ++i) {
        EXPECT_EQ(a.next_u64(), b.next_u64());
    }
}

TEST_CASE(pcs_rng_gen_range_is_in_bounds) {
    texas::PcsRng rng(42);
    for (int i = 0; i < 1000; ++i) {
        EXPECT_TRUE(rng.gen_range(47) < 47);
    }
}

TEST_CASE(sample_uniform_outcome_is_unbiased_in_long_run) {
    texas::PcsRng rng(7);
    std::uint64_t total = 0;
    constexpr std::uint64_t n = 100000;
    for (std::uint64_t i = 0; i < n; ++i) {
        const auto [idx, weight] = texas::sample_uniform_outcome(rng, 47);
        EXPECT_EQ(weight, 47.0);
        total += idx;
    }

    const auto mean = static_cast<double>(total) / static_cast<double>(n);
    const auto expected = (47.0 - 1.0) / 2.0;
    EXPECT_TRUE(std::abs(mean - expected) < 0.3);
}

TEST_CASE(sample_uniform_outcome_negative_control_without_importance_weight) {
    texas::PcsRng rng(7);
    constexpr std::array<double, 4> values = {1.0, 2.0, 3.0, 4.0};
    constexpr double true_sum = 10.0;
    constexpr int n = 50000;

    double unweighted = 0.0;
    double weighted = 0.0;
    for (int i = 0; i < n; ++i) {
        const auto [idx, weight] = texas::sample_uniform_outcome(rng, values.size());
        unweighted += values[idx];
        weighted += weight * values[idx] / static_cast<double>(values.size());
    }

    const auto est_unweighted = unweighted / static_cast<double>(n);
    const auto est_weighted_mean = weighted / static_cast<double>(n);
    EXPECT_TRUE(std::abs(est_unweighted - true_sum / static_cast<double>(values.size())) < 0.05);
    EXPECT_TRUE(std::abs(est_weighted_mean - true_sum / static_cast<double>(values.size())) < 0.05);
}

TEST_CASE(pcs_rng_bernoulli_clamps_extreme_probabilities) {
    texas::PcsRng rng(11);
    for (int i = 0; i < 32; ++i) {
        EXPECT_TRUE(!rng.bernoulli(0.0));
        EXPECT_TRUE(rng.bernoulli(1.0));
    }
}

TEST_CASE(pcs_rng_weighted_sampling_is_deterministic_for_seed) {
    texas::PcsRng first(1234);
    texas::PcsRng second(1234);
    constexpr std::array<double, 4> weights = {0.0, 1.0, 3.0, 6.0};

    for (int i = 0; i < 128; ++i) {
        const auto [first_idx, first_importance] = first.sample_weighted(weights.data(), weights.size());
        const auto [second_idx, second_importance] = second.sample_weighted(weights.data(), weights.size());
        EXPECT_EQ(first_idx, second_idx);
        EXPECT_EQ(first_importance, second_importance);
        EXPECT_TRUE(first_idx != 0U);
        EXPECT_TRUE(first_importance > 0.0);
    }
}

TEST_CASE(pcs_rng_weighted_sampling_tracks_expected_importance_weights) {
    texas::PcsRng rng(99);
    constexpr std::array<double, 3> weights = {2.0, 3.0, 5.0};

    for (int i = 0; i < 64; ++i) {
        const auto [idx, importance] = rng.sample_weighted(weights.data(), weights.size());
        EXPECT_TRUE(idx < weights.size());
        EXPECT_NEAR(importance, 10.0 / weights[idx], 1e-12);
    }
}

TEST_CASE(pcs_rng_seed_mixing_is_deterministic_and_order_sensitive) {
    const auto a = texas::PcsRng::mix_seed(7, 1U, 2U, 3U, 4U);
    const auto b = texas::PcsRng::mix_seed(7, 1U, 2U, 3U, 4U);
    const auto c = texas::PcsRng::mix_seed(7, 4U, 3U, 2U, 1U);

    EXPECT_EQ(a, b);
    EXPECT_TRUE(a != c);
    EXPECT_TRUE(a != 0U);
}


