#include "solver/multiway_scheduler.hpp"

#include <algorithm>

namespace core {

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

}  // namespace core
