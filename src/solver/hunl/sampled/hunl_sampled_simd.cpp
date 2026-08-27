#include "solver/hunl/sampled/hunl_sampled_simd.hpp"

#include <algorithm>
#include <atomic>
#include <cmath>

#if defined(__AVX2__)
#include <immintrin.h>
#endif

namespace texas::solver::hunl {

namespace {

std::atomic<bool> g_hunl_sampled_simd_enabled{true};

bool use_sampled_avx2() noexcept {
#if defined(__AVX2__)
    return g_hunl_sampled_simd_enabled.load(std::memory_order_relaxed) &&
        detect_simd_backend() == SimdBackend::Avx2;
#else
    return false;
#endif
}

#if defined(__AVX2__)
void regret_matching_action_major_f32_avx2(
    const float* regret,
    std::uint32_t action_count,
    std::uint32_t bucket_count,
    float* strategy) {
    const auto zero = _mm256_setzero_ps();
    const auto uniform = _mm256_set1_ps(1.0f / static_cast<float>(action_count));
    for (std::uint32_t bucket = 0; bucket + 7U < bucket_count; bucket += 8U) {
        auto total = _mm256_setzero_ps();
        for (std::uint32_t action = 0; action < action_count; ++action) {
            const auto offset = static_cast<std::size_t>(action) * bucket_count + bucket;
            const auto values = _mm256_loadu_ps(regret + offset);
            if (_mm256_movemask_ps(_mm256_cmp_ps(values, values, _CMP_UNORD_Q)) != 0) {
                regret_matching_action_major_f32_scalar(regret, action_count, bucket_count, strategy);
                return;
            }
            const auto positive = _mm256_max_ps(values, zero);
            _mm256_storeu_ps(strategy + offset, positive);
            total = _mm256_add_ps(total, positive);
        }
        const auto has_positive = _mm256_cmp_ps(total, zero, _CMP_GT_OQ);
        for (std::uint32_t action = 0; action < action_count; ++action) {
            const auto offset = static_cast<std::size_t>(action) * bucket_count + bucket;
            const auto normalized = _mm256_div_ps(_mm256_loadu_ps(strategy + offset), total);
            _mm256_storeu_ps(strategy + offset, _mm256_blendv_ps(uniform, normalized, has_positive));
        }
    }
    for (std::uint32_t bucket = bucket_count - bucket_count % 8U; bucket < bucket_count; ++bucket) {
        float total = 0.0f;
        for (std::uint32_t action = 0; action < action_count; ++action) {
            const auto offset = static_cast<std::size_t>(action) * bucket_count + bucket;
            if (std::isnan(regret[offset])) {
                regret_matching_action_major_f32_scalar(regret, action_count, bucket_count, strategy);
                return;
            }
            const auto positive = std::max(regret[offset], 0.0f);
            strategy[offset] = positive;
            total += positive;
        }
        for (std::uint32_t action = 0; action < action_count; ++action) {
            const auto offset = static_cast<std::size_t>(action) * bucket_count + bucket;
            strategy[offset] = total > 0.0f ? strategy[offset] / total : 1.0f / static_cast<float>(action_count);
        }
    }
}

void regret_matching_action_major_f64_avx2(
    const double* regret,
    std::uint32_t action_count,
    std::uint32_t bucket_count,
    double* strategy) {
    const auto zero = _mm256_setzero_pd();
    const auto uniform = _mm256_set1_pd(1.0 / static_cast<double>(action_count));
    for (std::uint32_t bucket = 0; bucket + 3U < bucket_count; bucket += 4U) {
        auto total = _mm256_setzero_pd();
        for (std::uint32_t action = 0; action < action_count; ++action) {
            const auto offset = static_cast<std::size_t>(action) * bucket_count + bucket;
            const auto values = _mm256_loadu_pd(regret + offset);
            if (_mm256_movemask_pd(_mm256_cmp_pd(values, values, _CMP_UNORD_Q)) != 0) {
                regret_matching_action_major_f64_scalar(regret, action_count, bucket_count, strategy);
                return;
            }
            const auto positive = _mm256_max_pd(values, zero);
            _mm256_storeu_pd(strategy + offset, positive);
            total = _mm256_add_pd(total, positive);
        }
        const auto has_positive = _mm256_cmp_pd(total, zero, _CMP_GT_OQ);
        for (std::uint32_t action = 0; action < action_count; ++action) {
            const auto offset = static_cast<std::size_t>(action) * bucket_count + bucket;
            const auto normalized = _mm256_div_pd(_mm256_loadu_pd(strategy + offset), total);
            _mm256_storeu_pd(strategy + offset, _mm256_blendv_pd(uniform, normalized, has_positive));
        }
    }
    for (std::uint32_t bucket = bucket_count - bucket_count % 4U; bucket < bucket_count; ++bucket) {
        double total = 0.0;
        for (std::uint32_t action = 0; action < action_count; ++action) {
            const auto offset = static_cast<std::size_t>(action) * bucket_count + bucket;
            if (std::isnan(regret[offset])) {
                regret_matching_action_major_f64_scalar(regret, action_count, bucket_count, strategy);
                return;
            }
            const auto positive = std::max(regret[offset], 0.0);
            strategy[offset] = positive;
            total += positive;
        }
        for (std::uint32_t action = 0; action < action_count; ++action) {
            const auto offset = static_cast<std::size_t>(action) * bucket_count + bucket;
            strategy[offset] = total > 0.0 ? strategy[offset] / total : 1.0 / static_cast<double>(action_count);
        }
    }
}

void accumulate_average_strategy_action_major_f32_avx2(
    const float* strategy,
    const float* reach_or_weight,
    std::uint32_t action_count,
    std::uint32_t bucket_count,
    float scale,
    float* strategy_sum) {
    const auto scale_vec = _mm256_set1_ps(scale);
    for (std::uint32_t action = 0; action < action_count; ++action) {
        const auto action_offset = static_cast<std::size_t>(action) * bucket_count;
        std::uint32_t bucket = 0;
        for (; bucket + 7U < bucket_count; bucket += 8U) {
            const auto strat = _mm256_loadu_ps(strategy + action_offset + bucket);
            const auto reach = _mm256_loadu_ps(reach_or_weight + bucket);
            const auto sum = _mm256_loadu_ps(strategy_sum + action_offset + bucket);
            const auto delta = _mm256_mul_ps(_mm256_mul_ps(strat, reach), scale_vec);
            _mm256_storeu_ps(strategy_sum + action_offset + bucket, _mm256_add_ps(sum, delta));
        }
        for (; bucket < bucket_count; ++bucket) {
            strategy_sum[action_offset + bucket] +=
                strategy[action_offset + bucket] * reach_or_weight[bucket] * scale;
        }
    }
}

void add_regret_delta_action_major_f32_avx2(
    const float* action_values,
    const float* node_values,
    const float* cf_reach,
    std::uint32_t action_count,
    std::uint32_t bucket_count,
    float* regret) {
    for (std::uint32_t action = 0; action < action_count; ++action) {
        const auto action_offset = static_cast<std::size_t>(action) * bucket_count;
        std::uint32_t bucket = 0;
        for (; bucket + 7U < bucket_count; bucket += 8U) {
            const auto action_vec = _mm256_loadu_ps(action_values + action_offset + bucket);
            const auto node_vec = _mm256_loadu_ps(node_values + bucket);
            const auto reach_vec = _mm256_loadu_ps(cf_reach + bucket);
            const auto regret_vec = _mm256_loadu_ps(regret + action_offset + bucket);
            const auto delta = _mm256_mul_ps(_mm256_sub_ps(action_vec, node_vec), reach_vec);
            _mm256_storeu_ps(regret + action_offset + bucket, _mm256_add_ps(regret_vec, delta));
        }
        for (; bucket < bucket_count; ++bucket) {
            regret[action_offset + bucket] +=
                (action_values[action_offset + bucket] - node_values[bucket]) * cf_reach[bucket];
        }
    }
}

void accumulate_average_strategy_action_major_f64_avx2(
    const double* strategy,
    const double* reach_or_weight,
    std::uint32_t action_count,
    std::uint32_t bucket_count,
    double scale,
    double* strategy_sum) {
    const auto scale_vec = _mm256_set1_pd(scale);
    for (std::uint32_t action = 0; action < action_count; ++action) {
        const auto action_offset = static_cast<std::size_t>(action) * bucket_count;
        std::uint32_t bucket = 0;
        for (; bucket + 3U < bucket_count; bucket += 4U) {
            const auto strat = _mm256_loadu_pd(strategy + action_offset + bucket);
            const auto reach = _mm256_loadu_pd(reach_or_weight + bucket);
            const auto sum = _mm256_loadu_pd(strategy_sum + action_offset + bucket);
            const auto delta = _mm256_mul_pd(_mm256_mul_pd(strat, reach), scale_vec);
            _mm256_storeu_pd(strategy_sum + action_offset + bucket, _mm256_add_pd(sum, delta));
        }
        for (; bucket < bucket_count; ++bucket) {
            strategy_sum[action_offset + bucket] +=
                strategy[action_offset + bucket] * reach_or_weight[bucket] * scale;
        }
    }
}

void add_regret_delta_action_major_f64_avx2(
    const double* action_values,
    const double* node_values,
    const double* cf_reach,
    std::uint32_t action_count,
    std::uint32_t bucket_count,
    double* regret) {
    for (std::uint32_t action = 0; action < action_count; ++action) {
        const auto action_offset = static_cast<std::size_t>(action) * bucket_count;
        std::uint32_t bucket = 0;
        for (; bucket + 3U < bucket_count; bucket += 4U) {
            const auto action_vec = _mm256_loadu_pd(action_values + action_offset + bucket);
            const auto node_vec = _mm256_loadu_pd(node_values + bucket);
            const auto reach_vec = _mm256_loadu_pd(cf_reach + bucket);
            const auto regret_vec = _mm256_loadu_pd(regret + action_offset + bucket);
            const auto delta = _mm256_mul_pd(_mm256_sub_pd(action_vec, node_vec), reach_vec);
            _mm256_storeu_pd(regret + action_offset + bucket, _mm256_add_pd(regret_vec, delta));
        }
        for (; bucket < bucket_count; ++bucket) {
            regret[action_offset + bucket] +=
                (action_values[action_offset + bucket] - node_values[bucket]) * cf_reach[bucket];
        }
    }
}

double weighted_sum_f32_f64_accum_avx2(
    std::uint32_t n,
    const float* values,
    const float* weights) {
    std::uint32_t i = 0;
    __m256 sum = _mm256_setzero_ps();
    for (; i + 7U < n; i += 8U) {
        const auto vv = _mm256_loadu_ps(values + i);
        const auto ww = _mm256_loadu_ps(weights + i);
        sum = _mm256_add_ps(sum, _mm256_mul_ps(vv, ww));
    }
    alignas(32) float tmp[8];
    _mm256_store_ps(tmp, sum);
    double total = 0.0;
    for (float value : tmp) {
        total += static_cast<double>(value);
    }
    for (; i < n; ++i) {
        total += static_cast<double>(values[i]) * static_cast<double>(weights[i]);
    }
    return total;
}
#endif

}  // namespace

void set_hunl_sampled_simd_enabled(bool enabled) noexcept {
    g_hunl_sampled_simd_enabled.store(enabled, std::memory_order_relaxed);
}

bool hunl_sampled_simd_enabled() noexcept {
    return g_hunl_sampled_simd_enabled.load(std::memory_order_relaxed);
}

HUNLSampledSimdBackend hunl_sampled_simd_backend() noexcept {
    return use_sampled_avx2() ? HUNLSampledSimdBackend::Avx2Fma : HUNLSampledSimdBackend::Scalar;
}

const char* hunl_sampled_simd_backend_name(HUNLSampledSimdBackend backend) noexcept {
    switch (backend) {
        case HUNLSampledSimdBackend::Scalar:
            return "scalar";
        case HUNLSampledSimdBackend::Avx2Fma:
            return "avx2-fma";
    }
    return "unknown";
}

void regret_matching_action_major_f32_scalar(
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
            const auto index = static_cast<std::size_t>(action) * bucket_count + bucket;
            const auto positive = std::max(regret[index], 0.0f);
            strategy[index] = positive;
            positive_total += positive;
        }

