#pragma once

/** @file
 * @brief Scalar and SIMD kernels used by sampled HUNL strategy updates.
 */

#include "util/simd.hpp"

#include <cstddef>
#include <cstdint>

namespace texas::solver::hunl
{

	/** @brief Runtime kernel backend selected for sampled operations. */
	enum class HUNLSampledSimdBackend : std::uint8_t
	{
		Scalar = 0,	 ///< Portable scalar implementation.
		Avx2 = 1, ///< AVX2 implementation when supported.
	};

	/** @brief Enables or disables available SIMD kernels. */
	void set_hunl_sampled_simd_enabled(bool enabled) noexcept;
	/** @brief Reports whether SIMD kernels are enabled. */
	[[nodiscard]] bool hunl_sampled_simd_enabled() noexcept;
	/** @brief Returns the selected SIMD backend. */
	[[nodiscard]] HUNLSampledSimdBackend hunl_sampled_simd_backend() noexcept;
	/** @brief Returns the stable name for a SIMD backend. */
	[[nodiscard]] const char* hunl_sampled_simd_backend_name(HUNLSampledSimdBackend backend) noexcept;

	/** @brief Computes float action-major regret-matching rows using the scalar kernel. */
	void regret_matching_action_major_f32_scalar(
		const float* regret,
		std::uint32_t action_count,
		std::uint32_t bucket_count,
		float* strategy);

	/** @brief Computes float action-major regret-matching rows using the selected kernel. */
	void regret_matching_action_major_f32(
		const float* regret,
		std::uint32_t action_count,
		std::uint32_t bucket_count,
		float* strategy);

	/** @brief Computes double action-major regret-matching rows using the scalar kernel. */
	void regret_matching_action_major_f64_scalar(
		const double* regret,
		std::uint32_t action_count,
		std::uint32_t bucket_count,
		double* strategy);

	/** @brief Computes double action-major regret-matching rows using the selected kernel. */
	void regret_matching_action_major_f64(
		const double* regret,
		std::uint32_t action_count,
		std::uint32_t bucket_count,
		double* strategy);

	/** @brief Adds float strategy weighted by bucket reach using the scalar kernel. */
	void accumulate_average_strategy_action_major_f32_scalar(
		const float* strategy,
		const float* reach_or_weight,
		std::uint32_t action_count,
		std::uint32_t bucket_count,
		float scale,
		float* strategy_sum);

	/** @brief Adds float strategy weighted by bucket reach using the selected kernel. */
	void accumulate_average_strategy_action_major_f32(
		const float* strategy,
		const float* reach_or_weight,
		std::uint32_t action_count,
		std::uint32_t bucket_count,
		float scale,
		float* strategy_sum);

	/** @brief Adds double strategy weighted by bucket reach using the scalar kernel. */
	void accumulate_average_strategy_action_major_f64_scalar(
		const double* strategy,
		const double* reach_or_weight,
		std::uint32_t action_count,
		std::uint32_t bucket_count,
		double scale,
		double* strategy_sum);

	/** @brief Adds double strategy weighted by bucket reach using the selected kernel. */
	void accumulate_average_strategy_action_major_f64(
		const double* strategy,
		const double* reach_or_weight,
		std::uint32_t action_count,
		std::uint32_t bucket_count,
		double scale,
		double* strategy_sum);

	/** @brief Adds float counterfactual action-value deltas to regret using the scalar kernel. */
	void add_regret_delta_action_major_f32_scalar(
		const float* action_values,
		const float* node_values,
		const float* cf_reach,
		std::uint32_t action_count,
		std::uint32_t bucket_count,
		float* regret);

	/** @brief Adds float counterfactual action-value deltas to regret using the selected kernel. */
	void add_regret_delta_action_major_f32(
		const float* action_values,
		const float* node_values,
		const float* cf_reach,
		std::uint32_t action_count,
		std::uint32_t bucket_count,
		float* regret);

	/** @brief Adds double counterfactual action-value deltas to regret using the scalar kernel. */
	void add_regret_delta_action_major_f64_scalar(
		const double* action_values,
		const double* node_values,
		const double* cf_reach,
		std::uint32_t action_count,
		std::uint32_t bucket_count,
		double* regret);

	/** @brief Adds double counterfactual action-value deltas to regret using the selected kernel. */
	void add_regret_delta_action_major_f64(
		const double* action_values,
		const double* node_values,
		const double* cf_reach,
		std::uint32_t action_count,
		std::uint32_t bucket_count,
		double* regret);

	/** @brief Computes y += alpha * x for n float values. */
	void saxpy_f32(std::uint32_t n, float alpha, const float* x, float* y);
	/** @brief Computes the dot product of two float arrays with double accumulation. */
	double dot_f32_f64_accum(std::uint32_t n, const float* x, const float* y);
	/** @brief Computes a weighted sum of float values with double accumulation. */
	double weighted_sum_f32_f64_accum(std::uint32_t n, const float* values, const float* weights);

} // namespace texas::solver::hunl
