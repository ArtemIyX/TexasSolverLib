#include "core/simd.hpp"

#include <cmath>
#include <limits>

namespace core {

namespace {

double nan_preserving_max(double a, double b) noexcept {
    if (std::isnan(a) || std::isnan(b)) {
        return std::numeric_limits<double>::quiet_NaN();
    }
    return a >= b ? a : b;
}

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
    discount_regrets_scalar(regrets, len, pos_scale, neg_scale);
}

void discount_strategy_sum(double* strategy, std::size_t len, double strat_scale) noexcept {
    discount_strategy_sum_scalar(strategy, len, strat_scale);
}

double positive_regrets_and_total(const double* regrets, double* out_positive, std::size_t len) noexcept {
    return positive_regrets_and_total_scalar(regrets, out_positive, len);
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
    normalize_scalar(out, len, total);
}

void compute_strategy_row(const double* regrets, double* out, std::size_t len) noexcept {
    compute_strategy_row_scalar(regrets, out, len);
}

}  // namespace core
