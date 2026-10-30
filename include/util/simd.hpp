#pragma once

/** @file
 * Scalar and selected SIMD kernels for solver numeric arrays.
 */

#include <cstddef>
#include <string_view>

namespace texas::util
{

	/** Runtime backend selected for vectorized solver kernels. */
	enum class SimdBackend
	{
		Scalar = 0,
		Sse2 = 1,
		Avx2 = 2,
		Neon = 3,
	};

	/** Detects the best supported runtime backend. */
	SimdBackend detect_simd_backend() noexcept;
	/** Returns a stable display name for a backend. */
	std::string_view simd_backend_name(SimdBackend backend) noexcept;

	/** Scalar reference for regret discounting with separate sign scales. */
	void discount_regrets_scalar(double* regrets, std::size_t len, double pos_scale, double neg_scale) noexcept;
	/** Scalar reference for strategy-sum discounting. */
	void discount_strategy_sum_scalar(double* strategy, std::size_t len, double strat_scale) noexcept;
	/** Writes positive regrets and returns their sum. */
	double positive_regrets_and_total_scalar(const double* regrets, double* out_positive, std::size_t len) noexcept;
	/** Applies one scalar regret update across actions. */
	void update_regret_sum_scalar(
		double* regret_sum,
		const double* action_values,
		std::size_t len,
		double node_value,
		double opp_reach) noexcept;
	/** Applies a scalar regret update using strided action values. */
	void update_regret_sum_strided_scalar(
		double* regret_sum,
		const double* action_values,
		std::size_t len,
		std::size_t stride,
		double node_value,
		double opp_reach) noexcept;
	/** Adds own-reach-weighted strategy mass to every element. */
	void update_strategy_sum_scalar(
		double* strategy_sum,
		const double* strategy,
		std::size_t len,
		double own_reach) noexcept;
	/** Adds own-reach-weighted strategy mass using a fixed stride. */
	void update_strategy_sum_strided_scalar(
		double* strategy_sum,
		const double* strategy,
		std::size_t len,
		std::size_t stride,
		double own_reach) noexcept;
	/** Updates action-major regret values for a set of hands. */
	void update_regret_sum_vector_scalar(
		double* regret,
		const double* action_value,
		const double* node_value,
		std::size_t hand_count,
		std::size_t action_count) noexcept;
	/** Returns the scalar dot product of two contiguous arrays. */
	double dot_product_scalar(const double* lhs, const double* rhs, std::size_t len) noexcept;
	/** Returns a dot product using a strided right-hand array. */
	double dot_product_strided_scalar(
		const double* lhs,
		const double* rhs,
		std::size_t len,
		std::size_t rhs_stride) noexcept;
	/** Normalizes values into out using a caller-provided total. */
	void normalize_scalar(double* out, std::size_t len, double total) noexcept;
	/** Builds a strategy row from positive regrets. */
	void compute_strategy_row_scalar(const double* regrets, double* out, std::size_t len) noexcept;
	/** Scalar reference kernels for single precision accumulator rows. */
	void discount_regrets_f32_scalar(float* regrets, std::size_t len, double pos_scale, double neg_scale) noexcept;
	void discount_strategy_sum_f32_scalar(float* strategy, std::size_t len, double strat_scale) noexcept;

	/** Dispatches regret discounting to the selected runtime backend. */
	void discount_regrets(double* regrets, std::size_t len, double pos_scale, double neg_scale) noexcept;
	/** Dispatches strategy-sum discounting to the selected runtime backend. */
	void discount_strategy_sum(double* strategy, std::size_t len, double strat_scale) noexcept;
	/** Dispatches positive-regret reduction and returns its total. */
	double positive_regrets_and_total(const double* regrets, double* out_positive, std::size_t len) noexcept;
	/** Dispatches the regret update over contiguous action values. */
	void update_regret_sum(
		double* regret_sum,
		const double* action_values,
		std::size_t len,
		double node_value,
		double opp_reach) noexcept;
	/** Dispatches the regret update using strided action values. */
	void update_regret_sum_strided(
		double* regret_sum,
		const double* action_values,
		std::size_t len,
		std::size_t stride,
		double node_value,
		double opp_reach) noexcept;
	/** Dispatches strategy accumulation over contiguous values. */
	void update_strategy_sum(
		double* strategy_sum,
		const double* strategy,
		std::size_t len,
		double own_reach) noexcept;
	/** Dispatches strategy accumulation using a fixed stride. */
	void update_strategy_sum_strided(
		double* strategy_sum,
		const double* strategy,
		std::size_t len,
		std::size_t stride,
		double own_reach) noexcept;
	/** Copies len double values through the selected backend. */
	void copy_values(double* out, const double* in, std::size_t len) noexcept;
	/** Computes a contiguous dot product through the selected backend. */
	double dot_product(const double* lhs, const double* rhs, std::size_t len) noexcept;
	/** Computes a dot product with a strided right-hand array. */
	double dot_product_strided(
		const double* lhs,
		const double* rhs,
		std::size_t len,
		std::size_t rhs_stride) noexcept;
	/** Reduces unweighted action values to one scalar. */
	double reduce_action_values(const double* values, std::size_t len) noexcept;
	/** Reduces action values using the corresponding weights. */
	double reduce_weighted_action_values(const double* values, const double* weights, std::size_t len) noexcept;
	/** Updates action-major regret rows across hands. */
	void update_regret_sum_vector(
		double* regret,
		const double* action_value,
		const double* node_value,
		std::size_t hand_count,
		std::size_t action_count) noexcept;
	/** Normalizes an output array using a known total. */
	void normalize(double* out, std::size_t len, double total) noexcept;
	/** Normalizes source values into out with a computed total. */
	void normalize_row(const double* values, double* out, std::size_t len) noexcept;
	/** Builds a regret-matched strategy row. */
	void compute_strategy_row(const double* regrets, double* out, std::size_t len) noexcept;
	/** Builds a small regret-matched row using the bounded menu kernel. */
	void compute_strategy_row_small(const double* regrets, double* out, std::size_t len) noexcept;
	/** Dispatches single-precision regret discounting. */
	void discount_regrets_f32(float* regrets, std::size_t len, double pos_scale, double neg_scale) noexcept;
	/** Dispatches single-precision strategy-sum discounting. */
	void discount_strategy_sum_f32(float* strategy, std::size_t len, double strat_scale) noexcept;

} // namespace texas::util
