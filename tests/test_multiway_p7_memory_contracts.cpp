#include "solver/multiway_memory.hpp"
#include "test_harness.hpp"

#include <cstdint>
#include <limits>
#include <stdexcept>
#include <vector>

namespace {

core::MultiwaySolverLimits bounded_limits() {
    core::MultiwaySolverLimits limits;
    limits.worker_count = 2U;
    limits.trajectories_per_batch = 8U;
    limits.max_public_states = 4U;
    limits.max_sparse_rows = 3U;
    limits.max_sparse_values = 12U;
    limits.max_worker_delta_entries = 5U;
    return limits;
}

core::MultiwayMemoryBudget roomy_budget() {
    return {800'000'000U, 1'000'000'000U, 900'000'000U};
}

core::MultiwayMemoryPreflight baseline(core::MultiwayMemoryInputs inputs = {}) {
    return core::preflight_multiway_memory(bounded_limits(), roomy_budget(), inputs);
}

std::uint64_t root_bytes(const core::MultiwayMemoryEstimate& estimate) {
    return estimate.public_state_bytes + estimate.blueprint_index_bytes +
        estimate.range_row_bytes + estimate.future_bucket_cache_bytes +
        estimate.off_tree_menu_bytes + estimate.export_bytes;
}

std::uint64_t row_bytes(const core::MultiwayMemoryEstimate& estimate) {
    return estimate.sparse_row_bytes + estimate.sparse_value_bytes;
}

}  // namespace

TEST_CASE(multiway_p7_memory_default_warning_is_48_gib) {
    const core::MultiwayMemoryBudget budget;
    EXPECT_EQ(budget.warning_bytes, 48ULL * 1024ULL * 1024ULL * 1024ULL);
}

TEST_CASE(multiway_p7_memory_default_operating_cap_is_56_gib) {
    const core::MultiwayMemoryBudget budget;
    EXPECT_EQ(budget.operating_bytes, 56ULL * 1024ULL * 1024ULL * 1024ULL);
}

TEST_CASE(multiway_p7_memory_default_reject_cap_is_60_gib) {
    const core::MultiwayMemoryBudget budget;
    EXPECT_EQ(budget.reject_bytes, 60ULL * 1024ULL * 1024ULL * 1024ULL);
}

TEST_CASE(multiway_p7_memory_two_argument_budget_uses_midpoint_operating_cap) {
    const core::MultiwayMemoryBudget budget{100U, 200U};
    EXPECT_EQ(budget.operating_bytes, 150U);
}

TEST_CASE(multiway_p7_memory_explicit_operating_cap_is_preserved) {
    const core::MultiwayMemoryBudget budget{100U, 300U, 175U};
    EXPECT_EQ(budget.operating_bytes, 175U);
}

TEST_CASE(multiway_p7_memory_budget_accepts_equal_warning_and_operating_caps) {
    const core::MultiwayMemoryBudget budget{100U, 101U, 100U};
    budget.validate();
    EXPECT_TRUE(true);
}

TEST_CASE(multiway_p7_memory_budget_rejects_zero_warning) {
    core::MultiwayMemoryBudget budget{1U, 3U, 2U};
    budget.warning_bytes = 0U;
    EXPECT_THROW(budget.validate(), std::invalid_argument);
}

TEST_CASE(multiway_p7_memory_budget_rejects_zero_operating) {
    core::MultiwayMemoryBudget budget{1U, 3U, 2U};
    budget.operating_bytes = 0U;
    EXPECT_THROW(budget.validate(), std::invalid_argument);
}

TEST_CASE(multiway_p7_memory_budget_rejects_zero_reject) {
    core::MultiwayMemoryBudget budget{1U, 3U, 2U};
    budget.reject_bytes = 0U;
    EXPECT_THROW(budget.validate(), std::invalid_argument);
}

TEST_CASE(multiway_p7_memory_budget_rejects_warning_above_operating) {
    core::MultiwayMemoryBudget budget{1U, 3U, 2U};
    budget.warning_bytes = 3U;
    EXPECT_THROW(budget.validate(), std::invalid_argument);
}

TEST_CASE(multiway_p7_memory_budget_rejects_operating_equal_to_reject) {
    core::MultiwayMemoryBudget budget{1U, 3U, 2U};
    budget.operating_bytes = 3U;
    EXPECT_THROW(budget.validate(), std::invalid_argument);
}

TEST_CASE(multiway_p7_memory_budget_rejects_operating_above_reject) {
    core::MultiwayMemoryBudget budget{1U, 3U, 2U};
    budget.operating_bytes = 4U;
    EXPECT_THROW(budget.validate(), std::invalid_argument);
}

TEST_CASE(multiway_p7_memory_roomy_preflight_admits_mandatory_stages) {
    const auto result = baseline();
    EXPECT_EQ(result.status, core::MultiwayMemoryStatus::Ok);
    EXPECT_EQ(result.highest_admitted_stage, core::MultiwayMemoryAdmissionStage::WorkerDeltas);
}

TEST_CASE(multiway_p7_memory_no_optional_cache_is_not_marked_admitted) {
    const auto result = baseline();
    EXPECT_TRUE(!result.optional_continuation_cache_admitted);
    EXPECT_TRUE(!result.degraded);
}

TEST_CASE(multiway_p7_memory_public_state_estimate_uses_one_kib_per_state) {
    const auto result = baseline();
    EXPECT_EQ(result.estimate.public_state_bytes, 4U * 1024U);
}

TEST_CASE(multiway_p7_memory_sparse_row_metadata_is_accounted) {
    const auto result = baseline();
    EXPECT_EQ(
        result.estimate.sparse_row_bytes,
        3U * static_cast<std::uint64_t>(sizeof(core::MultiwaySparseRowMetadata)));
}

TEST_CASE(multiway_p7_memory_sparse_values_account_for_two_double_tables) {
    const auto result = baseline();
    EXPECT_EQ(result.estimate.sparse_value_bytes, 12U * 2U * sizeof(double));
}

TEST_CASE(multiway_p7_memory_blueprint_index_bytes_are_exact) {
    core::MultiwayMemoryInputs inputs;
    inputs.blueprint_index_bytes = 1234U;
    EXPECT_EQ(baseline(inputs).estimate.blueprint_index_bytes, 1234U);
}

TEST_CASE(multiway_p7_memory_range_rows_account_for_vector_owners) {
    core::MultiwayMemoryInputs inputs;
    inputs.range_row_count = 6U;
    EXPECT_EQ(
        baseline(inputs).estimate.range_row_bytes,
        6U * static_cast<std::uint64_t>(sizeof(std::vector<core::MultiwayWeightedHole>)));
}

TEST_CASE(multiway_p7_memory_range_entries_account_for_source_and_compiled_copies) {
    core::MultiwayMemoryInputs inputs;
    inputs.range_entry_count = 7U;
    EXPECT_EQ(
        baseline(inputs).estimate.range_row_bytes,
        7U * 2U * static_cast<std::uint64_t>(sizeof(core::MultiwayWeightedHole)));
}

TEST_CASE(multiway_p7_memory_future_bucket_cache_bytes_are_exact) {
    core::MultiwayMemoryInputs inputs;
    inputs.future_bucket_cache_bytes = 4321U;
    EXPECT_EQ(baseline(inputs).estimate.future_bucket_cache_bytes, 4321U);
}

TEST_CASE(multiway_p7_memory_off_tree_menu_entries_use_action_descriptor_size) {
    core::MultiwayMemoryInputs inputs;
    inputs.off_tree_menu_entries = 9U;
    EXPECT_EQ(
        baseline(inputs).estimate.off_tree_menu_bytes,
        9U * static_cast<std::uint64_t>(sizeof(core::MultiwayActionDescriptor)));
}

TEST_CASE(multiway_p7_memory_continuation_scratch_scales_by_worker) {
    core::MultiwayMemoryInputs inputs;
    inputs.continuation_scratch_bytes_per_worker = 77U;
    EXPECT_EQ(baseline(inputs).estimate.continuation_scratch_bytes, 154U);
}

TEST_CASE(multiway_p7_memory_worker_delta_storage_is_nonzero) {
    EXPECT_TRUE(baseline().estimate.worker_delta_bytes > 0U);
}

TEST_CASE(multiway_p7_memory_merge_scratch_is_nonzero) {
    EXPECT_TRUE(baseline().estimate.merge_scratch_bytes > 0U);
}

TEST_CASE(multiway_p7_memory_export_accounts_for_two_probability_buffers) {
    core::MultiwayMemoryInputs inputs;
    inputs.export_action_capacity = 3U;
    EXPECT_EQ(
        baseline(inputs).estimate.export_bytes,
        3U * 2U * static_cast<std::uint64_t>(sizeof(core::MultiwayRootActionProbability)));
}

TEST_CASE(multiway_p7_memory_mandatory_total_excludes_optional_cache) {
    core::MultiwayMemoryInputs inputs;
    inputs.continuation_cache_bytes = 99U;
    const auto result = baseline(inputs);
    EXPECT_EQ(result.estimate.total_bytes, result.estimate.mandatory_bytes + 99U);
}

TEST_CASE(multiway_p7_memory_optional_cache_is_admitted_when_roomy) {
    core::MultiwayMemoryInputs inputs;
    inputs.continuation_cache_bytes = 99U;
    const auto result = baseline(inputs);
    EXPECT_TRUE(result.optional_continuation_cache_admitted);
    EXPECT_EQ(
        result.highest_admitted_stage,
        core::MultiwayMemoryAdmissionStage::OptionalContinuationCache);
}

TEST_CASE(multiway_p7_memory_admitted_bytes_include_roomy_optional_cache) {
    core::MultiwayMemoryInputs inputs;
    inputs.continuation_cache_bytes = 99U;
    const auto result = baseline(inputs);
    EXPECT_EQ(result.estimate.admitted_bytes, result.estimate.total_bytes);
}

TEST_CASE(multiway_p7_memory_optional_cache_degrades_above_operating_cap) {
    core::MultiwayMemoryInputs inputs;
    inputs.continuation_cache_bytes = 100U;
    const auto estimate = baseline(inputs).estimate;
    const auto result = core::preflight_multiway_memory(
        bounded_limits(),
        {1U, estimate.total_bytes + 1U, estimate.mandatory_bytes},
        inputs);
    EXPECT_EQ(result.status, core::MultiwayMemoryStatus::Warning);
    EXPECT_TRUE(result.degraded);
}

TEST_CASE(multiway_p7_memory_degraded_cache_is_not_partially_admitted) {
    core::MultiwayMemoryInputs inputs;
    inputs.continuation_cache_bytes = 100U;
    const auto estimate = baseline(inputs).estimate;
    const auto result = core::preflight_multiway_memory(
        bounded_limits(),
        {1U, estimate.total_bytes + 1U, estimate.mandatory_bytes},
        inputs);
    EXPECT_TRUE(!result.optional_continuation_cache_admitted);
    EXPECT_EQ(result.estimate.admitted_bytes, estimate.mandatory_bytes);
}

TEST_CASE(multiway_p7_memory_optional_cache_at_operating_cap_is_admitted) {
    core::MultiwayMemoryInputs inputs;
    inputs.continuation_cache_bytes = 100U;
    const auto estimate = baseline(inputs).estimate;
    const auto result = core::preflight_multiway_memory(
        bounded_limits(),
        {estimate.total_bytes, estimate.total_bytes + 1U, estimate.total_bytes},
        inputs);
    EXPECT_TRUE(result.optional_continuation_cache_admitted);
    EXPECT_TRUE(!result.degraded);
}

TEST_CASE(multiway_p7_memory_exact_warning_threshold_reports_warning) {
    const auto estimate = baseline().estimate;
    const auto result = core::preflight_multiway_memory(
        bounded_limits(),
        {estimate.mandatory_bytes, estimate.mandatory_bytes + 1U, estimate.mandatory_bytes});
    EXPECT_EQ(result.status, core::MultiwayMemoryStatus::Warning);
}

TEST_CASE(multiway_p7_memory_root_above_operating_cap_is_rejected_before_admission) {
    const auto estimate = baseline().estimate;
    const auto root = root_bytes(estimate);
    const auto result = core::preflight_multiway_memory(
        bounded_limits(), {1U, estimate.mandatory_bytes + 1U, root - 1U});
    EXPECT_EQ(result.status, core::MultiwayMemoryStatus::Rejected);
    EXPECT_EQ(result.highest_admitted_stage, core::MultiwayMemoryAdmissionStage::None);
}

TEST_CASE(multiway_p7_memory_root_at_operating_cap_is_admitted) {
    const auto estimate = baseline().estimate;
    const auto root = root_bytes(estimate);
    const auto result = core::preflight_multiway_memory(
        bounded_limits(), {root, estimate.mandatory_bytes + 1U, root});
    EXPECT_EQ(result.highest_admitted_stage, core::MultiwayMemoryAdmissionStage::Root);
}

TEST_CASE(multiway_p7_memory_sparse_rows_above_operating_cap_stop_at_root) {
    const auto estimate = baseline().estimate;
    const auto root = root_bytes(estimate);
    const auto result = core::preflight_multiway_memory(
        bounded_limits(),
        {1U, estimate.mandatory_bytes + 1U, root + row_bytes(estimate) - 1U});
    EXPECT_EQ(result.status, core::MultiwayMemoryStatus::Rejected);
    EXPECT_EQ(result.highest_admitted_stage, core::MultiwayMemoryAdmissionStage::Root);
    EXPECT_EQ(result.estimate.admitted_bytes, root);
}

TEST_CASE(multiway_p7_memory_worker_deltas_above_operating_cap_stop_after_rows) {
    const auto estimate = baseline().estimate;
    const auto through_rows = root_bytes(estimate) + row_bytes(estimate);
    const auto result = core::preflight_multiway_memory(
        bounded_limits(), {1U, estimate.mandatory_bytes + 1U, estimate.mandatory_bytes - 1U});
    EXPECT_EQ(result.status, core::MultiwayMemoryStatus::Rejected);
    EXPECT_EQ(result.highest_admitted_stage, core::MultiwayMemoryAdmissionStage::SparseRows);
    EXPECT_EQ(result.estimate.admitted_bytes, through_rows);
}

TEST_CASE(multiway_p7_memory_public_state_estimate_saturates_on_overflow) {
    auto limits = bounded_limits();
    limits.max_public_states = std::numeric_limits<std::size_t>::max();
    const auto result = core::preflight_multiway_memory(limits);
    EXPECT_EQ(result.estimate.public_state_bytes, std::numeric_limits<std::uint64_t>::max());
    EXPECT_EQ(result.status, core::MultiwayMemoryStatus::Rejected);
}

TEST_CASE(multiway_p7_memory_sparse_value_estimate_saturates_on_overflow) {
    auto limits = bounded_limits();
    limits.max_sparse_values = std::numeric_limits<std::size_t>::max();
    const auto result = core::preflight_multiway_memory(limits);
    EXPECT_EQ(result.estimate.sparse_value_bytes, std::numeric_limits<std::uint64_t>::max());
    EXPECT_EQ(result.status, core::MultiwayMemoryStatus::Rejected);
}

TEST_CASE(multiway_p7_memory_optional_cache_total_saturates_on_overflow) {
    core::MultiwayMemoryInputs inputs;
    inputs.continuation_cache_bytes = std::numeric_limits<std::uint64_t>::max();
    const auto result = baseline(inputs);
    EXPECT_EQ(result.estimate.total_bytes, std::numeric_limits<std::uint64_t>::max());
    EXPECT_TRUE(result.degraded);
}

TEST_CASE(multiway_p7_memory_blueprint_root_overflow_rejects_without_admission) {
    core::MultiwayMemoryInputs inputs;
    inputs.blueprint_index_bytes = std::numeric_limits<std::uint64_t>::max();
    const auto result = baseline(inputs);
    EXPECT_EQ(result.status, core::MultiwayMemoryStatus::Rejected);
    EXPECT_EQ(result.highest_admitted_stage, core::MultiwayMemoryAdmissionStage::None);
}
