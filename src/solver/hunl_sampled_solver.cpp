#include "solver/hunl_sampled_solver.hpp"

#include "solver/hunl_sampled_range.hpp"
#include "solver/hunl_sampled_trajectory.hpp"

#include <algorithm>
#include <chrono>
#include <limits>
#include <stdexcept>
#include <thread>
#include <utility>

namespace texas::solver::hunl {
namespace {

constexpr std::size_t kDeltaEntriesPerTrajectory = 4096U;
constexpr std::uint64_t kSaturatedMemoryEstimate = std::numeric_limits<std::uint64_t>::max();

std::uint64_t saturating_add(std::uint64_t left, std::uint64_t right) noexcept {
    return left > kSaturatedMemoryEstimate - right ? kSaturatedMemoryEstimate : left + right;
}

std::uint64_t saturating_multiply(std::uint64_t left, std::uint64_t right) noexcept {
    if (left == 0U || right == 0U) return 0U;
    return left > kSaturatedMemoryEstimate / right ? kSaturatedMemoryEstimate : left * right;
}

std::uint64_t saturating_size(std::size_t value) noexcept {
    if constexpr (sizeof(std::size_t) > sizeof(std::uint64_t)) {
        if (value > static_cast<std::size_t>(kSaturatedMemoryEstimate)) {
            return kSaturatedMemoryEstimate;
        }
    }
    return static_cast<std::uint64_t>(value);
}

const char* message_for_status(HUNLSampledMemoryStatus status) noexcept {
    switch (status) {
        case HUNLSampledMemoryStatus::Ok:
            return "sampled structured memory preflight passed";
        case HUNLSampledMemoryStatus::Warning:
            return "sampled structured memory preflight exceeded warning threshold";
        case HUNLSampledMemoryStatus::Rejected:
            return "sampled structured memory preflight exceeded hard limit";
    }
    return "sampled structured memory preflight unknown";
}

HUNLSampledStorage make_sampled_storage(const HUNLSampledSolverConfig& config) {
    validate_sampled_config_or_throw(config);
    return HUNLSampledStorage(config.layout, config.precision);
}

}  // namespace

HUNLSampledSolver::HUNLSampledSolver(HUNLSampledSolverConfig config)
    : config_(config), storage_(make_sampled_storage(config)) {
    validate_sampled_config_or_throw(config_);
}

HUNLSampledSolveResult HUNLSampledSolver::solve_for(
    const HUNLSampledSolveRequest& request,
    std::chrono::milliseconds budget) {
    structured_session_.reset();
    if (budget.count() <= 0) return run_batches(request, 0U);
    HUNLSampledSolver staged(config_);
    const auto result = staged.run_batches_impl(
        request,
        std::numeric_limits<std::uint32_t>::max(),
        std::chrono::steady_clock::now() + budget);
    if (!result.timed_out) {
        config_ = std::move(staged.config_);
        storage_ = std::move(staged.storage_);
        profile_ = std::move(staged.profile_);
        root_strategy_ = std::move(staged.root_strategy_);
    }
    return result;
}

HUNLSampledSolveResult HUNLSampledSolver::run_batches(
    const HUNLSampledSolveRequest& request,
    std::uint32_t batches) {
    structured_session_.reset();
    HUNLSampledSolver staged(config_);
    const auto result = staged.run_batches_impl(request, batches);
    config_ = std::move(staged.config_);
    storage_ = std::move(staged.storage_);
    profile_ = std::move(staged.profile_);
    root_strategy_ = std::move(staged.root_strategy_);
    return result;
}

void HUNLSampledSolver::begin_structured_session(
    HUNLStructuredRootRequest root,
    const HUNLLeafEvaluator* leaf_evaluator) {
    root.validate();
    const HUNLSampledSolveRequest request{root, leaf_evaluator};
    const auto preflight_result = preflight(request);
    if (preflight_result.status == HUNLSampledMemoryStatus::Rejected) {
        throw std::runtime_error(preflight_result.message);
    }

    structured_session_.reset();
    storage_.clear_keep_capacity();
    profile_.reset();
    root_strategy_ = {};
    config_ = preflight_result.effective_config;

    std::uint64_t mutable_growth_limit = 0U;
    if (config_.enable_memory_guardrails) {
        const auto reserved = estimate_hunl_sampled_range_memory(root, config_).peak_bytes();
        if (reserved >= config_.memory_fail_bytes) {
            throw std::runtime_error("sampled memory reservation leaves no safe range-storage growth budget");
        }
        mutable_growth_limit = (config_.memory_fail_bytes - reserved) / 2U;
    }
    storage_.set_memory_limit_bytes(mutable_growth_limit);
    structured_session_ = std::make_unique<HUNLSampledRangeSession>(
        std::move(root), config_, storage_, profile_, 0U, leaf_evaluator, mutable_growth_limit);
    root_strategy_ = structured_session_->export_root_strategy();
}

HUNLSampledSolveResult HUNLSampledSolver::resume_structured_batches(
    std::uint32_t batches,
    std::optional<std::chrono::steady_clock::time_point> deadline) {
    if (structured_session_ == nullptr) {
        throw std::logic_error("no structured sampled session has been started");
    }
    const auto range_run = structured_session_->resume_batches(batches, deadline);
    root_strategy_ = range_run.root_strategy;
    const auto final_memory = memory_estimate();
    profile_.record_sparse_storage(storage_.row_count(), storage_.total_value_count());
    profile_.record_memory_budget(
        final_memory.public_states_cached,
        final_memory.infoset_rows_allocated,
        final_memory.sparse_values_allocated,
        final_memory.terminal_cache_bytes,
        final_memory.worker_delta_bytes,
        final_memory.export_bytes,
        final_memory.total_bytes(),
        false,
        false);
    profile_.record_observed_memory(final_memory.total_bytes(), final_memory.total_bytes());

    return HUNLSampledSolveResult{
        root_strategy_,
        range_run.range_wide_root_strategy,
        profile_.snapshot(),
        range_run.batches_completed,
        range_run.timed_out};
}

bool HUNLSampledSolver::has_structured_session() const noexcept {
    return structured_session_ != nullptr;
}

HUNLSampledSolveResult HUNLSampledSolver::run_batches_impl(
    const HUNLSampledSolveRequest& request,
    std::uint32_t batches,
    std::optional<std::chrono::steady_clock::time_point> deadline) {
    request.structured_root.validate();
    storage_.clear_keep_capacity();
    profile_.reset();
    root_strategy_ = {};

    const auto preflight_result = preflight(request);
    if (preflight_result.status == HUNLSampledMemoryStatus::Rejected) {
        profile_.record_memory_budget(
            preflight_result.estimate.public_states_cached,
            preflight_result.estimate.infoset_rows_allocated,
            preflight_result.estimate.sparse_values_allocated,
            preflight_result.estimate.terminal_cache_bytes,
            preflight_result.estimate.worker_delta_bytes,
            preflight_result.estimate.export_bytes,
            preflight_result.estimate.total_bytes(),
            false,
            true);
        throw std::runtime_error(preflight_result.message);
    }

    config_ = preflight_result.effective_config;
    std::uint64_t mutable_growth_limit = 0U;
    if (config_.enable_memory_guardrails) {
        const auto reserved = estimate_hunl_sampled_range_memory(
            request.structured_root, config_).peak_bytes();
        if (reserved >= config_.memory_fail_bytes) {
            throw std::runtime_error("sampled memory reservation leaves no safe range-storage growth budget");
        }
        mutable_growth_limit = (config_.memory_fail_bytes - reserved) / 2U;
    }
    storage_.set_memory_limit_bytes(mutable_growth_limit);
    const auto range_run = run_hunl_sampled_structured_range_batches(
        request.structured_root,
        config_,
        0U,
        batches,
        deadline,
        storage_,
        profile_,
        request.leaf_evaluator,
        mutable_growth_limit);
    root_strategy_ = range_run.root_strategy;

    const auto final_memory = memory_estimate();
    profile_.record_sparse_storage(storage_.row_count(), storage_.total_value_count());
    profile_.record_memory_budget(
        final_memory.public_states_cached,
        final_memory.infoset_rows_allocated,
        final_memory.sparse_values_allocated,
        final_memory.terminal_cache_bytes,
        final_memory.worker_delta_bytes,
        final_memory.export_bytes,
        final_memory.total_bytes(),
        preflight_result.status == HUNLSampledMemoryStatus::Warning,
        false);
    profile_.record_observed_memory(
        final_memory.total_bytes(),
        std::max(final_memory.total_bytes(), preflight_result.estimate.total_bytes()));

    return HUNLSampledSolveResult{
        root_strategy_,
        range_run.range_wide_root_strategy,
        profile_.snapshot(),
        range_run.batches_completed,
        range_run.timed_out};
}

HUNLSampledRootStrategy HUNLSampledSolver::export_root_strategy() const {
    return root_strategy_;
}

const HUNLSampledProfile& HUNLSampledSolver::profile() const noexcept {
    return profile_;
}

HUNLSampledMemoryEstimate HUNLSampledSolver::memory_estimate() const noexcept {
    const auto storage_memory = storage_.memory_estimate();
    HUNLSampledMemoryEstimate estimate;
    estimate.infoset_row_bytes = storage_memory.total_bytes();
    estimate.infoset_rows_allocated = storage_memory.sparse_rows;
    estimate.sparse_values_allocated = storage_memory.sparse_values;
    estimate.export_bytes = saturating_multiply(16U, sizeof(HUNLSampledActionProbability));
    if (structured_session_ != nullptr) {
        const auto range_memory = structured_session_->memory_estimate();
        estimate.structured_joint_deal_bytes = range_memory.joint_deal_bytes;
        estimate.structured_infoset_lookup_bytes = range_memory.infoset_lookup_bytes;
        estimate.structured_session_bytes = range_memory.retained_bytes;
        estimate.export_bytes = saturating_add(estimate.export_bytes, range_memory.export_bytes);
    }
    estimate.total_bytes_live = saturating_add(
        estimate.infoset_row_bytes,
        saturating_add(estimate.export_bytes, estimate.structured_session_bytes));
    return estimate;
}

HUNLSampledMemoryPreflight HUNLSampledSolver::preflight(
    const HUNLSampledSolveRequest& request) const noexcept {
    return build_preflight_result(request, config_);
}

const HUNLSampledSolverConfig& HUNLSampledSolver::config() const noexcept {
    return config_;
}

HUNLSampledStorage& HUNLSampledSolver::storage() noexcept {
    return storage_;
}

const HUNLSampledStorage& HUNLSampledSolver::storage() const noexcept {
    return storage_;
}

HUNLSampledMemoryEstimate HUNLSampledSolver::estimate_memory_for(
    const HUNLSampledSolveRequest& request,
    const HUNLSampledSolverConfig& config) const noexcept {
    HUNLSampledMemoryEstimate estimate;
    const auto infoset_rows = std::max<std::uint64_t>(
        1'024U, saturating_multiply(static_cast<std::uint64_t>(config.minibatch_size), 4'096U));
    estimate.infoset_rows_allocated = infoset_rows;
    estimate.sparse_values_allocated = saturating_multiply(
        saturating_multiply(infoset_rows, 1U), 16U);
    estimate.infoset_row_bytes = saturating_multiply(
        estimate.sparse_values_allocated, sizeof(float) * 2ULL);
    const auto range_memory = estimate_hunl_sampled_range_memory(request.structured_root, config);
    estimate.structured_joint_deal_bytes = range_memory.joint_deal_bytes;
    estimate.structured_infoset_lookup_bytes = range_memory.infoset_lookup_bytes;
    estimate.structured_session_bytes = range_memory.retained_bytes;
    estimate.worker_delta_bytes = range_memory.batch_scratch_bytes;
    estimate.export_bytes = range_memory.export_bytes;

