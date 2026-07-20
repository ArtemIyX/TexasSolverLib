#include "solver/hunl_sampled_solver.hpp"

#include <algorithm>
#include <stdexcept>

namespace core {

namespace {

constexpr std::uint64_t kDefaultPublicStateBytes = 512ULL;
constexpr std::uint64_t kDefaultTerminalCachePerStateBytes = 128ULL;
constexpr std::uint64_t kWorkerDeltaBytesPerTraversal = 4096ULL;
constexpr std::uint64_t kBuilderCacheNodeFloor = 1ULL;

std::uint8_t infer_root_action_count(const HUNLSampledSolveRequest& request) noexcept {
    return request.root_state.has_value()
        ? static_cast<std::uint8_t>(request.root_state->legal_actions().size())
        : request.root_action_count;
}

std::uint64_t infer_bucket_count(const HUNLSampledSolveRequest& request, const HUNLSampledSolverConfig& config) noexcept {
    if (config.bucket_count_hint > 0) {
        return static_cast<std::uint64_t>(config.bucket_count_hint);
    }
    if (request.root_state.has_value() && request.root_state->hole_cards.has_value()) {
        return 1326ULL;
    }
    return 128ULL;
}

const char* message_for_status(HUNLSampledMemoryStatus status) noexcept {
    switch (status) {
        case HUNLSampledMemoryStatus::Ok:
            return "sampled memory preflight passed";
        case HUNLSampledMemoryStatus::Warning:
            return "sampled memory preflight exceeded warning threshold";
        case HUNLSampledMemoryStatus::Rejected:
            return "sampled memory preflight exceeded hard limit";
    }
    return "sampled memory preflight unknown";
}

}  // namespace

HUNLSampledSolverNotReady::HUNLSampledSolverNotReady()
    : std::logic_error(
          "HUNLSampledSolver cannot run positive-work requests until sampled MCCFR is implemented") {
}

HUNLSampledSolver::HUNLSampledSolver(HUNLSampledSolverConfig config)
    : config_(config),
      builder_(HUNLSampledBuilderConfig{config.use_public_chance_isomorphism}),
      storage_(config.layout, config.precision) {
    validate_sampled_config_or_throw(config_);
}

HUNLSampledSolveResult HUNLSampledSolver::solve_for(
    const HUNLSampledSolveRequest& request,
    std::chrono::milliseconds budget) {
    if (budget.count() > 0) {
        throw HUNLSampledSolverNotReady{};
    }
    return run_batches(request, 0);
}

HUNLSampledSolveResult HUNLSampledSolver::run_batches(
    const HUNLSampledSolveRequest& request,
    std::uint32_t batches) {
    if (batches > 0) {
        throw HUNLSampledSolverNotReady{};
    }

    profile_.reset();

    auto preflight_result = preflight(request);
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
    if (request.root_state.has_value()) {
        builder_.initialize(*request.root_state);
        if (config_.max_cached_public_states > 0 &&
            builder_.node_count() > static_cast<std::size_t>(config_.max_cached_public_states)) {
            throw std::runtime_error("sampled solver exceeded max_cached_public_states during initialization");
        }
    }

    const auto live_memory = memory_estimate();
    profile_.record_sparse_storage(storage_.row_count(), storage_.total_value_count());
    profile_.record_memory_budget(
        live_memory.public_states_cached,
        live_memory.infoset_rows_allocated,
        live_memory.sparse_values_allocated,
        live_memory.terminal_cache_bytes,
        live_memory.worker_delta_bytes,
        live_memory.export_bytes,
        live_memory.total_bytes(),
        preflight_result.status == HUNLSampledMemoryStatus::Warning,
        false);
    root_strategy_ = HUNLSampledStrategyExporter::export_uniform(infer_root_action_count(request));

    HUNLSampledSolveResult result;
    result.root_strategy = root_strategy_;
    result.profile = profile_.snapshot();
    result.batches_completed = 0;
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
    HUNLSampledMemoryEstimate estimate;
    estimate.public_state_cache_bytes = builder_memory.total_bytes();
    estimate.public_states_cached = builder_memory.nodes;
    estimate.public_state_edges = builder_memory.edges;
    estimate.infoset_row_bytes = storage_memory.total_bytes();
    estimate.infoset_rows_allocated = storage_memory.sparse_rows;
    estimate.sparse_values_allocated = storage_memory.sparse_values;
    estimate.terminal_cache_bytes = 0;
    estimate.worker_delta_bytes = estimate_worker_delta_bytes({}, config_);
    estimate.export_bytes = static_cast<std::uint64_t>(root_strategy_.actions.capacity()) *
        sizeof(HUNLSampledActionProbability);
    estimate.total_bytes_live =
        estimate.public_state_cache_bytes +
        estimate.infoset_row_bytes +
        estimate.terminal_cache_bytes +
        estimate.worker_delta_bytes +
        estimate.export_bytes;
    return estimate;
}

HUNLSampledMemoryPreflight HUNLSampledSolver::preflight(
    const HUNLSampledSolveRequest& request) const noexcept {
    return build_preflight_result(request, config_);
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

HUNLSampledMemoryEstimate HUNLSampledSolver::estimate_memory_for(
    const HUNLSampledSolveRequest& request,
    const HUNLSampledSolverConfig& config) const noexcept {
    HUNLSampledMemoryEstimate estimate;

    const auto public_states = config.max_cached_public_states > 0
        ? static_cast<std::uint64_t>(config.max_cached_public_states)
        : std::max<std::uint64_t>(
            kBuilderCacheNodeFloor,
            static_cast<std::uint64_t>(config.minibatch_size) *
                static_cast<std::uint64_t>(config.workers));
    estimate.public_states_cached = public_states;
    estimate.public_state_edges = public_states * std::max<std::uint64_t>(1U, infer_root_action_count(request));
    estimate.public_state_cache_bytes = public_states * kDefaultPublicStateBytes;

    const auto bucket_count = infer_bucket_count(request, config);
    const auto action_count = std::max<std::uint64_t>(1U, infer_root_action_count(request));
    const auto infoset_rows = std::max<std::uint64_t>(
        1ULL,
        static_cast<std::uint64_t>(config.minibatch_size) *
            static_cast<std::uint64_t>(config.workers));
    estimate.infoset_rows_allocated = infoset_rows;
    estimate.sparse_values_allocated = infoset_rows * bucket_count * action_count;
    estimate.infoset_row_bytes =
        infoset_rows * (sizeof(HUNLSampledInfosetMeta) + sizeof(InfosetId) + sizeof(std::size_t) + sizeof(void*) * 2ULL) +
        estimate.sparse_values_allocated * sizeof(float) * 2ULL;
    estimate.terminal_cache_bytes = estimate_terminal_cache_bytes(request, config);
    estimate.worker_delta_bytes = estimate_worker_delta_bytes(request, config);
    estimate.export_bytes = estimate_export_bytes(request);
    estimate.total_bytes_live =
        estimate.public_state_cache_bytes +
        estimate.infoset_row_bytes +
        estimate.terminal_cache_bytes +
        estimate.worker_delta_bytes +
        estimate.export_bytes;
    return estimate;
}

HUNLSampledMemoryPreflight HUNLSampledSolver::build_preflight_result(
    const HUNLSampledSolveRequest& request,
    const HUNLSampledSolverConfig& config) const noexcept {
    HUNLSampledMemoryPreflight result;
    result.effective_config = config;

    if (!config.enable_memory_guardrails) {
        result.estimate = estimate_memory_for(request, config);
        result.message = message_for_status(HUNLSampledMemoryStatus::Ok);
        return result;
    }

    if (!config.lazy_public_expansion && !config.sparse_infosets) {
        result.status = HUNLSampledMemoryStatus::Rejected;
        result.estimate = estimate_memory_for(request, config);
        result.message = "sampled solver refuses non-lazy plus non-sparse production mode";
        return result;
    }

    result.estimate = estimate_memory_for(request, result.effective_config);
    while (config.adaptive_memory_fallback &&
           result.estimate.total_bytes() > result.effective_config.memory_fail_bytes) {
        const auto before = result.effective_config;
        apply_adaptive_fallback(result.effective_config, result.adjustments);
        if (result.effective_config.traversals_per_iteration == before.traversals_per_iteration &&
            result.effective_config.minibatch_size == before.minibatch_size &&
            result.effective_config.use_average_strategy_sampling == before.use_average_strategy_sampling &&
            result.effective_config.bucket_count_hint == before.bucket_count_hint &&
            result.effective_config.depth_limit_plies_hint == before.depth_limit_plies_hint) {
            break;
        }
        result.estimate = estimate_memory_for(request, result.effective_config);
    }

    if (result.estimate.total_bytes() > result.effective_config.memory_fail_bytes) {
        result.status = HUNLSampledMemoryStatus::Rejected;
    } else if (result.estimate.total_bytes() > result.effective_config.memory_warning_bytes) {
        result.status = HUNLSampledMemoryStatus::Warning;
    } else {
        result.status = HUNLSampledMemoryStatus::Ok;
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
    if (config.traversals_per_iteration > 1U) {
        config.traversals_per_iteration = std::max<std::uint32_t>(1U, config.traversals_per_iteration / 2U);
        adjustments.reduced_traversals = true;
        return;
    }
    if (config.use_average_strategy_sampling) {
        config.use_average_strategy_sampling = false;
        if (config.mode == HUNLFlatSamplingMode::AverageStrategy) {
            config.mode = HUNLFlatSamplingMode::External;
        }
        adjustments.disabled_average_strategy_sampling = true;
        return;
    }
    if (config.bucket_count_hint > 32U) {
        config.bucket_count_hint = std::max<std::size_t>(32U, config.bucket_count_hint / 2U);
        adjustments.reduced_bucket_hint = true;
        return;
    }
    ++config.depth_limit_plies_hint;
    adjustments.increased_depth_limit_hint = true;
}

std::uint64_t HUNLSampledSolver::estimate_worker_delta_bytes(
    const HUNLSampledSolveRequest& request,
    const HUNLSampledSolverConfig& config) noexcept {
    const auto action_count = std::max<std::uint64_t>(1U, infer_root_action_count(request));
    const auto worker_factor =
        static_cast<std::uint64_t>(std::max<std::size_t>(1U, config.workers)) *
        static_cast<std::uint64_t>(std::max<std::uint32_t>(1U, config.minibatch_size));
    return worker_factor * (kWorkerDeltaBytesPerTraversal + action_count * sizeof(double) * 8ULL);
}

std::uint64_t HUNLSampledSolver::estimate_export_bytes(const HUNLSampledSolveRequest& request) noexcept {
    return static_cast<std::uint64_t>(std::max<std::uint8_t>(1U, infer_root_action_count(request))) *
        sizeof(HUNLSampledActionProbability);
}

std::uint64_t HUNLSampledSolver::estimate_terminal_cache_bytes(
    const HUNLSampledSolveRequest& request,
    const HUNLSampledSolverConfig& config) noexcept {
    const auto depth_factor = 1ULL + static_cast<std::uint64_t>(config.depth_limit_plies_hint);
    const auto public_states = config.max_cached_public_states > 0
        ? static_cast<std::uint64_t>(config.max_cached_public_states)
        : std::max<std::uint64_t>(1ULL, static_cast<std::uint64_t>(config.minibatch_size));
    const auto action_factor = std::max<std::uint64_t>(1U, infer_root_action_count(request));
    return public_states * action_factor * depth_factor * kDefaultTerminalCachePerStateBytes;
}

}  // namespace core
