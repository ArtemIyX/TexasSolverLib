#pragma once

/** @file
 * @brief Splits and calibrates multiway future-bucket profiles.
 */

#include "solver/multiway/abstraction/multiway_future_bucket.hpp"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace texas::solver::multiway
{

	/** @brief Target value and hand sample used to evaluate bucket quality. */
	struct MultiwayFutureBucketCalibrationSample
	{
		MultiwayBucketBoardRequest board{};
		std::array<std::uint8_t, 2> hole{};
		double target_value = 0.0;
		bool held_out = false;
	};

	/** @brief Maximum loss, variance, miss rate and artifact size for calibration. */
	struct MultiwayFutureBucketCalibrationLimits
	{
		double maximum_held_out_policy_loss = 1.0;
		double maximum_within_bucket_variance = 1.0;
		double maximum_missing_bucket_rate = 0.0;
		std::uint64_t maximum_artifact_bytes = 0U;
		/** @brief Validates calibration thresholds. */
		void validate() const;
	};

	/** @brief Measured bucket-quality losses and acceptance result. */
	struct MultiwayFutureBucketCalibrationResult
	{
		std::uint64_t profile_identity = 0U;
		std::size_t samples = 0U;
		std::size_t held_out_samples = 0U;
		std::size_t missing_buckets = 0U;
		double held_out_policy_loss = 0.0;
		double within_bucket_variance = 0.0;
		std::uint64_t artifact_bytes = 0U;
		bool within_limits = false;
	};

	/** @brief Chosen profile and its calibration result. */
	struct MultiwayFutureBucketCalibrationSelection
	{
		MultiwayFutureBucketProfile profile{};
		MultiwayFutureBucketCalibrationResult result{};
		bool selected = false;
	};

	/** @brief Deterministically marks a fraction of samples as held out. */
	[[nodiscard]] std::vector<MultiwayFutureBucketCalibrationSample>
	split_multiway_future_bucket_samples(
		const std::vector<MultiwayFutureBucketCalibrationSample>& samples,
		std::uint64_t held_out_seed,
		double held_out_fraction);

	/** @brief Measures one profile on samples and checks the configured limits. */
	[[nodiscard]] MultiwayFutureBucketCalibrationResult calibrate_multiway_future_bucket_profile(
		const MultiwayFutureBucketProfile& profile,
		const MultiwayModelIdentity& identity,
		const std::vector<MultiwayFutureBucketCalibrationSample>& samples,
		const MultiwayFutureBucketCalibrationLimits& limits = {});

	/** @brief Chooses the smallest profile that meets calibration limits. */
	[[nodiscard]] MultiwayFutureBucketCalibrationSelection select_smallest_multiway_future_bucket_profile(
		const std::vector<MultiwayFutureBucketProfile>& profiles,
		const MultiwayModelIdentity& identity,
		const std::vector<MultiwayFutureBucketCalibrationSample>& samples,
		const MultiwayFutureBucketCalibrationLimits& limits = {});

} // namespace texas::solver::multiway
