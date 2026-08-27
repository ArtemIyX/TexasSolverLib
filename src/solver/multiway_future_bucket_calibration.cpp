#include "solver/multiway_future_bucket_calibration.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace texas::solver::multiway {
namespace {
std::uint64_t mix(std::uint64_t value) noexcept {
    value ^= value >> 30U; value *= 0xBF58476D1CE4E5B9ULL;
    value ^= value >> 27U; value *= 0x94D049BB133111EBULL;
    return value ^ (value >> 31U);
}
}

void MultiwayFutureBucketCalibrationLimits::validate() const {
    if (!std::isfinite(maximum_held_out_policy_loss) || maximum_held_out_policy_loss < 0.0 ||
        !std::isfinite(maximum_within_bucket_variance) || maximum_within_bucket_variance < 0.0 ||
        maximum_missing_bucket_rate < 0.0 || maximum_missing_bucket_rate > 1.0) {
        throw std::invalid_argument("future bucket calibration limits are invalid");
    }
}

std::vector<MultiwayFutureBucketCalibrationSample> split_multiway_future_bucket_samples(
    const std::vector<MultiwayFutureBucketCalibrationSample>& samples,
    std::uint64_t held_out_seed, double held_out_fraction) {
    if (samples.empty() || held_out_seed == 0U || held_out_fraction < 0.0 || held_out_fraction > 1.0) {
        throw std::invalid_argument("future bucket sample split is invalid");
    }
    auto result = samples;
    const auto threshold = static_cast<std::uint64_t>(held_out_fraction * 1000000.0);
    for (std::size_t index = 0U; index < result.size(); ++index) {
        result[index].held_out = mix(held_out_seed + index) % 1000000U < threshold;
    }
    return result;
}

MultiwayFutureBucketCalibrationResult calibrate_multiway_future_bucket_profile(
    const MultiwayFutureBucketProfile& profile, const MultiwayModelIdentity& identity,
    const std::vector<MultiwayFutureBucketCalibrationSample>& samples,
    const MultiwayFutureBucketCalibrationLimits& limits) {
    profile.validate(); identity.validate(); limits.validate();
    if (samples.empty()) throw std::invalid_argument("future bucket calibration requires samples");
    std::vector<MultiwayBucketBoardRequest> boards;
    for (const auto& sample : samples) {
        const auto duplicate = std::find_if(boards.begin(), boards.end(), [&sample](const auto& board) {
            return board.street == sample.board.street && board.canonical_board == sample.board.canonical_board;
        });
        if (duplicate == boards.end()) boards.push_back(sample.board);
    }
    const auto artifact = build_multiway_future_bucket_artifact(identity, boards, profile);
    const auto bytes = serialize_multiway_future_bucket_artifact(artifact);
    MultiwayFutureBucketCalibrationResult result;
    result.samples = samples.size(); result.artifact_bytes = bytes.size();
    result.profile_identity = profile.feature_version ^ (profile.clustering_version << 21U) ^ profile.seed;
    double squared_error = 0.0; std::size_t error_count = 0U;
    for (const auto& sample : samples) {
        const auto bucket = artifact.lookup(sample.board.street, sample.board.canonical_board, sample.hole);
        if (bucket == MULTIWAY_INVALID_BUCKET) { ++result.missing_buckets; continue; }
        if (sample.held_out) { ++result.held_out_samples; }
        squared_error += sample.target_value * sample.target_value;
        ++error_count;
    }
    result.held_out_policy_loss = result.held_out_samples == 0U ? 0.0 : squared_error / error_count;
    result.within_bucket_variance = result.samples == 0U ? 0.0 : squared_error / result.samples;
    result.within_limits = (limits.maximum_artifact_bytes == 0U || result.artifact_bytes <= limits.maximum_artifact_bytes) &&
        result.held_out_policy_loss <= limits.maximum_held_out_policy_loss &&
        result.within_bucket_variance <= limits.maximum_within_bucket_variance &&
        static_cast<double>(result.missing_buckets) / result.samples <= limits.maximum_missing_bucket_rate;
    return result;
}
}  // namespace texas::solver::multiway
