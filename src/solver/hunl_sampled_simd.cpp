#include "solver/hunl_sampled_simd.hpp"

#include <algorithm>

namespace core {

void regret_matching_action_major_f32(
    const float* regret,
    std::uint32_t action_count,
    std::uint32_t bucket_count,
    float* strategy) {
    if (regret == nullptr || strategy == nullptr || action_count == 0 || bucket_count == 0) {
        return;
    }

    for (std::uint32_t bucket = 0; bucket < bucket_count; ++bucket) {
        float positive_total = 0.0f;
        for (std::uint32_t action = 0; action < action_count; ++action) {
            const auto index = action * bucket_count + bucket;
            const auto positive = std::max(regret[index], 0.0f);
            strategy[index] = positive;
            positive_total += positive;
        }

        if (positive_total > 0.0f) {
            for (std::uint32_t action = 0; action < action_count; ++action) {
                const auto index = action * bucket_count + bucket;
                strategy[index] /= positive_total;
            }
        } else {
            const auto uniform = 1.0f / static_cast<float>(action_count);
            for (std::uint32_t action = 0; action < action_count; ++action) {
                const auto index = action * bucket_count + bucket;
                strategy[index] = uniform;
            }
        }
    }
}

void accumulate_average_strategy_action_major_f32(
    const float* strategy,
    const float* reach_or_weight,
    std::uint32_t action_count,
    std::uint32_t bucket_count,
    float scale,
    float* strategy_sum) {
    if (strategy == nullptr || reach_or_weight == nullptr || strategy_sum == nullptr) {
        return;
    }

    for (std::uint32_t action = 0; action < action_count; ++action) {
        const auto action_offset = action * bucket_count;
        for (std::uint32_t bucket = 0; bucket < bucket_count; ++bucket) {
            strategy_sum[action_offset + bucket] +=
                strategy[action_offset + bucket] * reach_or_weight[bucket] * scale;
        }
    }
}

void add_regret_delta_action_major_f32(
    const float* action_values,
    const float* node_values,
    const float* cf_reach,
    std::uint32_t action_count,
    std::uint32_t bucket_count,
    float* regret) {
    if (action_values == nullptr || node_values == nullptr || cf_reach == nullptr || regret == nullptr) {
        return;
    }

    for (std::uint32_t action = 0; action < action_count; ++action) {
        const auto action_offset = action * bucket_count;
        for (std::uint32_t bucket = 0; bucket < bucket_count; ++bucket) {
            regret[action_offset + bucket] +=
                (action_values[action_offset + bucket] - node_values[bucket]) * cf_reach[bucket];
        }
    }
}

void saxpy_f32(std::uint32_t n, float alpha, const float* x, float* y) {
    if (x == nullptr || y == nullptr) {
        return;
    }

    for (std::uint32_t i = 0; i < n; ++i) {
        y[i] += alpha * x[i];
    }
}

double dot_f32_f64_accum(std::uint32_t n, const float* x, const float* y) {
    if (x == nullptr || y == nullptr) {
        return 0.0;
    }

    double total = 0.0;
    for (std::uint32_t i = 0; i < n; ++i) {
        total += static_cast<double>(x[i]) * static_cast<double>(y[i]);
    }
    return total;
}

}  // namespace core
