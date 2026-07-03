#pragma once

#include <cstddef>
#include <cstdint>
#include <type_traits>
#include <vector>

namespace core {

struct HUNLSampledTrajectoryRange {
    std::uint64_t begin = 0;
    std::uint64_t end = 0;

    [[nodiscard]] std::uint64_t size() const noexcept {
        return end - begin;
    }
};

struct HUNLSampledWorkerBatch {
    std::size_t worker_index = 0;
    HUNLSampledTrajectoryRange trajectories;
};

static_assert(
    std::is_trivially_copyable_v<HUNLSampledTrajectoryRange>,
    "HUNLSampledTrajectoryRange should stay trivially copyable");
static_assert(
    std::is_trivially_copyable_v<HUNLSampledWorkerBatch>,
    "HUNLSampledWorkerBatch should stay trivially copyable");

class HUNLSampledScheduler {
public:
    [[nodiscard]] static std::vector<HUNLSampledWorkerBatch> partition_deterministic(
        std::uint64_t trajectory_count,
        std::size_t worker_count);
};

}  // namespace core
