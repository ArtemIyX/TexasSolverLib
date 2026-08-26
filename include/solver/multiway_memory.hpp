#pragma once

#include "solver/multiway_solver.hpp"

#include <cstdint>

namespace texas::solver::multiway {

struct MultiwayMemoryBudget {
    std::uint64_t warning_bytes = 48ULL * 1024ULL * 1024ULL * 1024ULL;
    std::uint64_t operating_bytes = 56ULL * 1024ULL * 1024ULL * 1024ULL;
    std::uint64_t reject_bytes = 60ULL * 1024ULL * 1024ULL * 1024ULL;

    constexpr MultiwayMemoryBudget() noexcept = default;
    constexpr MultiwayMemoryBudget(
        std::uint64_t warning,
        std::uint64_t reject,
        std::uint64_t operating = 0U) noexcept
        : warning_bytes(warning),
          operating_bytes(operating == 0U
              ? warning + (reject - warning) / 2U
              : operating),
          reject_bytes(reject) {}

    void validate() const;
};

struct MultiwayMemoryInputs {
    std::uint64_t blueprint_index_bytes = 0U;
    std::uint64_t range_row_count = 0U;
    std::uint64_t range_entry_count = 0U;
    std::uint64_t future_bucket_cache_bytes = 0U;
    std::uint64_t off_tree_menu_entries = 0U;
    std::uint64_t continuation_scratch_bytes_per_worker = 0U;
    std::uint64_t continuation_cache_bytes = 0U;
    std::uint64_t export_action_capacity = 0U;
};

struct MultiwayMemoryEstimate {
    std::uint64_t public_state_bytes = 0;
    std::uint64_t sparse_row_bytes = 0;
    std::uint64_t sparse_value_bytes = 0;
    std::uint64_t blueprint_index_bytes = 0U;
    std::uint64_t range_row_bytes = 0U;
    std::uint64_t future_bucket_cache_bytes = 0U;
    std::uint64_t off_tree_menu_bytes = 0U;
    std::uint64_t continuation_scratch_bytes = 0U;
    std::uint64_t worker_delta_bytes = 0;
    std::uint64_t merge_scratch_bytes = 0U;
    std::uint64_t export_bytes = 0U;
    std::uint64_t continuation_cache_bytes = 0U;
    std::uint64_t mandatory_bytes = 0U;
    std::uint64_t admitted_bytes = 0U;
    std::uint64_t total_bytes = 0;
};

enum class MultiwayMemoryStatus : std::uint8_t {
    Ok,
    Warning,
    Rejected,
};

enum class MultiwayMemoryAdmissionStage : std::uint8_t {
    None = 0U,
    Root = 1U,
    SparseRows = 2U,
    WorkerDeltas = 3U,
    OptionalContinuationCache = 4U,
};

struct MultiwayMemoryPreflight {
    MultiwayMemoryStatus status = MultiwayMemoryStatus::Ok;
    MultiwayMemoryEstimate estimate{};
    MultiwayMemoryAdmissionStage highest_admitted_stage = MultiwayMemoryAdmissionStage::None;
    bool optional_continuation_cache_admitted = false;
    bool degraded = false;
    const char* message = "ok";
};

[[nodiscard]] MultiwayMemoryPreflight preflight_multiway_memory(
    const MultiwaySolverLimits& limits,
    MultiwayMemoryBudget budget = {});

[[nodiscard]] MultiwayMemoryPreflight preflight_multiway_memory(
    const MultiwaySolverLimits& limits,
    MultiwayMemoryBudget budget,
    const MultiwayMemoryInputs& inputs);

}  // namespace texas::solver::multiway
