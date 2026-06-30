#include "util/simd.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
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

bool is_aligned_32(const void* ptr) noexcept {
    return (reinterpret_cast<std::uintptr_t>(ptr) & 31U) == 0;
}

bool is_aligned_16(const void* ptr) noexcept {
    return (reinterpret_cast<std::uintptr_t>(ptr) & 15U) == 0;
}

#if defined(__AVX2__) || defined(__SSE2__) || defined(_M_X64) || (defined(_M_IX86_FP) && _M_IX86_FP >= 2)
inline void store_row(double* out, const double* in, bool aligned) noexcept {
#if defined(__AVX2__)
    if (aligned) {
        _mm256_store_pd(out, _mm256_load_pd(in));
    } else {
        _mm256_storeu_pd(out, _mm256_loadu_pd(in));
    }
#else
    if (aligned) {
        _mm_store_pd(out, _mm_load_pd(in));
    } else {
        _mm_storeu_pd(out, _mm_loadu_pd(in));
    }
#endif
}

inline double sum_row_chunk(const double* values, bool aligned) noexcept {
#if defined(__AVX2__)
    return aligned ? _mm256_cvtsd_f64(_mm256_castpd256_pd128(_mm256_load_pd(values))) : _mm256_cvtsd_f64(_mm256_castpd256_pd128(_mm256_loadu_pd(values)));
#else
    return aligned ? _mm_cvtsd_f64(_mm_load_pd(values)) : _mm_cvtsd_f64(_mm_loadu_pd(values));
#endif
}
#endif

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
        const __m128d nan_mask = _mm_cmpunord_pd(v, v);
        const __m128d positive = _mm_max_pd(v, zero);
        const __m128d p = _mm_or_pd(_mm_and_pd(nan_mask, v), _mm_andnot_pd(nan_mask, positive));
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
        const __m256d nan_mask = _mm256_cmp_pd(v, v, _CMP_UNORD_Q);
        const __m256d positive = _mm256_max_pd(v, zero);
        const __m256d p =
            _mm256_or_pd(_mm256_and_pd(nan_mask, v), _mm256_andnot_pd(nan_mask, positive));
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

inline void update_regret_sum_sse2(
    double* regret_sum,
    const double* action_values,
    std::size_t len,
    double node_value,
    double opp_reach) noexcept {
    const __m128d reach = _mm_set1_pd(opp_reach);
    const __m128d node = _mm_set1_pd(node_value);
    std::size_t i = 0;
    for (; i + 1 < len; i += 2) {
        const __m128d regret = _mm_loadu_pd(regret_sum + i);
        const __m128d action = _mm_loadu_pd(action_values + i);
        const __m128d delta = _mm_mul_pd(reach, _mm_sub_pd(action, node));
        _mm_storeu_pd(regret_sum + i, _mm_add_pd(regret, delta));
    }
    for (; i < len; ++i) {
        regret_sum[i] += opp_reach * (action_values[i] - node_value);
    }
}

inline void update_regret_sum_avx2(
    double* regret_sum,
    const double* action_values,
    std::size_t len,
    double node_value,
    double opp_reach) noexcept {
    const __m256d reach = _mm256_set1_pd(opp_reach);
    const __m256d node = _mm256_set1_pd(node_value);
    std::size_t i = 0;
    for (; i + 3 < len; i += 4) {
        const __m256d regret = _mm256_loadu_pd(regret_sum + i);
        const __m256d action = _mm256_loadu_pd(action_values + i);
        const __m256d delta = _mm256_mul_pd(reach, _mm256_sub_pd(action, node));
        _mm256_storeu_pd(regret_sum + i, _mm256_add_pd(regret, delta));
    }
    update_regret_sum_sse2(regret_sum + i, action_values + i, len - i, node_value, opp_reach);
}

inline void update_strategy_sum_sse2(
    double* strategy_sum,
    const double* strategy,
    std::size_t len,
    double own_reach) noexcept {
    const __m128d reach = _mm_set1_pd(own_reach);
    std::size_t i = 0;
    for (; i + 1 < len; i += 2) {
        const __m128d sum = _mm_loadu_pd(strategy_sum + i);
        const __m128d strat = _mm_loadu_pd(strategy + i);
        _mm_storeu_pd(strategy_sum + i, _mm_add_pd(sum, _mm_mul_pd(reach, strat)));
    }
    for (; i < len; ++i) {
        strategy_sum[i] += own_reach * strategy[i];
    }
}

