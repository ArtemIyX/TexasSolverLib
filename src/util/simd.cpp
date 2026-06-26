#include "util/simd.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

#if defined(__AVX2__) || defined(__SSE2__) || defined(_M_X64) || (defined(_M_IX86_FP) && _M_IX86_FP >= 2)
#include <immintrin.h>
#endif

namespace core {

namespace {

double nan_preserving_max(double a, double b) noexcept {
    if (std::isnan(a) || std::isnan(b)) {
        return std::numeric_limits<double>::quiet_NaN();
    }
    return a >= b ? a : b;
}

#if defined(__AVX2__) || defined(__SSE2__) || defined(_M_X64) || (defined(_M_IX86_FP) && _M_IX86_FP >= 2)
namespace {

inline void discount_regrets_sse2(double* regrets, std::size_t len, double pos_scale, double neg_scale) noexcept {
    const __m128d zero = _mm_set1_pd(0.0);
    const __m128d pos = _mm_set1_pd(pos_scale);
    const __m128d neg = _mm_set1_pd(neg_scale);
    std::size_t i = 0;
    for (; i + 1 < len; i += 2) {
        __m128d v = _mm_loadu_pd(regrets + i);
        const __m128d gt = _mm_cmpgt_pd(v, zero);
        const __m128d lt = _mm_cmplt_pd(v, zero);
        const __m128d pos_v = _mm_mul_pd(v, pos);
        const __m128d neg_v = _mm_mul_pd(v, neg);
        v = _mm_or_pd(_mm_and_pd(gt, pos_v), _mm_and_pd(lt, neg_v));
        _mm_storeu_pd(regrets + i, v);
    }
    for (; i < len; ++i) {
        if (regrets[i] > 0.0) {
            regrets[i] *= pos_scale;
        } else if (regrets[i] < 0.0) {
            regrets[i] *= neg_scale;
        }
    }
}

inline void discount_regrets_avx2(double* regrets, std::size_t len, double pos_scale, double neg_scale) noexcept {
    const __m256d zero = _mm256_set1_pd(0.0);
    const __m256d pos = _mm256_set1_pd(pos_scale);
    const __m256d neg = _mm256_set1_pd(neg_scale);
    std::size_t i = 0;
    for (; i + 3 < len; i += 4) {
        __m256d v = _mm256_loadu_pd(regrets + i);
        const __m256d gt = _mm256_cmp_pd(v, zero, _CMP_GT_OQ);
        const __m256d lt = _mm256_cmp_pd(v, zero, _CMP_LT_OQ);
        const __m256d pos_v = _mm256_mul_pd(v, pos);
        const __m256d neg_v = _mm256_mul_pd(v, neg);
        v = _mm256_or_pd(_mm256_and_pd(gt, pos_v), _mm256_and_pd(lt, neg_v));
        _mm256_storeu_pd(regrets + i, v);
    }
    discount_regrets_sse2(regrets + i, len - i, pos_scale, neg_scale);
}

inline void discount_strategy_sum_sse2(double* strategy, std::size_t len, double strat_scale) noexcept {
    const __m128d scale = _mm_set1_pd(strat_scale);
    std::size_t i = 0;
    for (; i + 1 < len; i += 2) {
        const __m128d v = _mm_loadu_pd(strategy + i);
        _mm_storeu_pd(strategy + i, _mm_mul_pd(v, scale));
    }
    for (; i < len; ++i) {
        strategy[i] *= strat_scale;
    }
}

inline void discount_strategy_sum_avx2(double* strategy, std::size_t len, double strat_scale) noexcept {
    const __m256d scale = _mm256_set1_pd(strat_scale);
    std::size_t i = 0;
    for (; i + 3 < len; i += 4) {
        const __m256d v = _mm256_loadu_pd(strategy + i);
        _mm256_storeu_pd(strategy + i, _mm256_mul_pd(v, scale));
    }
    discount_strategy_sum_sse2(strategy + i, len - i, strat_scale);
}

inline double positive_regrets_and_total_sse2(const double* regrets, double* out_positive, std::size_t len) noexcept {
    double total = 0.0;
    const __m128d zero = _mm_set1_pd(0.0);
    std::size_t i = 0;
    for (; i + 1 < len; i += 2) {
        const __m128d v = _mm_loadu_pd(regrets + i);
        const __m128d p = _mm_max_pd(v, zero);
        _mm_storeu_pd(out_positive + i, p);
        alignas(16) double tmp[2];
        _mm_store_pd(tmp, p);
        total += tmp[0] + tmp[1];
    }
    for (; i < len; ++i) {
        out_positive[i] = nan_preserving_max(regrets[i], 0.0);
        total += out_positive[i];
    }
    return total;
}

inline double positive_regrets_and_total_avx2(const double* regrets, double* out_positive, std::size_t len) noexcept {
    double total = 0.0;
    const __m256d zero = _mm256_set1_pd(0.0);
    std::size_t i = 0;
    for (; i + 3 < len; i += 4) {
        const __m256d v = _mm256_loadu_pd(regrets + i);
        const __m256d p = _mm256_max_pd(v, zero);
        _mm256_storeu_pd(out_positive + i, p);
        alignas(32) double tmp[4];
        _mm256_store_pd(tmp, p);
        total += tmp[0] + tmp[1] + tmp[2] + tmp[3];
    }
    return total + positive_regrets_and_total_sse2(regrets + i, out_positive + i, len - i);
}

inline void normalize_sse2(double* out, std::size_t len, double total) noexcept {
    if (total > 0.0) {
        const __m128d denom = _mm_set1_pd(total);
        std::size_t i = 0;
        for (; i + 1 < len; i += 2) {
            _mm_storeu_pd(out + i, _mm_div_pd(_mm_loadu_pd(out + i), denom));
        }
        for (; i < len; ++i) {
            out[i] /= total;
        }
    } else if (len > 0) {
        const auto uniform = 1.0 / static_cast<double>(len);
        std::fill(out, out + len, uniform);
    }
}

inline void normalize_avx2(double* out, std::size_t len, double total) noexcept {
    if (total > 0.0) {
        const __m256d denom = _mm256_set1_pd(total);
        std::size_t i = 0;
        for (; i + 3 < len; i += 4) {
            _mm256_storeu_pd(out + i, _mm256_div_pd(_mm256_loadu_pd(out + i), denom));
        }
        normalize_sse2(out + i, len - i, total);
    } else if (len > 0) {
        const auto uniform = 1.0 / static_cast<double>(len);
        std::fill(out, out + len, uniform);
    }
}

}
#endif

}

SimdBackend detect_simd_backend() noexcept {
#if defined(__aarch64__) || defined(_M_ARM64)
    return SimdBackend::Neon;
#elif defined(__AVX2__)
    return SimdBackend::Avx2;
#elif defined(__SSE2__) || defined(_M_X64) || (defined(_M_IX86_FP) && _M_IX86_FP >= 2)
    return SimdBackend::Sse2;
#else
    return SimdBackend::Scalar;
#endif
}

std::string_view simd_backend_name(SimdBackend backend) noexcept {
    switch (backend) {
        case SimdBackend::Scalar:
            return "scalar";
        case SimdBackend::Sse2:
            return "sse2";
        case SimdBackend::Avx2:
            return "avx2";
        case SimdBackend::Neon:
            return "neon";
    }
    return "unknown";
}

void discount_regrets_scalar(double* regrets, std::size_t len, double pos_scale, double neg_scale) noexcept {
    for (std::size_t i = 0; i < len; ++i) {
        if (regrets[i] > 0.0) {
            regrets[i] *= pos_scale;
        } else if (regrets[i] < 0.0) {
            regrets[i] *= neg_scale;
        }
    }
}

void discount_strategy_sum_scalar(double* strategy, std::size_t len, double strat_scale) noexcept {
    for (std::size_t i = 0; i < len; ++i) {
        strategy[i] *= strat_scale;
    }
}

double positive_regrets_and_total_scalar(const double* regrets, double* out_positive, std::size_t len) noexcept {
    double total = 0.0;
    for (std::size_t i = 0; i < len; ++i) {
        out_positive[i] = nan_preserving_max(regrets[i], 0.0);
        total += out_positive[i];
    }
    return total;
}

void update_regret_sum_scalar(
    double* regret_sum,
    const double* action_values,
    std::size_t len,
    double node_value,
    double opp_reach) noexcept {
    for (std::size_t i = 0; i < len; ++i) {
        regret_sum[i] += opp_reach * (action_values[i] - node_value);
    }
}

void update_strategy_sum_scalar(
    double* strategy_sum,
    const double* strategy,
    std::size_t len,
    double own_reach) noexcept {
    for (std::size_t i = 0; i < len; ++i) {
        strategy_sum[i] += own_reach * strategy[i];
    }
}

void update_regret_sum_vector_scalar(
    double* regret,
    const double* action_value,
    const double* node_value,
    std::size_t hand_count,
    std::size_t action_count) noexcept {
    for (std::size_t h = 0; h < hand_count; ++h) {
        const auto offset = h * action_count;
        const auto base = node_value[h];
        for (std::size_t a = 0; a < action_count; ++a) {
            regret[offset + a] += action_value[a * hand_count + h] - base;
        }
    }
}

void normalize_scalar(double* out, std::size_t len, double total) noexcept {
    if (total > 0.0) {
        for (std::size_t i = 0; i < len; ++i) {
            out[i] /= total;
        }
    } else if (len > 0) {
        const auto uniform = 1.0 / static_cast<double>(len);
        for (std::size_t i = 0; i < len; ++i) {
            out[i] = uniform;
        }
    }
}

void compute_strategy_row_scalar(const double* regrets, double* out, std::size_t len) noexcept {
    double normalizing = 0.0;
    for (std::size_t i = 0; i < len; ++i) {
        if (regrets[i] > 0.0) {
            normalizing += regrets[i];
        }
    }
    if (normalizing > 0.0) {
        for (std::size_t i = 0; i < len; ++i) {
            out[i] = regrets[i] > 0.0 ? regrets[i] / normalizing : 0.0;
        }
    } else if (len > 0) {
        const auto prob = 1.0 / static_cast<double>(len);
        for (std::size_t i = 0; i < len; ++i) {
            out[i] = prob;
        }
    }
}

void discount_regrets(double* regrets, std::size_t len, double pos_scale, double neg_scale) noexcept {
#if defined(__AVX2__)
    discount_regrets_avx2(regrets, len, pos_scale, neg_scale);
#elif defined(__SSE2__) || defined(_M_X64) || (defined(_M_IX86_FP) && _M_IX86_FP >= 2)
    discount_regrets_sse2(regrets, len, pos_scale, neg_scale);
#else
    discount_regrets_scalar(regrets, len, pos_scale, neg_scale);
#endif
}

void discount_strategy_sum(double* strategy, std::size_t len, double strat_scale) noexcept {
#if defined(__AVX2__)
    discount_strategy_sum_avx2(strategy, len, strat_scale);
#elif defined(__SSE2__) || defined(_M_X64) || (defined(_M_IX86_FP) && _M_IX86_FP >= 2)
    discount_strategy_sum_sse2(strategy, len, strat_scale);
#else
    discount_strategy_sum_scalar(strategy, len, strat_scale);
#endif
}

double positive_regrets_and_total(const double* regrets, double* out_positive, std::size_t len) noexcept {
#if defined(__AVX2__)
    return positive_regrets_and_total_avx2(regrets, out_positive, len);
#elif defined(__SSE2__) || defined(_M_X64) || (defined(_M_IX86_FP) && _M_IX86_FP >= 2)
    return positive_regrets_and_total_sse2(regrets, out_positive, len);
#else
    return positive_regrets_and_total_scalar(regrets, out_positive, len);
#endif
}

void update_regret_sum(
    double* regret_sum,
    const double* action_values,
    std::size_t len,
    double node_value,
    double opp_reach) noexcept {
    update_regret_sum_scalar(regret_sum, action_values, len, node_value, opp_reach);
}

void update_strategy_sum(
    double* strategy_sum,
    const double* strategy,
    std::size_t len,
    double own_reach) noexcept {
    update_strategy_sum_scalar(strategy_sum, strategy, len, own_reach);
}

void update_regret_sum_vector(
    double* regret,
    const double* action_value,
    const double* node_value,
    std::size_t hand_count,
    std::size_t action_count) noexcept {
    update_regret_sum_vector_scalar(regret, action_value, node_value, hand_count, action_count);
}

void normalize(double* out, std::size_t len, double total) noexcept {
#if defined(__AVX2__)
    normalize_avx2(out, len, total);
#elif defined(__SSE2__) || defined(_M_X64) || (defined(_M_IX86_FP) && _M_IX86_FP >= 2)
    normalize_sse2(out, len, total);
#else
    normalize_scalar(out, len, total);
#endif
}

void compute_strategy_row(const double* regrets, double* out, std::size_t len) noexcept {
    double total = positive_regrets_and_total(regrets, out, len);
    normalize(out, len, total);
}

}  // namespace core


