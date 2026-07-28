#include "solver/multiway_scheduler.hpp"

#include <algorithm>

namespace core {

std::vector<MultiwayWorkerBatch> MultiwayScheduler::partition_deterministic(
    std::uint64_t trajectory_count,
    std::size_t requested_workers) {
    if (trajectory_count == 0U) return {{0U, {0U, 0U}}};
    const auto worker_count = std::min<std::uint64_t>(
        trajectory_count,
        std::max<std::size_t>(1U, requested_workers));
    std::vector<MultiwayWorkerBatch> result;
    result.reserve(static_cast<std::size_t>(worker_count));
    const auto base = trajectory_count / worker_count;
    const auto remainder = trajectory_count % worker_count;
    std::uint64_t begin = 0;
    for (std::uint64_t worker = 0; worker < worker_count; ++worker) {
        const auto count = base + (worker < remainder ? 1U : 0U);
        result.push_back({static_cast<std::size_t>(worker), {begin, begin + count}});
        begin += count;
    }
    return result;
}

}  // namespace core