inline void update_strategy_sum_avx2(
    double* strategy_sum,
    const double* strategy,
    std::size_t len,
    double own_reach) noexcept {
    const __m256d reach = _mm256_set1_pd(own_reach);
    std::size_t i = 0;
    for (; i + 3 < len; i += 4) {
        const __m256d sum = _mm256_loadu_pd(strategy_sum + i);
        const __m256d strat = _mm256_loadu_pd(strategy + i);
        _mm256_storeu_pd(strategy_sum + i, _mm256_add_pd(sum, _mm256_mul_pd(reach, strat)));
    }
    update_strategy_sum_sse2(strategy_sum + i, strategy + i, len - i, own_reach);
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
#if defined(__AVX2__)
    update_regret_sum_avx2(regret_sum, action_values, len, node_value, opp_reach);
#elif defined(__SSE2__) || defined(_M_X64) || (defined(_M_IX86_FP) && _M_IX86_FP >= 2)
    update_regret_sum_sse2(regret_sum, action_values, len, node_value, opp_reach);
#else
    update_regret_sum_scalar(regret_sum, action_values, len, node_value, opp_reach);
#endif
}

void update_strategy_sum(
    double* strategy_sum,
    const double* strategy,
    std::size_t len,
    double own_reach) noexcept {
    switch (len) {
        case 0:
            return;
        case 1:
            strategy_sum[0] += own_reach * strategy[0];
            return;
        case 2:
            strategy_sum[0] += own_reach * strategy[0];
            strategy_sum[1] += own_reach * strategy[1];
            return;
        case 3:
            strategy_sum[0] += own_reach * strategy[0];
            strategy_sum[1] += own_reach * strategy[1];
            strategy_sum[2] += own_reach * strategy[2];
            return;
        case 4:
            strategy_sum[0] += own_reach * strategy[0];
            strategy_sum[1] += own_reach * strategy[1];
            strategy_sum[2] += own_reach * strategy[2];
            strategy_sum[3] += own_reach * strategy[3];
            return;
        default:
            break;
    }
#if defined(__AVX2__)
    update_strategy_sum_avx2(strategy_sum, strategy, len, own_reach);
#elif defined(__SSE2__) || defined(_M_X64) || (defined(_M_IX86_FP) && _M_IX86_FP >= 2)
    update_strategy_sum_sse2(strategy_sum, strategy, len, own_reach);
#else
    update_strategy_sum_scalar(strategy_sum, strategy, len, own_reach);
#endif
}

void copy_values(double* out, const double* in, std::size_t len) noexcept {
    switch (len) {
        case 0:
            return;
        case 1:
            out[0] = in[0];
            return;
        case 2:
            out[0] = in[0];
            out[1] = in[1];
            return;
        case 3:
            out[0] = in[0];
            out[1] = in[1];
            out[2] = in[2];
            return;
        case 4:
            out[0] = in[0];
            out[1] = in[1];
            out[2] = in[2];
            out[3] = in[3];
            return;
        default:
            break;
    }
#if defined(__AVX2__)
    std::size_t i = 0;
    const bool aligned = is_aligned_32(out) && is_aligned_32(in);
    for (; i + 3 < len; i += 4) {
        if (aligned) {
            _mm256_store_pd(out + i, _mm256_load_pd(in + i));
        } else {
            _mm256_storeu_pd(out + i, _mm256_loadu_pd(in + i));
        }
    }
    for (; i < len; ++i) {
        out[i] = in[i];
    }
#elif defined(__SSE2__) || defined(_M_X64) || (defined(_M_IX86_FP) && _M_IX86_FP >= 2)
    std::size_t i = 0;
    const bool aligned = is_aligned_16(out) && is_aligned_16(in);
    for (; i + 1 < len; i += 2) {
        if (aligned) {
            _mm_store_pd(out + i, _mm_load_pd(in + i));
        } else {
            _mm_storeu_pd(out + i, _mm_loadu_pd(in + i));
        }
    }
    for (; i < len; ++i) {
        out[i] = in[i];
    }
#else
    for (std::size_t i = 0; i < len; ++i) {
        out[i] = in[i];
    }
#endif
}

double reduce_action_values(const double* values, std::size_t len) noexcept {
    if (len == 0) {
        return 0.0;
    }

    switch (len) {
        case 1:
            return values[0];
        case 2:
            return values[0] + values[1];
        case 3:
            return values[0] + values[1] + values[2];
        case 4:
            return values[0] + values[1] + values[2] + values[3];
        default:
#if defined(__AVX2__)
        {
            std::size_t i = 0;
            __m256d sum = _mm256_setzero_pd();
            const bool aligned = is_aligned_32(values);
            for (; i + 3 < len; i += 4) {
                const __m256d chunk = aligned ? _mm256_load_pd(values + i) : _mm256_loadu_pd(values + i);
                sum = _mm256_add_pd(sum, chunk);
            }
            alignas(32) double tmp[4];
            _mm256_store_pd(tmp, sum);
            double total = tmp[0] + tmp[1] + tmp[2] + tmp[3];
            for (; i < len; ++i) {
                total += values[i];
            }
            return total;
        }
#elif defined(__SSE2__) || defined(_M_X64) || (defined(_M_IX86_FP) && _M_IX86_FP >= 2)
        {
            std::size_t i = 0;
            __m128d sum = _mm_setzero_pd();
            const bool aligned = is_aligned_16(values);
            for (; i + 1 < len; i += 2) {
                const __m128d chunk = aligned ? _mm_load_pd(values + i) : _mm_loadu_pd(values + i);
                sum = _mm_add_pd(sum, chunk);
            }
            alignas(16) double tmp[2];
            _mm_store_pd(tmp, sum);
            double total = tmp[0] + tmp[1];
            for (; i < len; ++i) {
                total += values[i];
            }
            return total;
        }
#else
        double total = 0.0;
        for (std::size_t i = 0; i < len; ++i) {
            total += values[i];
        }
        return total;
#endif
    }
}

