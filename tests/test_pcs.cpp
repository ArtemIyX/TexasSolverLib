#include "core/pcs.hpp"
#include "test_harness.hpp"

#include <array>
#include <cmath>

TEST_CASE(pcs_effective_beta_overrides_for_public_chance) {
    EXPECT_EQ(core::effective_beta(core::SamplingStrategy::Full, 0.0), 0.0);
    EXPECT_EQ(core::effective_beta(core::SamplingStrategy::Full, 0.7), 0.7);
    EXPECT_EQ(core::effective_beta(core::SamplingStrategy::PublicChance, 0.0), 0.5);
    EXPECT_EQ(core::effective_beta(core::SamplingStrategy::PublicChance, 0.9), 0.5);
}

TEST_CASE(pcs_rng_is_deterministic_for_seed) {
    core::PcsRng a(7);
    core::PcsRng b(7);
    for (int i = 0; i < 100; ++i) {
        EXPECT_EQ(a.next_u64(), b.next_u64());
    }
}

TEST_CASE(pcs_rng_gen_range_is_in_bounds) {
    core::PcsRng rng(42);
    for (int i = 0; i < 1000; ++i) {
        EXPECT_TRUE(rng.gen_range(47) < 47);
    }
}

TEST_CASE(sample_uniform_outcome_is_unbiased_in_long_run) {
    core::PcsRng rng(7);
    std::uint64_t total = 0;
    constexpr std::uint64_t n = 100000;
    for (std::uint64_t i = 0; i < n; ++i) {
        const auto [idx, weight] = core::sample_uniform_outcome(rng, 47);
        EXPECT_EQ(weight, 47.0);
        total += idx;
    }

    const auto mean = static_cast<double>(total) / static_cast<double>(n);
    const auto expected = (47.0 - 1.0) / 2.0;
    EXPECT_TRUE(std::abs(mean - expected) < 0.3);
}

TEST_CASE(sample_uniform_outcome_negative_control_without_importance_weight) {
    core::PcsRng rng(7);
    constexpr std::array<double, 4> values = {1.0, 2.0, 3.0, 4.0};
    constexpr double true_sum = 10.0;
    constexpr int n = 50000;

    double unweighted = 0.0;
    double weighted = 0.0;
    for (int i = 0; i < n; ++i) {
        const auto [idx, weight] = core::sample_uniform_outcome(rng, values.size());
        unweighted += values[idx];
        weighted += weight * values[idx] / static_cast<double>(values.size());
    }

    const auto est_unweighted = unweighted / static_cast<double>(n);
    const auto est_weighted_mean = weighted / static_cast<double>(n);
    EXPECT_TRUE(std::abs(est_unweighted - true_sum / static_cast<double>(values.size())) < 0.05);
    EXPECT_TRUE(std::abs(est_weighted_mean - true_sum / static_cast<double>(values.size())) < 0.05);
}
