#pragma once

#include <cstddef>
#include <string_view>

namespace core {

enum class SimdBackend {
    Scalar = 0,
    Sse2 = 1,
    Avx2 = 2,
    Neon = 3,
};

SimdBackend detect_simd_backend() noexcept;
std::string_view simd_backend_name(SimdBackend backend) noexcept;

void discount_regrets_scalar(double* regrets, std::size_t len, double pos_scale, double neg_scale) noexcept;
void discount_strategy_sum_scalar(double* strategy, std::size_t len, double strat_scale) noexcept;
double positive_regrets_and_total_scalar(const double* regrets, double* out_positive, std::size_t len) noexcept;
void update_regret_sum_scalar(
    double* regret_sum,
    const double* action_values,
    std::size_t len,
    double node_value,
    double opp_reach) noexcept;
void update_strategy_sum_scalar(
    double* strategy_sum,
    const double* strategy,
    std::size_t len,
    double own_reach) noexcept;
void update_regret_sum_vector_scalar(
    double* regret,
    const double* action_value,
    const double* node_value,
    std::size_t hand_count,
    std::size_t action_count) noexcept;
void normalize_scalar(double* out, std::size_t len, double total) noexcept;
void compute_strategy_row_scalar(const double* regrets, double* out, std::size_t len) noexcept;

void discount_regrets(double* regrets, std::size_t len, double pos_scale, double neg_scale) noexcept;
void discount_strategy_sum(double* strategy, std::size_t len, double strat_scale) noexcept;
double positive_regrets_and_total(const double* regrets, double* out_positive, std::size_t len) noexcept;
void update_regret_sum(
    double* regret_sum,
    const double* action_values,
    std::size_t len,
    double node_value,
    double opp_reach) noexcept;
void update_strategy_sum(
    double* strategy_sum,
    const double* strategy,
    std::size_t len,
    double own_reach) noexcept;
void update_regret_sum_vector(
    double* regret,
    const double* action_value,
    const double* node_value,
    std::size_t hand_count,
    std::size_t action_count) noexcept;
void normalize(double* out, std::size_t len, double total) noexcept;
void compute_strategy_row(const double* regrets, double* out, std::size_t len) noexcept;

}  // namespace core
