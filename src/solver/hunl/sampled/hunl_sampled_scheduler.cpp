#include "solver/hunl/sampled/hunl_sampled_scheduler.hpp"

namespace texas::solver::hunl {

std::vector<HUNLSampledWorkerBatch> HUNLSampledScheduler::partition_deterministic(
    std::uint64_t trajectory_count,
    std::size_t worker_count) {
    if (trajectory_count == 0U) {
        return {{0U, {0U, 0U}}};
    }
    if (worker_count == 0) {
        worker_count = 1;
    }
    if (trajectory_count < static_cast<std::uint64_t>(worker_count)) {
        worker_count = static_cast<std::size_t>(trajectory_count);
    }

    std::vector<HUNLSampledWorkerBatch> batches;
    batches.reserve(worker_count);

    const auto base = trajectory_count / static_cast<std::uint64_t>(worker_count);
    const auto remainder = trajectory_count % static_cast<std::uint64_t>(worker_count);

    std::uint64_t begin = 0;
    for (std::size_t worker_index = 0; worker_index < worker_count; ++worker_index) {
        const auto extra = worker_index < remainder ? 1ULL : 0ULL;
        const auto end = begin + base + extra;
        batches.push_back({worker_index, {begin, end}});
        begin = end;
    }

    return batches;
}

}  // namespace texas::solver::hunl
