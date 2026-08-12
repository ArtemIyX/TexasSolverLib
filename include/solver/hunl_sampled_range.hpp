#pragma once

#include "core/namespaces.hpp"

#include "games/hunl_solver.hpp"
#include "solver/hunl_sampled_config.hpp"
#include "solver/hunl_sampled_export.hpp"
#include "solver/hunl_sampled_profile.hpp"
#include "solver/hunl_sampled_storage.hpp"

#include <chrono>
#include <cstdint>
#include <memory>
#include <optional>

namespace texas::solver::hunl {

// Structured ranges use a private-state traversal rather than the fixed-deal
// public-node cache. The coordinator owns this object and advances it through
// deterministic bounded trajectory subbatches.
struct HUNLSampledRangeRunResult {
    HUNLSampledRootStrategy root_strategy;
    std::optional<HUNLSampledRootStrategy> range_wide_root_strategy = std::nullopt;
    std::uint64_t batches_completed = 0;
    bool timed_out = false;
};

struct HUNLSampledRangeMemoryEstimate {
    std::uint64_t joint_deal_bytes = 0;
    std::uint64_t infoset_lookup_bytes = 0;
    std::uint64_t retained_bytes = 0;
    std::uint64_t batch_scratch_bytes = 0;
    std::uint64_t export_bytes = 0;

    [[nodiscard]] std::uint64_t peak_bytes() const noexcept;
};

[[nodiscard]] HUNLSampledRangeMemoryEstimate estimate_hunl_sampled_range_memory(
    const HUNLStructuredRootRequest& root,
    const HUNLSampledSolverConfig& config) noexcept;

class HUNLSampledRangeSession {
public:
    HUNLSampledRangeSession(
        HUNLStructuredRootRequest root,
        HUNLSampledSolverConfig config,
        HUNLSampledStorage& storage,
        HUNLSampledProfile& profile,
        std::uint64_t first_batch = 0U,
        const HUNLLeafEvaluator* leaf_evaluator = nullptr,
        std::uint64_t memory_limit_bytes = 0U);
    ~HUNLSampledRangeSession();
    HUNLSampledRangeSession(HUNLSampledRangeSession&&) noexcept;
    HUNLSampledRangeSession& operator=(HUNLSampledRangeSession&&) noexcept;
    HUNLSampledRangeSession(const HUNLSampledRangeSession&) = delete;
    HUNLSampledRangeSession& operator=(const HUNLSampledRangeSession&) = delete;

    [[nodiscard]] HUNLSampledRangeRunResult resume_batches(
        std::uint32_t batch_count,
        std::optional<std::chrono::steady_clock::time_point> deadline = std::nullopt);
    [[nodiscard]] std::uint64_t next_batch() const noexcept;
    [[nodiscard]] HUNLSampledRootStrategy export_root_strategy();
    [[nodiscard]] HUNLSampledRangeMemoryEstimate memory_estimate() const noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

HUNLSampledRangeRunResult run_hunl_sampled_structured_range_batches(
    const HUNLStructuredRootRequest& root,
    const HUNLSampledSolverConfig& config,
    std::uint64_t first_batch,
    std::uint32_t batch_count,
    std::optional<std::chrono::steady_clock::time_point> deadline,
    HUNLSampledStorage& storage,
    HUNLSampledProfile& profile,
    const HUNLLeafEvaluator* leaf_evaluator = nullptr,
    std::uint64_t memory_limit_bytes = 0U);

}  // namespace texas::solver::hunl
