#include "solver/hunl_sampled_simd.hpp"
#include "test_harness.hpp"

#include <cmath>
#include <cstdint>
#include <vector>

namespace {

template <class T>
void scalar_regret_matching(const T* regret, std::uint32_t actions, std::uint32_t buckets, T* strategy);

template <>
void scalar_regret_matching<float>(
    const float* regret, std::uint32_t actions, std::uint32_t buckets, float* strategy) {
    texas::regret_matching_action_major_f32_scalar(regret, actions, buckets, strategy);
}

template <>
void scalar_regret_matching<double>(
    const double* regret, std::uint32_t actions, std::uint32_t buckets, double* strategy) {
    texas::regret_matching_action_major_f64_scalar(regret, actions, buckets, strategy);
}

template <class T>
void dispatched_regret_matching(const T* regret, std::uint32_t actions, std::uint32_t buckets, T* strategy);

template <>
void dispatched_regret_matching<float>(
    const float* regret, std::uint32_t actions, std::uint32_t buckets, float* strategy) {
    texas::regret_matching_action_major_f32(regret, actions, buckets, strategy);
}

template <>
void dispatched_regret_matching<double>(
    const double* regret, std::uint32_t actions, std::uint32_t buckets, double* strategy) {
    texas::regret_matching_action_major_f64(regret, actions, buckets, strategy);
}

template <class T>
void expect_regret_matching_matches_scalar(std::uint32_t actions, std::uint32_t buckets, double tolerance) {
    std::vector<T> regret(static_cast<std::size_t>(actions) * buckets);
    std::vector<T> scalar(regret.size(), static_cast<T>(0));
    std::vector<T> dispatched(regret.size(), static_cast<T>(0));
    for (std::size_t index = 0; index < regret.size(); ++index) {
        const auto value = static_cast<int>((index * 17U + actions * 5U + buckets) % 23U) - 11;
        regret[index] = static_cast<T>(value);
    }
    for (std::uint32_t bucket = 0; bucket < buckets; bucket += 5U) {
        for (std::uint32_t action = 0; action < actions; ++action) {
            regret[static_cast<std::size_t>(action) * buckets + bucket] = static_cast<T>(-1);
        }
    }

    scalar_regret_matching(regret.data(), actions, buckets, scalar.data());
    dispatched_regret_matching(regret.data(), actions, buckets, dispatched.data());
    for (std::size_t index = 0; index < regret.size(); ++index) {
        EXPECT_TRUE(std::isfinite(dispatched[index]));
        EXPECT_NEAR(dispatched[index], scalar[index], tolerance);
    }
    for (std::uint32_t bucket = 0; bucket < buckets; ++bucket) {
        double total = 0.0;
        for (std::uint32_t action = 0; action < actions; ++action) {
            total += dispatched[static_cast<std::size_t>(action) * buckets + bucket];
        }
        EXPECT_NEAR(total, 1.0, tolerance);
    }
}

#define P75_F32_CASE(actions, buckets) \
    TEST_CASE(hunl_p75_f32_regret_matching_##actions##x##buckets) { \
        expect_regret_matching_matches_scalar<float>(actions, buckets, 1e-5); \
    }

#define P75_F64_CASE(actions, buckets) \
    TEST_CASE(hunl_p75_f64_regret_matching_##actions##x##buckets) { \
        expect_regret_matching_matches_scalar<double>(actions, buckets, 1e-12); \
    }

P75_F32_CASE(1U, 1U)
P75_F32_CASE(1U, 8U)
P75_F32_CASE(2U, 1U)
P75_F32_CASE(2U, 3U)
P75_F32_CASE(2U, 7U)
P75_F32_CASE(2U, 8U)
P75_F32_CASE(2U, 9U)
P75_F32_CASE(3U, 2U)
P75_F32_CASE(3U, 5U)
P75_F32_CASE(3U, 8U)
P75_F32_CASE(3U, 15U)
P75_F32_CASE(4U, 1U)
P75_F32_CASE(4U, 4U)
P75_F32_CASE(4U, 7U)
P75_F32_CASE(4U, 16U)
P75_F32_CASE(5U, 3U)
P75_F32_CASE(5U, 8U)
P75_F32_CASE(5U, 11U)
P75_F32_CASE(6U, 1U)
P75_F32_CASE(6U, 6U)
P75_F32_CASE(6U, 8U)
P75_F32_CASE(6U, 17U)
P75_F32_CASE(7U, 5U)
P75_F32_CASE(7U, 8U)
P75_F32_CASE(8U, 9U)
P75_F32_CASE(8U, 31U)

P75_F64_CASE(1U, 1U)
P75_F64_CASE(1U, 4U)
P75_F64_CASE(2U, 1U)
P75_F64_CASE(2U, 3U)
P75_F64_CASE(2U, 4U)
P75_F64_CASE(2U, 5U)
P75_F64_CASE(2U, 7U)
P75_F64_CASE(3U, 2U)
P75_F64_CASE(3U, 4U)
P75_F64_CASE(3U, 9U)
P75_F64_CASE(3U, 15U)
P75_F64_CASE(4U, 1U)
P75_F64_CASE(4U, 4U)
P75_F64_CASE(4U, 7U)
P75_F64_CASE(4U, 16U)
P75_F64_CASE(5U, 3U)
P75_F64_CASE(5U, 4U)
P75_F64_CASE(5U, 11U)
P75_F64_CASE(6U, 1U)
P75_F64_CASE(6U, 6U)
P75_F64_CASE(6U, 8U)
P75_F64_CASE(6U, 17U)
P75_F64_CASE(7U, 5U)
P75_F64_CASE(7U, 8U)
P75_F64_CASE(8U, 9U)
P75_F64_CASE(8U, 31U)

#undef P75_F32_CASE
#undef P75_F64_CASE

TEST_CASE(hunl_p75_f32_nan_row_preserves_scalar_fallback) {
    const std::vector<float> regret = {
        1.0f, 2.0f, std::nanf(""), 4.0f,
        2.0f, 3.0f, 4.0f, 5.0f,
    };
    std::vector<float> scalar(regret.size(), 0.0f);
    std::vector<float> dispatched(regret.size(), 0.0f);
    scalar_regret_matching(regret.data(), 2U, 4U, scalar.data());
    dispatched_regret_matching(regret.data(), 2U, 4U, dispatched.data());
    for (std::size_t index = 0; index < regret.size(); ++index) {
        EXPECT_NEAR(dispatched[index], scalar[index], 1e-5);
    }
}

TEST_CASE(hunl_p75_f64_nan_row_preserves_scalar_fallback) {
    const std::vector<double> regret = {
        1.0, 2.0, std::nan(""), 4.0,
        2.0, 3.0, 4.0, 5.0,
    };
    std::vector<double> scalar(regret.size(), 0.0);
    std::vector<double> dispatched(regret.size(), 0.0);
    scalar_regret_matching(regret.data(), 2U, 4U, scalar.data());
    dispatched_regret_matching(regret.data(), 2U, 4U, dispatched.data());
    for (std::size_t index = 0; index < regret.size(); ++index) {
        EXPECT_NEAR(dispatched[index], scalar[index], 1e-12);
    }
}

TEST_CASE(hunl_p75_f32_dispatch_rejects_null_or_empty_rows_without_writing) {
    std::vector<float> strategy = {3.0f, 5.0f};
    const std::vector<float> regret = {1.0f, 2.0f};
    texas::regret_matching_action_major_f32(nullptr, 2U, 1U, strategy.data());
    texas::regret_matching_action_major_f32(regret.data(), 2U, 1U, nullptr);
    texas::regret_matching_action_major_f32(regret.data(), 0U, 1U, strategy.data());
    texas::regret_matching_action_major_f32(regret.data(), 2U, 0U, strategy.data());
    EXPECT_NEAR(strategy[0], 3.0, 1e-5);
    EXPECT_NEAR(strategy[1], 5.0, 1e-5);
}

TEST_CASE(hunl_p75_f64_dispatch_rejects_null_or_empty_rows_without_writing) {
    std::vector<double> strategy = {3.0, 5.0};
    const std::vector<double> regret = {1.0, 2.0};
    texas::regret_matching_action_major_f64(nullptr, 2U, 1U, strategy.data());
    texas::regret_matching_action_major_f64(regret.data(), 2U, 1U, nullptr);
    texas::regret_matching_action_major_f64(regret.data(), 0U, 1U, strategy.data());
    texas::regret_matching_action_major_f64(regret.data(), 2U, 0U, strategy.data());
    EXPECT_NEAR(strategy[0], 3.0, 1e-12);
    EXPECT_NEAR(strategy[1], 5.0, 1e-12);
}

}  // namespace
