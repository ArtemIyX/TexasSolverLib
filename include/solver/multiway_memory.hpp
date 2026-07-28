#pragma once

#include "solver/multiway_solver.hpp"

#include <cstdint>

namespace core {

struct MultiwayMemoryBudget {
    std::uint64_t warning_bytes = 48ULL * 1024ULL * 1024ULL * 1024ULL;
    std::uint64_t reject_bytes = 60ULL * 1024ULL * 1024ULL * 1024ULL;

    void validate() const;
};

struct MultiwayMemoryEstimate {
    std::uint64_t public_state_bytes = 0;
    std::uint64_t sparse_row_bytes = 0;
    std::uint64_t sparse_value_bytes = 0;
    std::uint64_t worker_delta_bytes = 0;
    std::uint64_t total_bytes = 0;
};

enum class MultiwayMemoryStatus : std::uint8_t {
    Ok,
    Warning,
    Rejected,
};

struct MultiwayMemoryPreflight {
    MultiwayMemoryStatus status = MultiwayMemoryStatus::Ok;
    MultiwayMemoryEstimate estimate{};
    const char* message = "ok";
};

[[nodiscard]] MultiwayMemoryPreflight preflight_multiway_memory(
    const MultiwaySolverLimits& limits,
    MultiwayMemoryBudget budget = {});

}  // namespace core