        if (positive_total > 0.0f) {
            for (std::uint32_t action = 0; action < action_count; ++action) {
                const auto index = static_cast<std::size_t>(action) * bucket_count + bucket;
                strategy[index] /= positive_total;
            }
        } else {
            const auto uniform = 1.0f / static_cast<float>(action_count);
            for (std::uint32_t action = 0; action < action_count; ++action) {
                const auto index = static_cast<std::size_t>(action) * bucket_count + bucket;
                strategy[index] = uniform;
            }
        }
    }
}

void regret_matching_action_major_f32(
    const float* regret,
    std::uint32_t action_count,
    std::uint32_t bucket_count,
    float* strategy) {
    if (regret == nullptr || strategy == nullptr || action_count == 0U || bucket_count == 0U) {
        return;
    }
#if defined(__AVX2__)
    if (use_sampled_avx2()) {
        regret_matching_action_major_f32_avx2(regret, action_count, bucket_count, strategy);
        return;
    }
#endif
    regret_matching_action_major_f32_scalar(regret, action_count, bucket_count, strategy);
}

void regret_matching_action_major_f64_scalar(
    const double* regret,
    std::uint32_t action_count,
    std::uint32_t bucket_count,
    double* strategy) {
    if (regret == nullptr || strategy == nullptr || action_count == 0 || bucket_count == 0) {
        return;
    }

    for (std::uint32_t bucket = 0; bucket < bucket_count; ++bucket) {
        double positive_total = 0.0;
        for (std::uint32_t action = 0; action < action_count; ++action) {
            const auto index = static_cast<std::size_t>(action) * bucket_count + bucket;
            const auto positive = std::max(regret[index], 0.0);
            strategy[index] = positive;
            positive_total += positive;
        }

        if (positive_total > 0.0) {
            for (std::uint32_t action = 0; action < action_count; ++action) {
                const auto index = static_cast<std::size_t>(action) * bucket_count + bucket;
                strategy[index] /= positive_total;
            }
        } else {
            const auto uniform = 1.0 / static_cast<double>(action_count);
            for (std::uint32_t action = 0; action < action_count; ++action) {
                const auto index = static_cast<std::size_t>(action) * bucket_count + bucket;
                strategy[index] = uniform;
            }
        }
    }
}

