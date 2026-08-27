#include "solver/multiway/engine/multiway_scheduler.hpp"
#include "core/fingerprint.hpp"

#include <algorithm>

namespace texas::solver::multiway {
namespace {

void hash_u64(std::uint64_t value, std::uint64_t& hash) noexcept {
    texas::core::fingerprint::append_u64(hash, value);
}

}  // namespace

std::uint64_t multiway_deterministic_trajectory_seed(
    std::uint64_t base_seed,
    std::uint64_t trajectory_id) noexcept {
    auto value = base_seed + 0x9e3779b97f4a7c15ULL + trajectory_id;
    value = (value ^ (value >> 30U)) * 0xbf58476d1ce4e5b9ULL;
    value = (value ^ (value >> 27U)) * 0x94d049bb133111ebULL;
    return value ^ (value >> 31U);
}

std::uint64_t multiway_deterministic_schedule_fingerprint(
    std::uint32_t worker_count,
    std::uint64_t base_seed,
    std::uint64_t first_trajectory_id,
    std::uint64_t trajectory_count) noexcept {
    std::uint64_t hash = 1469598103934665603ULL;
    hash_u64(worker_count, hash);
    hash_u64(base_seed, hash);
    hash_u64(first_trajectory_id, hash);
    hash_u64(trajectory_count, hash);
    hash_u64(MULTIWAY_PARTITION_VERSION, hash);
    hash_u64(MULTIWAY_TRAJECTORY_SEED_VERSION, hash);
    hash_u64(MULTIWAY_ACTION_SAMPLING_VERSION, hash);
    hash_u64(MULTIWAY_PUBLIC_CHANCE_ORDER_VERSION, hash);
    hash_u64(MULTIWAY_MERGE_ORDER_VERSION, hash);
    return hash == 0U ? 1U : hash;
}

std::size_t MultiwayScheduler::partition_deterministic_into(
    std::uint64_t trajectory_count,
    std::size_t requested_workers,
    MultiwayWorkerBatch* output,
    std::size_t output_capacity) noexcept {
    if (output == nullptr || output_capacity == 0U) return 0U;
    if (trajectory_count == 0U) {
        output[0] = {0U, {0U, 0U}};
        return 1U;
    }
    const auto worker_count_u64 = std::min<std::uint64_t>(
        trajectory_count,
        std::min<std::uint64_t>(
            std::max<std::size_t>(1U, requested_workers),
            output_capacity));
    const auto base = trajectory_count / worker_count_u64;
    const auto remainder = trajectory_count % worker_count_u64;
    std::uint64_t begin = 0U;
    for (std::uint64_t worker = 0U; worker < worker_count_u64; ++worker) {
        const auto count = base + (worker < remainder ? 1U : 0U);
        output[static_cast<std::size_t>(worker)] = {
            static_cast<std::size_t>(worker), {begin, begin + count}};
        begin += count;
    }
    return static_cast<std::size_t>(worker_count_u64);
}

std::vector<MultiwayWorkerBatch> MultiwayScheduler::partition_deterministic(
    std::uint64_t trajectory_count,
    std::size_t requested_workers) {
    const auto worker_count = trajectory_count == 0U ? 1U : static_cast<std::size_t>(
        std::min<std::uint64_t>(trajectory_count, std::max<std::size_t>(1U, requested_workers)));
    std::vector<MultiwayWorkerBatch> result(worker_count);
    (void)partition_deterministic_into(
        trajectory_count, requested_workers, result.data(), result.size());
    return result;
}

}  // namespace texas::solver::multiway
