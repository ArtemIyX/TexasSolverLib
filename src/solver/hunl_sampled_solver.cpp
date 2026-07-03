#include "solver/hunl_sampled_solver.hpp"

namespace core {

HUNLSampledSolver::HUNLSampledSolver(HUNLSampledSolverConfig config)
    : config_(config),
      builder_(HUNLSampledBuilderConfig{config.use_public_chance_isomorphism}),
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
    if (request.root_state.has_value()) {
        builder_.initialize(*request.root_state);
    }
    profile_.record_sparse_storage(storage_.row_count(), storage_.total_value_count());
    profile_.record_traversal(
        static_cast<std::uint64_t>(batches) * config_.traversals_per_iteration,
        0,
        0);

    const auto root_action_count = request.root_state.has_value()
        ? static_cast<std::uint8_t>(request.root_state->legal_actions().size())
        : request.root_action_count;
    root_strategy_ = HUNLSampledStrategyExporter::export_uniform(root_action_count);

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
    const auto builder_memory = builder_.memory_estimate();
    const auto storage_memory = storage_.memory_estimate();
    return {
        builder_memory.total_bytes(),
        builder_memory.nodes,
        builder_memory.edges,
        storage_memory.total_bytes(),
        storage_memory.sparse_rows,
        storage_memory.sparse_values,
    };
}

const HUNLSampledSolverConfig& HUNLSampledSolver::config() const noexcept {
    return config_;
}

HUNLSampledBuilder& HUNLSampledSolver::builder() noexcept {
    return builder_;
}

const HUNLSampledBuilder& HUNLSampledSolver::builder() const noexcept {
    return builder_;
}

HUNLSampledStorage& HUNLSampledSolver::storage() noexcept {
    return storage_;
}

const HUNLSampledStorage& HUNLSampledSolver::storage() const noexcept {
    return storage_;
}

}  // namespace core
