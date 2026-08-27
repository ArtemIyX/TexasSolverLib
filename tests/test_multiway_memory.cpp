#include "solver/multiway/engine/multiway_memory.hpp"
#include "test_harness.hpp"

#include <stdexcept>
#include <limits>

namespace {

texas::MultiwaySolverLimits limits() {
    texas::MultiwaySolverLimits result;
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
    const auto ok = texas::preflight_multiway_memory(input, {1'000'000, 2'000'000});
    EXPECT_EQ(ok.status, texas::MultiwayMemoryStatus::Ok);
    EXPECT_TRUE(ok.estimate.total_bytes > 0U);

    const auto warning = texas::preflight_multiway_memory(input, {1, 2'000'000});
    EXPECT_EQ(warning.status, texas::MultiwayMemoryStatus::Warning);

    const auto rejected = texas::preflight_multiway_memory(input, {1, 2});
    EXPECT_EQ(rejected.status, texas::MultiwayMemoryStatus::Rejected);
}

TEST_CASE(multiway_memory_budget_rejects_invalid_limits) {
    const texas::MultiwayMemoryBudget invalid{2, 1};
    EXPECT_THROW(invalid.validate(), std::invalid_argument);
}

TEST_CASE(multiway_memory_preflight_accounts_for_all_runtime_search_components) {
    texas::MultiwayMemoryInputs inputs;
    inputs.blueprint_index_bytes = 11U;
    inputs.range_row_count = 6U;
    inputs.range_entry_count = 42U;
    inputs.future_bucket_cache_bytes = 13U;
    inputs.off_tree_menu_entries = 17U;
    inputs.continuation_scratch_bytes_per_worker = 19U;
    inputs.continuation_cache_bytes = 23U;
    inputs.export_action_capacity = 8U;

    const auto result = texas::preflight_multiway_memory(
        limits(), texas::MultiwayMemoryBudget{1U, 10'000'000U, 9'000'000U}, inputs);
    EXPECT_TRUE(result.estimate.blueprint_index_bytes >= 11U);
    EXPECT_TRUE(result.estimate.range_row_bytes > 0U);
    EXPECT_EQ(result.estimate.future_bucket_cache_bytes, 13U);
    EXPECT_TRUE(result.estimate.off_tree_menu_bytes > 0U);
    EXPECT_EQ(result.estimate.continuation_scratch_bytes, 38U);
    EXPECT_TRUE(result.estimate.merge_scratch_bytes > 0U);
    EXPECT_TRUE(result.estimate.export_bytes > 0U);
    EXPECT_EQ(result.estimate.continuation_cache_bytes, 23U);
    EXPECT_EQ(result.estimate.admitted_bytes, result.estimate.total_bytes);
    EXPECT_EQ(
        result.highest_admitted_stage,
        texas::MultiwayMemoryAdmissionStage::OptionalContinuationCache);
}

TEST_CASE(multiway_memory_preflight_drops_optional_cache_before_operating_cap) {
    texas::MultiwayMemoryInputs inputs;
    inputs.continuation_cache_bytes = 4'096U;
    const auto baseline = texas::preflight_multiway_memory(
        limits(), texas::MultiwayMemoryBudget{1U, 10'000'000U, 9'000'000U}, inputs);
    const auto constrained = texas::preflight_multiway_memory(
        limits(),
        texas::MultiwayMemoryBudget{
            1U,
            baseline.estimate.total_bytes + 1U,
            baseline.estimate.mandatory_bytes},
        inputs);

    EXPECT_EQ(constrained.status, texas::MultiwayMemoryStatus::Warning);
    EXPECT_TRUE(constrained.degraded);
    EXPECT_TRUE(!constrained.optional_continuation_cache_admitted);
    EXPECT_EQ(constrained.estimate.admitted_bytes, constrained.estimate.mandatory_bytes);
    EXPECT_EQ(
        constrained.highest_admitted_stage,
        texas::MultiwayMemoryAdmissionStage::WorkerDeltas);
}

TEST_CASE(multiway_memory_preflight_stops_before_rejected_row_allocation) {
    const auto roomy = texas::preflight_multiway_memory(
        limits(), texas::MultiwayMemoryBudget{1U, 10'000'000U, 9'000'000U});
    const auto root_bytes = roomy.estimate.mandatory_bytes -
        roomy.estimate.sparse_row_bytes - roomy.estimate.sparse_value_bytes -
        roomy.estimate.worker_delta_bytes - roomy.estimate.merge_scratch_bytes -
        roomy.estimate.continuation_scratch_bytes;
    const auto rejected = texas::preflight_multiway_memory(
        limits(),
        texas::MultiwayMemoryBudget{1U, roomy.estimate.total_bytes + 1U, root_bytes});

    EXPECT_EQ(rejected.status, texas::MultiwayMemoryStatus::Rejected);
    EXPECT_EQ(rejected.highest_admitted_stage, texas::MultiwayMemoryAdmissionStage::Root);
    EXPECT_EQ(rejected.estimate.admitted_bytes, root_bytes);
}

TEST_CASE(multiway_memory_preflight_rejects_synthetic_maximums_without_estimate_overflow) {
    auto maximum = limits();
    maximum.max_public_states = std::numeric_limits<std::size_t>::max();
    maximum.max_sparse_rows = std::numeric_limits<std::size_t>::max();
    maximum.max_sparse_values = std::numeric_limits<std::size_t>::max();
    const auto rejected = texas::preflight_multiway_memory(maximum);

    EXPECT_EQ(rejected.status, texas::MultiwayMemoryStatus::Rejected);
    EXPECT_EQ(rejected.highest_admitted_stage, texas::MultiwayMemoryAdmissionStage::None);
    EXPECT_EQ(rejected.estimate.total_bytes, std::numeric_limits<std::uint64_t>::max());
}
