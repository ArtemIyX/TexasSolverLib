#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace core {

struct MultiwayTrajectoryRange {
    std::uint64_t begin = 0;
    std::uint64_t end = 0;

    [[nodiscard]] std::uint64_t size() const noexcept { return end - begin; }
};

struct MultiwayWorkerBatch {
    std::size_t worker_index = 0;
    MultiwayTrajectoryRange trajectories{};
};

class MultiwayScheduler {
public:
    // Writes a deterministic contiguous partition into caller-owned storage.
    // Returns zero only when output is null or capacity is zero.
    [[nodiscard]] static std::size_t partition_deterministic_into(
        std::uint64_t trajectory_count,
        std::size_t requested_workers,
        MultiwayWorkerBatch* output,
        std::size_t output_capacity) noexcept;

    [[nodiscard]] static std::vector<MultiwayWorkerBatch> partition_deterministic(
        std::uint64_t trajectory_count,
        std::size_t requested_workers);
};

}  // namespace core