void regret_matching_action_major_f64(
    const double* regret,
    std::uint32_t action_count,
    std::uint32_t bucket_count,
    double* strategy) {
    if (regret == nullptr || strategy == nullptr || action_count == 0U || bucket_count == 0U) {
        return;
    }
#if defined(__AVX2__)
    if (use_sampled_avx2()) {
        regret_matching_action_major_f64_avx2(regret, action_count, bucket_count, strategy);
        return;
    }
#endif
    regret_matching_action_major_f64_scalar(regret, action_count, bucket_count, strategy);
}

void accumulate_average_strategy_action_major_f32_scalar(
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
        const auto action_offset = static_cast<std::size_t>(action) * bucket_count;
        for (std::uint32_t bucket = 0; bucket < bucket_count; ++bucket) {
            strategy_sum[action_offset + bucket] +=
                strategy[action_offset + bucket] * reach_or_weight[bucket] * scale;
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
#if defined(__AVX2__)
    if (use_sampled_avx2()) {
        accumulate_average_strategy_action_major_f32_avx2(
            strategy,
            reach_or_weight,
            action_count,
            bucket_count,
            scale,
            strategy_sum);
        return;
    }
#endif
    accumulate_average_strategy_action_major_f32_scalar(
        strategy,
        reach_or_weight,
        action_count,
        bucket_count,
        scale,
        strategy_sum);
}

void accumulate_average_strategy_action_major_f64_scalar(
    const double* strategy,
    const double* reach_or_weight,
    std::uint32_t action_count,
    std::uint32_t bucket_count,
    double scale,
    double* strategy_sum) {
    if (strategy == nullptr || reach_or_weight == nullptr || strategy_sum == nullptr) {
        return;
    }

    for (std::uint32_t action = 0; action < action_count; ++action) {
        const auto action_offset = static_cast<std::size_t>(action) * bucket_count;
        for (std::uint32_t bucket = 0; bucket < bucket_count; ++bucket) {
            strategy_sum[action_offset + bucket] +=
                strategy[action_offset + bucket] * reach_or_weight[bucket] * scale;
        }
    }
}

void accumulate_average_strategy_action_major_f64(
    const double* strategy,
    const double* reach_or_weight,
    std::uint32_t action_count,
    std::uint32_t bucket_count,
    double scale,
    double* strategy_sum) {
#if defined(__AVX2__)
    if (use_sampled_avx2()) {
        accumulate_average_strategy_action_major_f64_avx2(
            strategy,
            reach_or_weight,
            action_count,
            bucket_count,
            scale,
            strategy_sum);
        return;
    }
#endif
    accumulate_average_strategy_action_major_f64_scalar(
        strategy,
        reach_or_weight,
        action_count,
        bucket_count,
        scale,
        strategy_sum);
}

void add_regret_delta_action_major_f32_scalar(
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
        const auto action_offset = static_cast<std::size_t>(action) * bucket_count;
        for (std::uint32_t bucket = 0; bucket < bucket_count; ++bucket) {
            regret[action_offset + bucket] +=
                (action_values[action_offset + bucket] - node_values[bucket]) * cf_reach[bucket];
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
#if defined(__AVX2__)
    if (use_sampled_avx2()) {
        add_regret_delta_action_major_f32_avx2(
            action_values,
            node_values,
            cf_reach,
            action_count,
            bucket_count,
            regret);
        return;
    }
#endif
    add_regret_delta_action_major_f32_scalar(
        action_values,
        node_values,
        cf_reach,
        action_count,
        bucket_count,
        regret);
}

void add_regret_delta_action_major_f64_scalar(
    const double* action_values,
    const double* node_values,
    const double* cf_reach,
    std::uint32_t action_count,
    std::uint32_t bucket_count,
    double* regret) {
    if (action_values == nullptr || node_values == nullptr || cf_reach == nullptr || regret == nullptr) {
        return;
    }

    for (std::uint32_t action = 0; action < action_count; ++action) {
        const auto action_offset = static_cast<std::size_t>(action) * bucket_count;
        for (std::uint32_t bucket = 0; bucket < bucket_count; ++bucket) {
            regret[action_offset + bucket] +=
                (action_values[action_offset + bucket] - node_values[bucket]) * cf_reach[bucket];
        }
    }
}

void add_regret_delta_action_major_f64(
    const double* action_values,
    const double* node_values,
    const double* cf_reach,
    std::uint32_t action_count,
    std::uint32_t bucket_count,
    double* regret) {
#if defined(__AVX2__)
    if (use_sampled_avx2()) {
        add_regret_delta_action_major_f64_avx2(
            action_values,
            node_values,
            cf_reach,
            action_count,
            bucket_count,
            regret);
        return;
    }
#endif
    add_regret_delta_action_major_f64_scalar(
        action_values,
        node_values,
        cf_reach,
        action_count,
        bucket_count,
        regret);
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

double weighted_sum_f32_f64_accum(std::uint32_t n, const float* values, const float* weights) {
    if (values == nullptr || weights == nullptr) {
        return 0.0;
    }
#if defined(__AVX2__)
    if (use_sampled_avx2()) {
        return weighted_sum_f32_f64_accum_avx2(n, values, weights);
    }
#endif
    return dot_f32_f64_accum(n, values, weights);
}

}  // namespace texas::solver::hunl
