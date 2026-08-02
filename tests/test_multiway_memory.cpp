#include "solver/multiway_memory.hpp"
#include "test_harness.hpp"

#include <stdexcept>

namespace {

core::MultiwaySolverLimits limits() {
    core::MultiwaySolverLimits result;
    result.worker_count = 2;
    result.trajectories_per_batch = 32;
    result.max_public_states = 64;
    result.max_sparse_rows = 128;
    result.max_sparse_values = 1024;
    result.max_worker_delta_entries = 256;
    return result;
}

}  // namespace

TEST_CASE(multiway_memory_preflight_reports_warning_and_rejection) {
    const auto input = limits();
    const auto ok = core::preflight_multiway_memory(input, {1'000'000, 2'000'000});
    EXPECT_EQ(ok.status, core::MultiwayMemoryStatus::Ok);
    EXPECT_TRUE(ok.estimate.total_bytes > 0U);

    const auto warning = core::preflight_multiway_memory(input, {1, 2'000'000});
    EXPECT_EQ(warning.status, core::MultiwayMemoryStatus::Warning);

    const auto rejected = core::preflight_multiway_memory(input, {1, 2});
    EXPECT_EQ(rejected.status, core::MultiwayMemoryStatus::Rejected);
}

TEST_CASE(multiway_memory_budget_rejects_invalid_limits) {
    const core::MultiwayMemoryBudget invalid{2, 1};
    EXPECT_THROW(invalid.validate(), std::invalid_argument);
}