    const auto retained = memory_estimate();
    estimate.infoset_row_bytes = std::max(estimate.infoset_row_bytes, retained.infoset_row_bytes);
    estimate.structured_joint_deal_bytes = std::max(
        estimate.structured_joint_deal_bytes, retained.structured_joint_deal_bytes);
    estimate.structured_infoset_lookup_bytes = std::max(
        estimate.structured_infoset_lookup_bytes, retained.structured_infoset_lookup_bytes);
    estimate.structured_session_bytes = std::max(
        estimate.structured_session_bytes, retained.structured_session_bytes);
    estimate.total_bytes_live = saturating_add(
        saturating_add(estimate.public_state_cache_bytes, estimate.infoset_row_bytes),
        saturating_add(
            estimate.worker_delta_bytes,
            saturating_add(estimate.export_bytes, estimate.structured_session_bytes)));
    return estimate;
}

HUNLSampledMemoryPreflight HUNLSampledSolver::build_preflight_result(
    const HUNLSampledSolveRequest& request,
    const HUNLSampledSolverConfig& config) const noexcept {
    HUNLSampledMemoryPreflight result;
    result.effective_config = config;
    result.estimate = estimate_memory_for(request, result.effective_config);
    if (config.enable_memory_guardrails) {
        while (config.adaptive_memory_fallback &&
               result.estimate.total_bytes() > result.effective_config.memory_fail_bytes &&
               result.adjustments.recorded_step_count < HUNLSampledAdaptiveAdjustments::kMaxRecordedSteps) {
            const auto before = result.effective_config;
            auto candidate = before;
            auto candidate_adjustments = result.adjustments;
            apply_adaptive_fallback(candidate, candidate_adjustments);
            if (candidate.minibatch_size == before.minibatch_size) break;
            const auto old_estimate = result.estimate.total_bytes();
            const auto new_estimate = estimate_memory_for(request, candidate);
            if (new_estimate.total_bytes() >= old_estimate) break;
            result.effective_config = candidate;
            result.adjustments = candidate_adjustments;
            const auto step = result.adjustments.recorded_step_count++;
            result.adjustments.estimate_before[step] = old_estimate;
            result.adjustments.estimate_after[step] = new_estimate.total_bytes();
            result.estimate = new_estimate;
        }
    }
    if (!config.enable_memory_guardrails ||
        result.estimate.total_bytes() <= result.effective_config.memory_fail_bytes) {
        result.status = result.estimate.total_bytes() > result.effective_config.memory_warning_bytes
            ? HUNLSampledMemoryStatus::Warning : HUNLSampledMemoryStatus::Ok;
    } else {
        result.status = HUNLSampledMemoryStatus::Rejected;
    }
    result.message = message_for_status(result.status);
    return result;
}