double reduce_weighted_action_values(const double* values, const double* weights, std::size_t len) noexcept {
    if (len == 0) {
        return 0.0;
    }

    switch (len) {
        case 1:
            return values[0] * weights[0];
        case 2:
            return values[0] * weights[0] + values[1] * weights[1];
        case 3:
            return values[0] * weights[0] + values[1] * weights[1] + values[2] * weights[2];
        case 4:
            return values[0] * weights[0] + values[1] * weights[1] + values[2] * weights[2] + values[3] * weights[3];
        default:
#if defined(__AVX2__)
        {
            std::size_t i = 0;
            __m256d sum = _mm256_setzero_pd();
            const bool aligned = is_aligned_32(values) && is_aligned_32(weights);
            for (; i + 3 < len; i += 4) {
                const __m256d v = aligned ? _mm256_load_pd(values + i) : _mm256_loadu_pd(values + i);
                const __m256d w = aligned ? _mm256_load_pd(weights + i) : _mm256_loadu_pd(weights + i);
                sum = _mm256_add_pd(sum, _mm256_mul_pd(v, w));
            }
            alignas(32) double tmp[4];
            _mm256_store_pd(tmp, sum);
            double total = tmp[0] + tmp[1] + tmp[2] + tmp[3];
            for (; i < len; ++i) {
                total += values[i] * weights[i];
            }
            return total;
        }
#elif defined(__SSE2__) || defined(_M_X64) || (defined(_M_IX86_FP) && _M_IX86_FP >= 2)
        {
            std::size_t i = 0;
            __m128d sum = _mm_setzero_pd();
            const bool aligned = is_aligned_16(values) && is_aligned_16(weights);
            for (; i + 1 < len; i += 2) {
                const __m128d v = aligned ? _mm_load_pd(values + i) : _mm_loadu_pd(values + i);
                const __m128d w = aligned ? _mm_load_pd(weights + i) : _mm_loadu_pd(weights + i);
                sum = _mm_add_pd(sum, _mm_mul_pd(v, w));
            }
            alignas(16) double tmp[2];
            _mm_store_pd(tmp, sum);
            double total = tmp[0] + tmp[1];
            for (; i < len; ++i) {
                total += values[i] * weights[i];
            }
            return total;
        }
#else
        double total = 0.0;
        for (std::size_t i = 0; i < len; ++i) {
            total += values[i] * weights[i];
        }
        return total;
#endif
    }
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

void normalize_row(const double* values, double* out, std::size_t len) noexcept {
    switch (len) {
        case 0:
            return;
        case 1:
            out[0] = values[0] > 0.0 ? 1.0 : 1.0;
            return;
        case 2:
        case 3:
        case 4: {
            double total = 0.0;
            for (std::size_t i = 0; i < len; ++i) {
                total += values[i];
                out[i] = values[i];
            }
            normalize(out, len, total);
            return;
        }
        default:
            break;
    }
    double total = 0.0;
    for (std::size_t i = 0; i < len; ++i) {
        total += values[i];
        out[i] = values[i];
    }
    normalize(out, len, total);
}

void compute_strategy_row(const double* regrets, double* out, std::size_t len) noexcept {
    double total = positive_regrets_and_total(regrets, out, len);
    normalize(out, len, total);
}

void compute_strategy_row_small(const double* regrets, double* out, std::size_t len) noexcept {
    if (len == 0) {
        return;
    }

    switch (len) {
        case 1:
            out[0] = 1.0;
            return;
        case 2:
        case 3:
        case 4: {
            double positive[4] = {0.0, 0.0, 0.0, 0.0};
            const double total = positive_regrets_and_total(regrets, positive, len);
            normalize(positive, len, total);
            for (std::size_t i = 0; i < len; ++i) {
                out[i] = positive[i];
            }
            return;
        }
        default:
            compute_strategy_row(regrets, out, len);
            return;
    }
}

}  // namespace core


