#pragma once

#include "core/namespaces.hpp"

#include "util/simd.hpp"

#include <cstddef>
#include <cstdint>

namespace texas::solver::hunl {

enum class HUNLSampledSimdBackend : std::uint8_t {
    Scalar = 0,
    Avx2Fma = 1,
};

void set_hunl_sampled_simd_enabled(bool enabled) noexcept;
[[nodiscard]] bool hunl_sampled_simd_enabled() noexcept;
[[nodiscard]] HUNLSampledSimdBackend hunl_sampled_simd_backend() noexcept;
[[nodiscard]] const char* hunl_sampled_simd_backend_name(HUNLSampledSimdBackend backend) noexcept;

void regret_matching_action_major_f32_scalar(
    const float* regret,
    std::uint32_t action_count,
    std::uint32_t bucket_count,
    float* strategy);

void regret_matching_action_major_f32(
    const float* regret,
    std::uint32_t action_count,
    std::uint32_t bucket_count,
    float* strategy);

void regret_matching_action_major_f64_scalar(
    const double* regret,
    std::uint32_t action_count,
    std::uint32_t bucket_count,
    double* strategy);

void regret_matching_action_major_f64(
    const double* regret,
    std::uint32_t action_count,
    std::uint32_t bucket_count,
    double* strategy);

void accumulate_average_strategy_action_major_f32_scalar(
    const float* strategy,
    const float* reach_or_weight,
    std::uint32_t action_count,
    std::uint32_t bucket_count,
    float scale,
    float* strategy_sum);

void accumulate_average_strategy_action_major_f32(
    const float* strategy,
    const float* reach_or_weight,
    std::uint32_t action_count,
    std::uint32_t bucket_count,
    float scale,
    float* strategy_sum);

void accumulate_average_strategy_action_major_f64_scalar(
    const double* strategy,
    const double* reach_or_weight,
    std::uint32_t action_count,
    std::uint32_t bucket_count,
    double scale,
    double* strategy_sum);

void accumulate_average_strategy_action_major_f64(
    const double* strategy,
    const double* reach_or_weight,
    std::uint32_t action_count,
    std::uint32_t bucket_count,
    double scale,
    double* strategy_sum);

void add_regret_delta_action_major_f32_scalar(
    const float* action_values,
    const float* node_values,
    const float* cf_reach,
    std::uint32_t action_count,
    std::uint32_t bucket_count,
    float* regret);

void add_regret_delta_action_major_f32(
    const float* action_values,
    const float* node_values,
    const float* cf_reach,
    std::uint32_t action_count,
    std::uint32_t bucket_count,
    float* regret);

void add_regret_delta_action_major_f64_scalar(
    const double* action_values,
    const double* node_values,
    const double* cf_reach,
    std::uint32_t action_count,
    std::uint32_t bucket_count,
    double* regret);

void add_regret_delta_action_major_f64(
    const double* action_values,
    const double* node_values,
    const double* cf_reach,
    std::uint32_t action_count,
    std::uint32_t bucket_count,
    double* regret);

void saxpy_f32(std::uint32_t n, float alpha, const float* x, float* y);
double dot_f32_f64_accum(std::uint32_t n, const float* x, const float* y);
double weighted_sum_f32_f64_accum(std::uint32_t n, const float* values, const float* weights);

}  // namespace texas::solver::hunl