void HUNLSampledSolver::apply_adaptive_fallback(
    HUNLSampledSolverConfig& config,
    HUNLSampledAdaptiveAdjustments& adjustments) noexcept {
    if (config.minibatch_size > 1U) {
        config.minibatch_size = std::max<std::uint32_t>(1U, config.minibatch_size / 2U);
        adjustments.reduced_minibatch = true;
        return;
    }
}

std::uint64_t HUNLSampledSolver::estimate_worker_delta_bytes(
    const HUNLSampledSolveRequest&,
    const HUNLSampledSolverConfig& config) noexcept {
    const auto trajectories = std::max<std::uint64_t>(1U, config.minibatch_size);
    const auto workers = std::min(
        std::max<std::uint64_t>(1U, saturating_size(config.workers)), trajectories);
    const auto largest_partition = (trajectories + workers - 1U) / workers;
    const auto entries = saturating_multiply(largest_partition + 1U, kDeltaEntriesPerTrajectory);
    return saturating_multiply(
        workers,
        saturating_add(
            saturating_multiply(entries, sizeof(HUNLSampledValueDelta)),
            sizeof(HUNLSampledWorkerScratch) + sizeof(HUNLSampledTraversalResult) + sizeof(std::thread)));
}

std::uint64_t HUNLSampledSolver::estimate_export_bytes(
    const HUNLSampledSolveRequest&) noexcept {
    return saturating_multiply(16U, sizeof(HUNLSampledActionProbability));
}

std::uint64_t HUNLSampledSolver::estimate_terminal_cache_bytes(
    const HUNLSampledSolveRequest&,
    const HUNLSampledSolverConfig&) noexcept {
    return 0U;
}

}  // namespace texas::solver::hunl
