#include "solver/hunl_sampled_solver.hpp"

namespace core {

HUNLSampledSolver::HUNLSampledSolver(HUNLSampledSolverConfig config)
    : config_(config),
      storage_(config.layout, config.precision) {
    validate_sampled_config_or_throw(config_);
}

HUNLSampledSolveResult HUNLSampledSolver::solve_for(
    const HUNLSampledSolveRequest& request,
    std::chrono::milliseconds budget) {
    const auto batches = budget.count() > 0 ? 1U : 0U;
    return run_batches(request, batches);
}

HUNLSampledSolveResult HUNLSampledSolver::run_batches(
    const HUNLSampledSolveRequest& request,
    std::uint32_t batches) {
    profile_.reset();
    profile_.record_sparse_storage(storage_.row_count(), storage_.total_value_count());
    profile_.record_traversal(
        static_cast<std::uint64_t>(batches) * config_.traversals_per_iteration,
        0,
        0);

    root_strategy_ = HUNLSampledStrategyExporter::export_uniform(request.root_action_count);

    HUNLSampledSolveResult result;
    result.root_strategy = root_strategy_;
    result.profile = profile_.snapshot();
    result.batches_completed = batches;
    result.timed_out = false;
    return result;
}

HUNLSampledRootStrategy HUNLSampledSolver::export_root_strategy() const {
    return root_strategy_;
}

const HUNLSampledProfile& HUNLSampledSolver::profile() const noexcept {
    return profile_;
}

HUNLSampledMemoryEstimate HUNLSampledSolver::memory_estimate() const noexcept {
    const auto storage_memory = storage_.memory_estimate();
    return {
        storage_memory.total_bytes(),
        storage_memory.sparse_rows,
        storage_memory.sparse_values,
    };
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

}  // namespace core
