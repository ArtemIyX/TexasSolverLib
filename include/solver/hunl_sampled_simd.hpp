#pragma once

#include <cstddef>
#include <cstdint>

namespace core {

void regret_matching_action_major_f32(
    const float* regret,
    std::uint32_t action_count,
    std::uint32_t bucket_count,
    float* strategy);

void accumulate_average_strategy_action_major_f32(
    const float* strategy,
    const float* reach_or_weight,
    std::uint32_t action_count,
    std::uint32_t bucket_count,
    float scale,
    float* strategy_sum);

void add_regret_delta_action_major_f32(
    const float* action_values,
    const float* node_values,
    const float* cf_reach,
    std::uint32_t action_count,
    std::uint32_t bucket_count,
    float* regret);

void saxpy_f32(std::uint32_t n, float alpha, const float* x, float* y);
double dot_f32_f64_accum(std::uint32_t n, const float* x, const float* y);

}  // namespace core
