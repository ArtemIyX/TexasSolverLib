#include "solver/multiway_future_bucket_calibration.hpp"

#include <algorithm>
#include <cmath>
#include <iterator>
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
    struct BucketStats { std::size_t board = 0U; std::uint32_t bucket = 0U; std::size_t count = 0U; double sum = 0.0; double sum_squared = 0.0; };
    std::vector<BucketStats> stats;
    std::vector<std::size_t> sample_boards;
    sample_boards.reserve(samples.size());
    for (const auto& sample : samples) {
        const auto board = std::find_if(boards.begin(), boards.end(), [&sample](const auto& candidate) {
            return candidate.street == sample.board.street && candidate.canonical_board == sample.board.canonical_board;
        });
        sample_boards.push_back(static_cast<std::size_t>(board - boards.begin()));
    }
    for (const auto& sample : samples) {
        const auto bucket = artifact.lookup(sample.board.street, sample.board.canonical_board, sample.hole);
        if (bucket == MULTIWAY_INVALID_BUCKET) { ++result.missing_buckets; continue; }
        const auto board_index = sample_boards[&sample - samples.data()];
        auto stat = std::find_if(stats.begin(), stats.end(), [board_index, bucket](const auto& entry) {
            return entry.board == board_index && entry.bucket == bucket;
        });
        if (stat == stats.end()) {
            stats.push_back({board_index, bucket, 0U, 0.0, 0.0});
            stat = std::prev(stats.end());
        }
        if (!sample.held_out) {
            ++stat->count; stat->sum += sample.target_value;
            stat->sum_squared += sample.target_value * sample.target_value;
        }
    }
    double variance_sum = 0.0;
    for (const auto& stat : stats) {
        if (stat.count != 0U) {
            variance_sum += std::max(0.0, stat.sum_squared / stat.count -
                (stat.sum / stat.count) * (stat.sum / stat.count)) * stat.count;
        }
    }
    double heldout_loss = 0.0; std::size_t heldout_count = 0U;
    for (std::size_t index = 0U; index < samples.size(); ++index) {
        if (!samples[index].held_out) continue;
        ++result.held_out_samples;
        const auto bucket = artifact.lookup(samples[index].board.street, samples[index].board.canonical_board, samples[index].hole);
        const auto board_index = sample_boards[index];
        const auto stat = std::find_if(stats.begin(), stats.end(), [board_index, bucket](const auto& entry) {
            return entry.board == board_index && entry.bucket == bucket;
        });
        if (bucket == MULTIWAY_INVALID_BUCKET) continue;
        if (stat == stats.end() || stat->count == 0U) {
            ++result.missing_buckets;
            continue;
        }
        const auto error = samples[index].target_value - stat->sum / stat->count;
        heldout_loss += error * error; ++heldout_count;
    }
    result.held_out_policy_loss = heldout_count == 0U ? 0.0 : heldout_loss / heldout_count;
    result.within_bucket_variance = result.samples == 0U ? 0.0 : variance_sum / result.samples;
    result.within_limits = (limits.maximum_artifact_bytes == 0U || result.artifact_bytes <= limits.maximum_artifact_bytes) &&
        result.held_out_policy_loss <= limits.maximum_held_out_policy_loss &&
        result.within_bucket_variance <= limits.maximum_within_bucket_variance &&
        static_cast<double>(result.missing_buckets) / result.samples <= limits.maximum_missing_bucket_rate;
    return result;
}

MultiwayFutureBucketCalibrationSelection select_smallest_multiway_future_bucket_profile(
    const std::vector<MultiwayFutureBucketProfile>& profiles, const MultiwayModelIdentity& identity,
    const std::vector<MultiwayFutureBucketCalibrationSample>& samples,
    const MultiwayFutureBucketCalibrationLimits& limits) {
    if (profiles.empty()) throw std::invalid_argument("future bucket profile selection requires profiles");
    MultiwayFutureBucketCalibrationSelection selection;
    const auto bucket_total = [](const auto& profile) {
        return static_cast<std::uint64_t>(profile.flop_bucket_count) + profile.turn_bucket_count + profile.river_bucket_count;
    };
    for (const auto& profile : profiles) {
        const auto result = calibrate_multiway_future_bucket_profile(profile, identity, samples, limits);
        if (result.within_limits && (!selection.selected || result.artifact_bytes < selection.result.artifact_bytes ||
            (result.artifact_bytes == selection.result.artifact_bytes && bucket_total(profile) < bucket_total(selection.profile)))) {
            selection.profile = profile;
            selection.result = result;
            selection.selected = true;
        }
    }
    return selection;
}
}  // namespace texas::solver::multiway
