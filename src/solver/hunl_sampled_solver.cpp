#include "solver/hunl_sampled_solver.hpp"
#include "solver/hunl_sampled_scheduler.hpp"
#include "solver/hunl_sampled_traversal.hpp"
#include "util/pcs.hpp"

#include <algorithm>
#include <limits>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <vector>

namespace core {

namespace {

constexpr std::uint64_t kDefaultPublicStateBytes = 512ULL;
constexpr std::uint64_t kDefaultTerminalCachePerStateBytes = 128ULL;
constexpr std::size_t kWorkerDeltaEntriesPerTraversal = 4096U;
constexpr std::uint64_t kBuilderCacheNodeFloor = 1ULL;

constexpr std::uint64_t kSaturatedMemoryEstimate = std::numeric_limits<std::uint64_t>::max();

std::uint64_t saturating_add(std::uint64_t left, std::uint64_t right) noexcept {
    return left > kSaturatedMemoryEstimate - right ? kSaturatedMemoryEstimate : left + right;
}

std::uint64_t saturating_multiply(std::uint64_t left, std::uint64_t right) noexcept {
    if (left == 0 || right == 0) {
        return 0;
    }
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

std::uint8_t infer_root_action_count(const HUNLSampledSolveRequest& request) noexcept {
    return request.root_state.has_value()
        ? static_cast<std::uint8_t>(request.root_state->legal_actions().size())
        : request.root_action_count;
}

std::uint64_t infer_bucket_count(const HUNLSampledSolveRequest& request, const HUNLSampledSolverConfig& config) noexcept {
    if (request.root_state.has_value() && request.root_state->hole_cards.has_value()) {
        return 1ULL;
    }
    if (config.bucket_count_hint > 0) {
        return saturating_size(config.bucket_count_hint);
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

HUNLSampledStorage make_sampled_storage(const HUNLSampledSolverConfig& config) {
    validate_sampled_config_or_throw(config);
    return HUNLSampledStorage(config.layout, config.precision);
}

std::size_t effective_public_state_cap(const HUNLSampledSolverConfig& config) noexcept {
    if (config.max_cached_public_states > 0U) return config.max_cached_public_states;
    const auto workers = std::max<std::size_t>(1U, config.workers);
    const auto minibatch = static_cast<std::size_t>(config.minibatch_size);
    if (minibatch != 0U && workers > std::numeric_limits<std::size_t>::max() / minibatch) {
        return std::numeric_limits<std::size_t>::max();
    }
    return std::max<std::size_t>(1U, minibatch * workers);
}

std::size_t sample_joint_deal(const std::vector<HUNLJointRangeDeal>& deals, std::uint64_t seed) {
    PcsRng rng(seed);
    const auto draw = rng.next_unit_f64();
    double cumulative = 0.0;
    for (std::size_t index = 0; index < deals.size(); ++index) {
        cumulative += deals[index].weight;
        if (draw < cumulative) return index;
    }
    return deals.size() - 1U;
}

}  // namespace

HUNLSampledSolverNotReady::HUNLSampledSolverNotReady()
    : std::logic_error(
          "positive sampled batches require an explicit fixed-private-card root state") {
}

HUNLSampledTimedSolveNotReady::HUNLSampledTimedSolveNotReady()
    : std::logic_error(
          "positive timed sampled solving is unavailable until deadline-aware traversal is implemented") {
}

std::uint64_t delta_entries_bytes(std::uint64_t entries) noexcept {
    return saturating_multiply(entries, sizeof(HUNLSampledValueDelta));
}

HUNLSampledStructuredRangeNotReady::HUNLSampledStructuredRangeNotReady()
    : std::logic_error(
          "structured range sampled solving is disabled until private-state-aware traversal is available") {
}

HUNLSampledSolver::HUNLSampledSolver(HUNLSampledSolverConfig config)
    : config_(config),
      builder_(HUNLSampledBuilderConfig{config.use_public_chance_isomorphism, effective_public_state_cap(config), 0U}),
      storage_(make_sampled_storage(config)) {
    validate_sampled_config_or_throw(config_);
}

HUNLSampledSolveResult HUNLSampledSolver::solve_for(
    const HUNLSampledSolveRequest& request,
    std::chrono::milliseconds budget) {
    if (budget.count() <= 0) {
        return run_batches(request, 0);
    }
    (void)request;
    throw HUNLSampledTimedSolveNotReady{};
}

HUNLSampledSolveResult HUNLSampledSolver::run_batches(
    const HUNLSampledSolveRequest& request,
    std::uint32_t batches) {
    // Build an independent session and publish it only after every phase has
    // completed. A rejected preflight, preparation failure, or worker failure
    // therefore leaves this solver's last clean root export intact.
    HUNLSampledSolver staged(config_);
    const auto result = staged.run_batches_impl(request, batches);
    config_ = std::move(staged.config_);
    builder_ = std::move(staged.builder_);
    storage_ = std::move(staged.storage_);
    profile_ = std::move(staged.profile_);
    root_strategy_ = std::move(staged.root_strategy_);
    return result;
}

HUNLSampledSolveResult HUNLSampledSolver::run_batches_impl(
    const HUNLSampledSolveRequest& request,
    std::uint32_t batches) {
    if (request.root_state.has_value() && request.structured_root.has_value()) {
        throw std::invalid_argument(
            "sampled solve request must choose either an explicit root or a structured range root");
    }
    if (batches > 0U && request.structured_root.has_value()) {
        request.structured_root->validate();
        throw HUNLSampledStructuredRangeNotReady{};
    }
    HUNLSampledSolveRequest effective_request = request;
    if (request.structured_root.has_value()) {
        request.structured_root->validate();
        auto config = std::make_shared<const HUNLConfig>(request.structured_root->config);
        effective_request.root_state = HUNLState::initial(config);
    }
    if (batches > 0 && !effective_request.root_state.has_value()) {
        throw HUNLSampledSolverNotReady{};
    }

    // run_batches starts an independent solve. A future resumable API must
    // retain a validated session identity and a monotonic work cursor.
    builder_.clear();
    storage_.clear_keep_capacity();
    profile_.reset();
    root_strategy_ = {};

    auto preflight_result = preflight(effective_request);
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
    builder_.set_max_cached_public_states(effective_public_state_cap(config_));
    std::uint64_t mutable_growth_limit = 0U;
    if (config_.enable_memory_guardrails) {
        const auto reserved = saturating_add(
            saturating_add(estimate_worker_delta_bytes(effective_request, config_),
                           estimate_terminal_cache_bytes(effective_request, config_)),
            estimate_export_bytes(effective_request));
        if (reserved >= config_.memory_fail_bytes) {
            throw std::runtime_error("sampled memory reservation leaves no safe builder/storage growth budget");
        }
        // Builder and sparse storage are the two independently growing live
        // domains. Splitting the residual makes their independent admissions a
        // global hard bound rather than two unrelated full-limit checks.
        mutable_growth_limit = (config_.memory_fail_bytes - reserved) / 2U;
    }
    builder_.set_memory_limit_bytes(mutable_growth_limit);
    storage_.set_memory_limit_bytes(mutable_growth_limit);
    if (effective_request.root_state.has_value()) {
        const auto initialized_root_id = builder_.initialize(*effective_request.root_state);
        if (initialized_root_id != builder_.root_id()) {
            throw std::runtime_error("sampled solver initialized an inconsistent root node");
        }
        if (builder_.node_count() > effective_public_state_cap(config_)) {
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
    profile_.record_observed_memory(live_memory.total_bytes(), live_memory.total_bytes());
    root_strategy_ = HUNLSampledStrategyExporter::export_uniform(infer_root_action_count(effective_request));

    if (batches > 0) {
        HUNLSampledTraversal traversal(builder_, storage_, terminal_evaluator_);
        const auto root_id = builder_.root_id();
        const auto bucket_count = static_cast<std::uint32_t>(std::max<std::uint64_t>(1U, infer_bucket_count(effective_request, config_)));
        for (std::uint32_t batch = 0; batch < batches; ++batch) {
            const auto traverse_started = std::chrono::steady_clock::now();
            std::vector<HUNLSampledTraversalRequest> trajectory_requests;
            trajectory_requests.reserve(config_.minibatch_size);
            for (std::uint32_t trajectory = 0; trajectory < config_.minibatch_size; ++trajectory) {
                trajectory_requests.push_back({
                    static_cast<PlayerId>((static_cast<std::uint64_t>(batch) * config_.minibatch_size + trajectory) & 1U), config_.seed,
                    static_cast<std::uint64_t>(batch) * config_.minibatch_size + trajectory,
                    batch + 1U, root_id, 0U, bucket_count, 4096U});
                prepare_hunl_sampled_trajectory(
                    builder_, storage_, terminal_evaluator_, trajectory_requests.back());
            }
            const auto worker_count = std::min<std::size_t>(config_.workers, trajectory_requests.size());
            const auto batch_peak = saturating_add(
                memory_estimate().total_bytes(), estimate_worker_delta_bytes(effective_request, config_));
            profile_.record_observed_memory(memory_estimate().total_bytes(), batch_peak);
            const auto worker_batches = HUNLSampledScheduler::partition_deterministic(
                trajectory_requests.size(), std::max<std::size_t>(1U, worker_count));
            std::vector<HUNLSampledWorkerScratch> worker_scratch(worker_batches.size());
            std::vector<HUNLSampledTraversalResult> worker_results(worker_batches.size());
            std::vector<std::thread> threads;
            std::exception_ptr worker_error;
            std::mutex worker_error_mutex;
            threads.reserve(worker_batches.size() > 0U ? worker_batches.size() - 1U : 0U);
            const auto execute_worker = [&](std::size_t worker_index) {
                try {
                    if (config_.test_throw_worker_index == static_cast<std::int32_t>(worker_index)) {
                        throw std::runtime_error("sampled solver regression worker failure");
                    }
                    auto& aggregate = worker_scratch[worker_index];
                    const auto range = worker_batches[worker_index].trajectories;
                    aggregate.reserve_deltas(static_cast<std::size_t>(range.size()) * kWorkerDeltaEntriesPerTraversal);
                    HUNLSampledWorkerScratch trajectory_scratch;
                    trajectory_scratch.reserve_deltas(kWorkerDeltaEntriesPerTraversal);
                    for (std::uint64_t id = range.begin; id < range.end; ++id) {
                        const auto result = traversal.run_unmerged(
                            trajectory_requests[static_cast<std::size_t>(id)], trajectory_scratch);
                        aggregate.deltas.insert(
                            aggregate.deltas.end(), trajectory_scratch.deltas.begin(), trajectory_scratch.deltas.end());
                        worker_results[worker_index].nodes_visited += result.nodes_visited;
                        worker_results[worker_index].infosets_updated += result.infosets_updated;
                    }
                } catch (...) {
                    std::lock_guard<std::mutex> lock(worker_error_mutex);
                    if (worker_error == nullptr) worker_error = std::current_exception();
                }
            };
            for (std::size_t worker = 1; worker < worker_batches.size(); ++worker) {
                threads.emplace_back(execute_worker, worker);
            }
            if (!worker_batches.empty()) execute_worker(0);
            for (auto& thread : threads) thread.join();
            if (worker_error != nullptr) std::rethrow_exception(worker_error);
            profile_.add_traverse_seconds(std::chrono::duration<double>(
                std::chrono::steady_clock::now() - traverse_started).count());
            const auto merge_started = std::chrono::steady_clock::now();
            for (std::size_t worker = 0; worker < worker_batches.size(); ++worker) {
                profile_.record_traversal(
                    worker_batches[worker].trajectories.size(),
                    worker_results[worker].nodes_visited,
                    worker_results[worker].infosets_updated);
                // Each worker owns a deterministic contiguous trajectory range.
                // Merging those sorted streams in worker order avoids a second
                // full-minibatch coordinator arena.
                merge_hunl_sampled_worker_deltas(storage_, worker_scratch[worker]);
            }
            profile_.add_merge_seconds(std::chrono::duration<double>(
                std::chrono::steady_clock::now() - merge_started).count());
        }
        const auto export_started = std::chrono::steady_clock::now();
        const auto& root = builder_.node(root_id);
        if (root.type == HUNLFlatNodeType::Decision) {
            root_strategy_ = HUNLSampledStrategyExporter::export_average_strategy(storage_.view(root.infoset_id));
        }
        profile_.add_export_seconds(std::chrono::duration<double>(
            std::chrono::steady_clock::now() - export_started).count());
    }
    if (effective_request.root_state.has_value()) {
        const auto& root = builder_.node(builder_.root_id());
        if (root.type == HUNLFlatNodeType::Decision) {
            std::vector<ActionId> actions;
            std::vector<int> targets;
            const auto menu = effective_request.root_state->legal_actions();
            actions.reserve(menu.size());
            targets.reserve(menu.size());
            for (const auto action : menu) {
                const auto child = effective_request.root_state->next_state(action);
                actions.push_back(action);
                targets.push_back(root.player >= 0
                    ? child.contributions[static_cast<std::size_t>(root.player)] : 0);
            }
            HUNLSampledStrategyExporter::attach_action_descriptors(root_strategy_, actions, targets);
        }
    }

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
    profile_.record_observed_memory(final_memory.total_bytes(), final_memory.total_bytes());

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
    HUNLSampledMemoryEstimate estimate;
    estimate.public_state_cache_bytes = builder_memory.total_bytes();
    estimate.public_states_cached = builder_memory.nodes;
    estimate.public_state_edges = builder_memory.edges;
    estimate.infoset_row_bytes = storage_memory.total_bytes();
    estimate.infoset_rows_allocated = storage_memory.sparse_rows;
    estimate.sparse_values_allocated = storage_memory.sparse_values;
    estimate.terminal_cache_bytes = 0;
    // Worker arenas are transient; live retained memory contains no worker
    // scratch after a batch completes.
    estimate.worker_delta_bytes = 0;
    estimate.export_bytes = saturating_multiply(
        saturating_size(root_strategy_.actions.capacity()), sizeof(HUNLSampledActionProbability));
    estimate.total_bytes_live = saturating_add(
        saturating_add(estimate.public_state_cache_bytes, estimate.infoset_row_bytes),
        saturating_add(
            saturating_add(estimate.terminal_cache_bytes, estimate.worker_delta_bytes),
            estimate.export_bytes));
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

    const auto public_states = static_cast<std::uint64_t>(effective_public_state_cap(config));
    estimate.public_states_cached = public_states;
    estimate.public_state_edges = saturating_multiply(
        public_states, std::max<std::uint64_t>(1U, infer_root_action_count(request)));
    estimate.public_state_cache_bytes = saturating_multiply(public_states, kDefaultPublicStateBytes);

    const auto bucket_count = infer_bucket_count(request, config);
    const auto action_count = std::max<std::uint64_t>(16U, infer_root_action_count(request));
    const auto infoset_rows = std::max<std::uint64_t>(1ULL, public_states);
    estimate.infoset_rows_allocated = infoset_rows;
    estimate.sparse_values_allocated = saturating_multiply(
        saturating_multiply(infoset_rows, bucket_count), action_count);
    const auto meta_bytes = saturating_add(
        saturating_add(sizeof(HUNLSampledInfosetMeta), sizeof(InfosetId)),
        saturating_add(sizeof(std::size_t), saturating_multiply(sizeof(void*), 2ULL)));
    estimate.infoset_row_bytes = saturating_add(
        saturating_multiply(infoset_rows, meta_bytes),
        saturating_multiply(estimate.sparse_values_allocated, sizeof(float) * 2ULL));
    estimate.terminal_cache_bytes = estimate_terminal_cache_bytes(request, config);
    estimate.worker_delta_bytes = estimate_worker_delta_bytes(request, config);
    estimate.export_bytes = estimate_export_bytes(request);
    const auto retained = memory_estimate();
    estimate.public_state_cache_bytes = std::max(estimate.public_state_cache_bytes, retained.public_state_cache_bytes);
    estimate.infoset_row_bytes = std::max(estimate.infoset_row_bytes, retained.infoset_row_bytes);
    estimate.total_bytes_live = saturating_add(
        saturating_add(estimate.public_state_cache_bytes, estimate.infoset_row_bytes),
        saturating_add(
            saturating_add(estimate.terminal_cache_bytes, estimate.worker_delta_bytes),
            estimate.export_bytes));
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

    result.estimate = estimate_memory_for(request, result.effective_config);
    while (config.adaptive_memory_fallback &&
           result.estimate.total_bytes() > result.effective_config.memory_fail_bytes) {
        if (result.adjustments.recorded_step_count == HUNLSampledAdaptiveAdjustments::kMaxRecordedSteps) {
            break;
        }
        const auto before = result.effective_config;
        auto candidate = before;
        auto candidate_adjustments = result.adjustments;
        apply_adaptive_fallback(candidate, candidate_adjustments);
        if (candidate.minibatch_size == before.minibatch_size &&
            candidate.bucket_count_hint == before.bucket_count_hint) {
            break;
        }
        const auto old_estimate = result.estimate.total_bytes();
        const auto new_estimate = estimate_memory_for(request, candidate);
        if (new_estimate.total_bytes() >= old_estimate) {
            break;
        }
        result.effective_config = candidate;
        result.adjustments = candidate_adjustments;
        const auto step = result.adjustments.recorded_step_count++;
        result.adjustments.estimate_before[step] = old_estimate;
        result.adjustments.estimate_after[step] = new_estimate.total_bytes();
        result.estimate = new_estimate;
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
    if (config.bucket_count_hint > 32U) {
        config.bucket_count_hint = std::max<std::uint32_t>(32U, config.bucket_count_hint / 2U);
        adjustments.reduced_bucket_hint = true;
        return;
    }
}

std::uint64_t HUNLSampledSolver::estimate_worker_delta_bytes(
    const HUNLSampledSolveRequest&,
    const HUNLSampledSolverConfig& config) noexcept {
    const auto trajectories = std::max<std::uint64_t>(1U, static_cast<std::uint64_t>(config.minibatch_size));
    const auto workers = std::min(
        std::max<std::uint64_t>(1U, saturating_size(config.workers)), trajectories);
    const auto largest_partition = (trajectories + workers - 1U) / workers;
    const auto aggregate_entries = saturating_multiply(largest_partition, kWorkerDeltaEntriesPerTraversal);
    const auto per_worker = saturating_add(
        delta_entries_bytes(aggregate_entries + kWorkerDeltaEntriesPerTraversal),
        saturating_add(sizeof(HUNLSampledWorkerScratch) * 2ULL, sizeof(std::thread)));
    const auto worker_arenas = saturating_multiply(workers, per_worker);
    return worker_arenas;
}

std::uint64_t HUNLSampledSolver::estimate_export_bytes(const HUNLSampledSolveRequest& request) noexcept {
    return saturating_multiply(
        static_cast<std::uint64_t>(std::max<std::uint8_t>(1U, infer_root_action_count(request))),
        sizeof(HUNLSampledActionProbability));
}

std::uint64_t HUNLSampledSolver::estimate_terminal_cache_bytes(
    const HUNLSampledSolveRequest& request,
    const HUNLSampledSolverConfig& config) noexcept {
    const auto public_states = static_cast<std::uint64_t>(effective_public_state_cap(config));
    const auto action_factor = std::max<std::uint64_t>(1U, infer_root_action_count(request));
    return saturating_multiply(
        saturating_multiply(public_states, action_factor),
        kDefaultTerminalCachePerStateBytes);
}

}  // namespace core
