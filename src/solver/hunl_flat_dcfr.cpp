#include "solver/hunl_flat_dcfr.hpp"

#include "solver/dcfr.hpp"
#include "util/abstraction.hpp"
#include "util/profiling.hpp"
#include "util/simd.hpp"

#include <array>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <exception>
#include <stdexcept>
#include <vector>

namespace core {

namespace {

template <class T, class Allocator>
std::uint64_t vector_storage_bytes(const std::vector<T, Allocator>& values) {
    return static_cast<std::uint64_t>(values.capacity()) * sizeof(T);
}

std::uint64_t estimate_bucket_map_bytes(const HUNLFlatBucketMap& bucket_map) {
    std::uint64_t bytes = sizeof(HUNLFlatBucketMap);
    for (std::uint32_t infoset_index = 0;; ++infoset_index) {
        const auto id = InfosetId{infoset_index};
        try {
            const auto& entry = bucket_map.entry(id);
            bytes += sizeof(HUNLFlatBucketEntry);
            bytes += vector_storage_bytes(entry.board);
            bytes += static_cast<std::uint64_t>(entry.canonical_board.capacity()) * sizeof(char);
            bytes += vector_storage_bytes(entry.dense_bucket_ids);
            bytes += vector_storage_bytes(entry.bucket_hand_counts);
            bytes += vector_storage_bytes(entry.bucket_weights);
        } catch (const std::out_of_range&) {
            break;
        }
    }
    return bytes;
}

std::uint64_t estimate_terminal_table_bytes(const HUNLBucketTerminalTable& terminal_table) {
    (void)terminal_table;
    return sizeof(HUNLBucketTerminalTable);
}

double prior_bucket_weight(
    const HUNLFlatBucketMap* bucket_map,
    const HUNLFlatInfosetTableMeta& infoset_meta,
    InfosetId infoset_id,
    std::size_t bucket_idx) {
    if (bucket_map == nullptr) {
        if (infoset_meta.bucket_count == 0) {
            return 0.0;
        }
        return 1.0 / static_cast<double>(infoset_meta.bucket_count);
    }
    return bucket_map->bucket_weight(infoset_id, bucket_idx);
}

std::string worker_scope_name(const char* base, std::size_t worker_index) {
    return std::string(base) + "[w" + std::to_string(worker_index) + "]";
}

std::size_t max_backward_row_width(const HUNLFlatSolveGraph& graph) {
    std::size_t max_width = 0;
    for (const auto& meta : graph.node_meta) {
        max_width = std::max<std::size_t>(
            max_width,
            std::max<std::size_t>(meta.child_count, meta.chance_count));
    }
    return max_width;
}

std::size_t max_bucket_width(const HUNLFlatInfosetTable& infoset_table) {
    std::size_t max_width = 0;
    for (const auto& meta : infoset_table.meta()) {
        max_width = std::max<std::size_t>(max_width, meta.bucket_count);
    }
    return max_width;
}

void mark_worker_scope(const char* base, std::size_t worker_index, double seconds) {
    if (!profiling::detail_enabled()) {
        return;
    }
    profiling::mark(worker_scope_name(base, worker_index), seconds);
}

void mark_worker_detail_scope(const char* base, std::size_t worker_index, double seconds) {
    if (!profiling::detail_enabled()) {
        return;
    }
    profiling::mark(worker_scope_name(base, worker_index), seconds);
}

void validate_flat_solver_graph(const HUNLFlatSolveGraph& graph) {
    if (graph.nodes.size() != graph.node_meta.size()) {
        throw std::invalid_argument("HUNLFlatDCFR graph nodes/node_meta size mismatch");
    }
    if (graph.root >= graph.nodes.size() && !graph.nodes.empty()) {
        throw std::invalid_argument("HUNLFlatDCFR graph root out of bounds");
    }
    for (std::size_t node_idx = 0; node_idx < graph.node_meta.size(); ++node_idx) {
        const auto& meta = graph.node_meta[node_idx];
        if (meta.child_begin + meta.child_count > graph.children.size()) {
            throw std::invalid_argument("HUNLFlatDCFR graph child range out of bounds");
        }
        if (meta.chance_begin + meta.chance_count > graph.chance_outcomes.size()) {
            throw std::invalid_argument("HUNLFlatDCFR graph chance range out of bounds");
        }
        if (meta.type == HUNLFlatNodeType::Decision) {
            if (!meta.has_infoset) {
                throw std::invalid_argument("HUNLFlatDCFR decision node missing infoset");
            }
            if (meta.infoset_id.value >= graph.infosets.size()) {
                throw std::invalid_argument("HUNLFlatDCFR decision node infoset id out of bounds");
            }
        }
    }
}

}  // namespace

std::optional<HUNLFlatBucketMap> HUNLFlatDCFR::load_bucket_map_for_graph(
    const HUNLFlatSolveGraph& graph,
    HUNLFlatSolveMode solve_mode) {
    if (solve_mode == HUNLFlatSolveMode::ExplicitHand) {
        return std::nullopt;
    }
    if (!graph.config || !graph.config->abstraction_path.has_value()) {
        if (solve_mode == HUNLFlatSolveMode::Bucketed) {
            throw std::invalid_argument("bucketed flat solve mode requires abstraction_path");
        }
        return std::nullopt;
    }
    auto bucket_map = HUNLFlatBucketMap::from_abstraction(
        graph,
        load_abstraction(*graph.config->abstraction_path));
    bucket_map.apply_range_inputs(graph, graph.config->player_ranges);
    return bucket_map;
}

std::optional<HUNLBucketTerminalTable> HUNLFlatDCFR::build_terminal_table_for_graph(
    const HUNLFlatSolveGraph& graph,
    const std::optional<HUNLFlatBucketMap>& bucket_map) {
    if (!bucket_map.has_value()) {
        return std::nullopt;
    }
    return HUNLBucketTerminalTable::build(graph, *bucket_map);
}

HUNLFlatDCFR::WorkerPool::WorkerPool(HUNLFlatDCFR& owner, std::size_t worker_count)
    : owner_(owner) {
    if (worker_count == 0) {
        throw std::invalid_argument("HUNLFlatDCFR worker_count must be at least 1");
    }
    threads_.reserve(worker_count);
    for (std::size_t worker_index = 0; worker_index < worker_count; ++worker_index) {
        threads_.emplace_back([this, worker_index] { worker_loop(worker_index); });
    }
}

HUNLFlatDCFR::WorkerPool::~WorkerPool() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        stop_ = true;
    }
    cv_.notify_all();
    for (auto& thread : threads_) {
        if (thread.joinable()) {
            thread.join();
        }
    }
}

void HUNLFlatDCFR::WorkerPool::run_stage(
    StageCommand command,
    HUNLFlatStageKind stage_kind,
    std::size_t depth) noexcept(false) {
    if (threads_.empty()) {
        return;
    }

    {
        std::lock_guard<std::mutex> lock(mutex_);
        command_ = command;
        stage_kind_ = stage_kind;
        depth_ = depth;
        stage_error_ = nullptr;
        completed_workers_ = 0;
        ++generation_;
    }
    cv_.notify_all();

    std::unique_lock<std::mutex> lock(mutex_);
    finished_cv_.wait(lock, [this] { return completed_workers_ == threads_.size(); });
    if (stage_error_) {
        std::rethrow_exception(stage_error_);
    }
}

std::size_t HUNLFlatDCFR::WorkerPool::worker_count() const noexcept {
    return threads_.size();
}

void HUNLFlatDCFR::WorkerPool::worker_loop(std::size_t worker_index) {
    std::size_t seen_generation = 0;
    for (;;) {
        StageCommand command = StageCommand::Discount;
        HUNLFlatStageKind stage_kind = HUNLFlatStageKind::Discount;
        std::size_t depth = 0;
        {
            std::unique_lock<std::mutex> lock(mutex_);
            cv_.wait(lock, [this, seen_generation] {
                return stop_ || generation_ != seen_generation;
            });
            if (stop_) {
                return;
            }
            seen_generation = generation_;
            command = command_;
            stage_kind = stage_kind_;
            depth = depth_;
        }

        try {
            const auto start = std::chrono::steady_clock::now();
            owner_.execute_stage_command(command, worker_index, depth);
            const auto finish = std::chrono::steady_clock::now();
            const auto seconds = std::chrono::duration<double>(finish - start).count();
            mark_worker_scope("hunl_flat.worker_stage", worker_index, seconds);
            add_stage_seconds(owner_.scheduler_diagnostics_.worker_profiles[worker_index], stage_kind, seconds);
        } catch (...) {
            std::lock_guard<std::mutex> lock(mutex_);
            if (!stage_error_) {
                stage_error_ = std::current_exception();
            }
        }

        {
            std::lock_guard<std::mutex> lock(mutex_);
            ++completed_workers_;
            if (completed_workers_ == threads_.size()) {
                finished_cv_.notify_one();
            }
        }
    }
}

HUNLFlatDCFR::HUNLFlatDCFR(
    HUNLFlatSolveGraph graph,
    std::array<std::size_t, 2> bucket_count_per_player,
    HUNLFlatValueLayout layout,
    std::size_t workers,
    double alpha,
    double beta,
    double gamma)
    : HUNLFlatDCFR(
          std::move(graph),
          bucket_count_per_player,
          HUNLFlatSolveMode::Auto,
          layout,
          workers,
          alpha,
          beta,
          gamma) {}

HUNLFlatDCFR::HUNLFlatDCFR(
    HUNLFlatSolveGraph graph,
    std::array<std::size_t, 2> bucket_count_per_player,
    HUNLFlatSolveMode solve_mode,
    HUNLFlatValueLayout layout,
    std::size_t workers,
    double alpha,
    double beta,
    double gamma)
    : graph_(std::move(graph)),
      bucket_map_(load_bucket_map_for_graph(graph_, solve_mode)),
      terminal_table_(build_terminal_table_for_graph(graph_, bucket_map_)),
      infoset_table_(HUNLFlatInfosetTable::build(
          graph_,
          bucket_count_per_player,
          bucket_map_ ? &*bucket_map_ : nullptr,
          layout)),
      player0_reach_(graph_.nodes.size(), 0.0),
      player1_reach_(graph_.nodes.size(), 0.0),
      chance_reach_(graph_.nodes.size(), 0.0),
      bucket_reach_(infoset_table_.total_bucket_count(), 0.0),
      normalized_bucket_reach_(infoset_table_.total_bucket_count(), 0.0),
      infoset_bucket_totals_(graph_.infosets.size(), 0.0),
      terminal_values_(graph_.nodes.size(), 0.0),
      node_values_(graph_.nodes.size(), 0.0),
      action_values_(graph_.children.size(), 0.0),
      worker_count_(std::max<std::size_t>(1, workers)),
      parallel_plan_(HUNLFlatParallelPlan::build(
          graph_,
          infoset_table_,
          std::max<std::size_t>(1, workers))),
      pipeline_(HUNLFlatPipeline(HUNLFlatPipelinePlan::build(graph_, infoset_table_, parallel_plan_))),
      worker_scratch_(std::max<std::size_t>(1, workers)),
      alpha_(alpha),
      beta_(beta),
      gamma_(gamma) {
    const auto max_child_count = max_backward_row_width(graph_);
    const auto max_bucket_count = max_bucket_width(infoset_table_);
    validate_flat_solver_graph(graph_);
    validate_alpha(alpha_);
    if (beta_ < 0.0 || gamma_ < 0.0) {
        throw std::invalid_argument("HUNLFlatDCFR beta and gamma must be non-negative");
    }
    for (auto& scratch : worker_scratch_) {
        scratch.ensure_capacity(
            graph_.nodes.size(),
            graph_.children.size(),
            infoset_table_.total_bucket_count(),
            max_child_count,
            max_bucket_count);
    }
    scheduler_diagnostics_.worker_profiles.assign(worker_count_, HUNLFlatStageProfile{});
    worker_pool_ = std::make_unique<WorkerPool>(*this, worker_count_);
}

HUNLFlatDCFR::~HUNLFlatDCFR() = default;

std::size_t HUNLFlatDCFR::worker_count() const noexcept {
    return worker_count_;
}

const HUNLFlatSchedulerDiagnostics& HUNLFlatDCFR::scheduler_diagnostics() const noexcept {
    return scheduler_diagnostics_;
}

const HUNLFlatBucketMap* HUNLFlatDCFR::bucket_map() const noexcept {
    return bucket_map_ ? &*bucket_map_ : nullptr;
}

const HUNLFlatPipelinePlan& HUNLFlatDCFR::pipeline_plan() const noexcept {
    return pipeline_.plan();
}

HUNLFlatMemoryEstimate HUNLFlatDCFR::memory_estimate() const {
    HUNLFlatMemoryEstimateOptions options;
    options.max_child_count = max_backward_row_width(graph_);
    options.max_bucket_count = max_bucket_width(infoset_table_);
    if (bucket_map_) {
        options.auxiliary_bytes += estimate_bucket_map_bytes(*bucket_map_);
    }
    if (terminal_table_) {
        options.auxiliary_bytes += estimate_terminal_table_bytes(*terminal_table_);
    }
    return estimate_memory(graph_, infoset_table_, worker_count_, options);
}

void HUNLFlatDCFR::add_stage_seconds(
    HUNLFlatStageProfile& profile,
    HUNLFlatStageKind stage,
    double seconds) {
    switch (stage) {
        case HUNLFlatStageKind::Discount:
            profile.discount_seconds += seconds;
            break;
        case HUNLFlatStageKind::Strategy:
            profile.strategy_seconds += seconds;
            break;
        case HUNLFlatStageKind::Reach:
            profile.reach_seconds += seconds;
            break;
        case HUNLFlatStageKind::Terminal:
            profile.terminal_seconds += seconds;
            break;
        case HUNLFlatStageKind::Backward:
            profile.backward_seconds += seconds;
            break;
        case HUNLFlatStageKind::Regret:
            profile.regret_seconds += seconds;
            break;
        case HUNLFlatStageKind::AverageStrategy:
            profile.average_strategy_seconds += seconds;
            break;
    }
}

void HUNLFlatDCFR::run_stage_workers(HUNLFlatStageKind stage, StageCommand command, std::size_t depth) {
    if (!worker_pool_ || worker_count_ <= 1) {
        const auto start = std::chrono::steady_clock::now();
        execute_stage_command(command, 0, depth);
        const auto finish = std::chrono::steady_clock::now();
        add_stage_seconds(
            scheduler_diagnostics_.worker_profiles[0],
            stage,
            std::chrono::duration<double>(finish - start).count());
        return;
    }
    worker_pool_->run_stage(command, stage, depth);
}

void HUNLFlatDCFR::execute_stage_command(StageCommand command, std::size_t worker_index, std::size_t depth) {
    switch (command) {
        case StageCommand::Discount:
            worker_discount_stage(worker_index);
            return;
        case StageCommand::Strategy:
            worker_strategy_stage(worker_index);
            return;
        case StageCommand::ReachSeed:
            worker_reach_seed_depth(worker_index, depth);
            return;
        case StageCommand::ReachReduceNodes:
            worker_reach_reduce_nodes_depth(worker_index, depth);
            return;
        case StageCommand::ReachReduceBuckets:
            worker_reach_reduce_buckets_depth(worker_index);
            return;
        case StageCommand::NormalizeBucketReach:
            worker_normalize_bucket_reach_stage(worker_index);
            return;
        case StageCommand::ShowdownEquity:
            worker_showdown_equity_stage(worker_index);
            return;
        case StageCommand::DepthLimitedEval:
            worker_depth_limited_eval_stage(worker_index);
            return;
        case StageCommand::BackwardDepth:
            worker_backward_depth(worker_index, depth);
            return;
        case StageCommand::Regret:
            worker_regret_stage(worker_index);
            return;
        case StageCommand::AverageStrategy:
            worker_average_strategy_stage(worker_index);
            return;
    }
}

void HUNLFlatDCFR::run_iteration() {
    pipeline_.run_iteration(*this);
}

void HUNLFlatDCFR::run_iterations(std::uint32_t iterations) {
    const auto start = std::chrono::steady_clock::now();
    for (std::uint32_t i = 0; i < iterations; ++i) {
        run_iteration();
    }
    profiling::mark(
        "hunl_flat.solve.total",
        std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count());
}

const HUNLFlatSolveGraph& HUNLFlatDCFR::graph() const noexcept {
    return graph_;
}

const HUNLFlatInfosetTable& HUNLFlatDCFR::infoset_table() const noexcept {
    return infoset_table_;
}

HUNLFlatInfosetTable& HUNLFlatDCFR::infoset_table_mut() noexcept {
    return infoset_table_;
}

const HUNLFlatStageProfile& HUNLFlatDCFR::profile() const noexcept {
    return profile_;
}

std::uint32_t HUNLFlatDCFR::iterations() const noexcept {
    return iterations_;
}

const HUNLAlignedVector<double>& HUNLFlatDCFR::player0_reach() const noexcept {
    return player0_reach_;
}

const HUNLAlignedVector<double>& HUNLFlatDCFR::player1_reach() const noexcept {
    return player1_reach_;
}

const HUNLAlignedVector<double>& HUNLFlatDCFR::chance_reach() const noexcept {
    return chance_reach_;
}

const HUNLAlignedVector<double>& HUNLFlatDCFR::bucket_reach() const noexcept {
    return bucket_reach_;
}

const HUNLAlignedVector<double>& HUNLFlatDCFR::normalized_bucket_reach() const noexcept {
    return normalized_bucket_reach_;
}

const HUNLAlignedVector<double>& HUNLFlatDCFR::infoset_bucket_totals() const noexcept {
    return infoset_bucket_totals_;
}

const HUNLAlignedVector<double>& HUNLFlatDCFR::terminal_values() const noexcept {
    return terminal_values_;
}

const HUNLAlignedVector<double>& HUNLFlatDCFR::node_values() const noexcept {
    return node_values_;
}

const HUNLAlignedVector<double>& HUNLFlatDCFR::action_values() const noexcept {
    return action_values_;
}

std::unordered_map<std::string, std::vector<double>> HUNLFlatDCFR::export_average_strategy() const {
    std::unordered_map<std::string, std::vector<double>> out;
    out.reserve(graph_.infosets.size());

    for (const auto& infoset : graph_.infosets) {
        const auto& meta = infoset_table_.meta().at(infoset.id.value);
        const auto* strategy_sum = infoset_table_.strategy_sum(infoset.id);
        std::vector<double> average(meta.value_count, 0.0);

        if (infoset_table_.layout() == HUNLFlatValueLayout::InfosetActionHand) {
            for (std::size_t bucket = 0; bucket < meta.bucket_count; ++bucket) {
                const auto bucket_offset = bucket * static_cast<std::size_t>(meta.action_count);
                normalize_row(strategy_sum + bucket_offset, average.data() + bucket_offset, meta.action_count);
            }
        } else {
            for (std::size_t bucket = 0; bucket < meta.bucket_count; ++bucket) {
                const auto bucket_offset = bucket * static_cast<std::size_t>(meta.action_count);
                normalize_row(strategy_sum + bucket_offset, average.data() + bucket_offset, meta.action_count);
            }
        }

        out.emplace(infoset.key, std::move(average));
    }

    return out;
}

HUNLFlatAverageStrategyTable HUNLFlatDCFR::export_average_strategy_table() const {
    HUNLFlatAverageStrategyTable out;
    out.layout = infoset_table_.layout();
    out.meta = infoset_table_.meta();
    out.values.assign(infoset_table_.total_value_count(), 0.0);

    for (const auto& infoset : graph_.infosets) {
        const auto& meta = out.meta.at(infoset.id.value);
        const auto* strategy_sum = infoset_table_.strategy_sum(infoset.id);
        auto* average = out.values.data() + meta.offset;

        if (infoset_table_.layout() == HUNLFlatValueLayout::InfosetActionHand) {
            for (std::size_t bucket = 0; bucket < meta.bucket_count; ++bucket) {
                double total = 0.0;
                for (std::size_t action = 0; action < meta.action_count; ++action) {
                    total += strategy_sum[action * static_cast<std::size_t>(meta.bucket_count) + bucket];
                }
                if (total > 0.0) {
                    for (std::size_t action = 0; action < meta.action_count; ++action) {
                        average[action * static_cast<std::size_t>(meta.bucket_count) + bucket] =
                            strategy_sum[action * static_cast<std::size_t>(meta.bucket_count) + bucket] / total;
                    }
                    continue;
                }
                const auto uniform = 1.0 / static_cast<double>(meta.action_count);
                for (std::size_t action = 0; action < meta.action_count; ++action) {
                    average[action * static_cast<std::size_t>(meta.bucket_count) + bucket] = uniform;
                }
            }
            continue;
        }

        for (std::size_t bucket = 0; bucket < meta.bucket_count; ++bucket) {
            const auto bucket_offset = bucket * static_cast<std::size_t>(meta.action_count);
            normalize_row(
                strategy_sum + bucket_offset,
                average + bucket_offset,
                meta.action_count);
        }
    }

    return out;
}

void HUNLFlatDCFR::apply_dcfr_discount_stage() {
    run_stage_workers(HUNLFlatStageKind::Discount, StageCommand::Discount);
}

void HUNLFlatDCFR::worker_discount_stage(std::size_t worker_index) {
    const auto target_iter = iterations_ + 1U;
    auto& metas = infoset_table_.meta_mut();
    const auto range = parallel_plan_.workers[worker_index].infoset_range;
    const auto worker_start = std::chrono::steady_clock::now();
    for (std::uint32_t infoset_index = range.begin; infoset_index < range.end; ++infoset_index) {
        auto& meta = metas[infoset_index];
        if (meta.last_discount_iter >= target_iter) {
            continue;
        }

        auto* regret = infoset_table_.regret_mut(meta.id);
        auto* strategy_sum = infoset_table_.strategy_sum_mut(meta.id);
        for (std::uint32_t tt = meta.last_discount_iter + 1U; tt <= target_iter; ++tt) {
            const auto t = static_cast<double>(tt);
            const auto ta = std::pow(t, alpha_);
            const auto tb = std::pow(t, beta_);
            const auto pos_scale = ta / (ta + 1.0);
            const auto neg_scale = tb / (tb + 1.0);
            const auto strat_scale = std::pow(t / (t + 1.0), gamma_);
            discount_regrets(regret, meta.value_count, pos_scale, neg_scale);
            discount_strategy_sum(strategy_sum, meta.value_count, strat_scale);
        }
        meta.last_discount_iter = target_iter;
    }
    mark_worker_scope(
        "hunl_flat.discount",
        worker_index,
        std::chrono::duration<double>(std::chrono::steady_clock::now() - worker_start).count());
}

void HUNLFlatDCFR::compute_strategy_stage() {
    run_stage_workers(HUNLFlatStageKind::Strategy, StageCommand::Strategy);
}

void HUNLFlatDCFR::worker_strategy_stage(std::size_t worker_index) {
    const auto& metas = infoset_table_.meta();
    const auto range = parallel_plan_.workers[worker_index].infoset_range;
    const auto worker_start = std::chrono::steady_clock::now();
    for (std::uint32_t infoset_index = range.begin; infoset_index < range.end; ++infoset_index) {
        const auto& meta = metas[infoset_index];
        if (meta.value_count == 0 || meta.bucket_count == 0 || meta.action_count == 0) {
            throw std::logic_error("HUNLFlatDCFR strategy stage requires non-empty infoset rows");
        }
        auto* regret = infoset_table_.regret_mut(meta.id);
        auto* strategy = infoset_table_.current_strategy_mut(meta.id);

        if (infoset_table_.layout() == HUNLFlatValueLayout::InfosetHandAction) {
            for (std::size_t bucket = 0; bucket < meta.bucket_count; ++bucket) {
                const auto bucket_offset = bucket * static_cast<std::size_t>(meta.action_count);
                compute_strategy_row_small(
                    regret + bucket_offset,
                    strategy + bucket_offset,
                    meta.action_count);
            }
            continue;
        }

        for (std::size_t bucket = 0; bucket < meta.bucket_count; ++bucket) {
            double positive_total = 0.0;
            for (std::size_t a = 0; a < meta.action_count; ++a) {
                const auto idx = a * static_cast<std::size_t>(meta.bucket_count) + bucket;
                strategy[idx] = std::max(regret[idx], 0.0);
                positive_total += strategy[idx];
            }
            if (positive_total > 0.0) {
                for (std::size_t a = 0; a < meta.action_count; ++a) {
                    const auto idx = a * static_cast<std::size_t>(meta.bucket_count) + bucket;
                    strategy[idx] /= positive_total;
                }
            } else {
                const double uniform = 1.0 / static_cast<double>(meta.action_count);
                for (std::size_t a = 0; a < meta.action_count; ++a) {
                    strategy[a * static_cast<std::size_t>(meta.bucket_count) + bucket] = uniform;
                }
            }
        }
    }
    mark_worker_scope(
        "hunl_flat.strategy",
        worker_index,
        std::chrono::duration<double>(std::chrono::steady_clock::now() - worker_start).count());
}

void HUNLFlatDCFR::forward_reach_stage() {
    std::fill(player0_reach_.begin(), player0_reach_.end(), 0.0);
    std::fill(player1_reach_.begin(), player1_reach_.end(), 0.0);
    std::fill(chance_reach_.begin(), chance_reach_.end(), 0.0);
    std::fill(bucket_reach_.begin(), bucket_reach_.end(), 0.0);
    if (!graph_.nodes.empty()) {
        player0_reach_[graph_.root] = 1.0;
        player1_reach_[graph_.root] = 1.0;
        chance_reach_[graph_.root] = 1.0;
    }

    for (std::size_t depth = 0; depth < graph_.depth_slices.size(); ++depth) {
        run_stage_workers(HUNLFlatStageKind::Reach, StageCommand::ReachSeed, depth);

        if (depth + 1 < graph_.depth_slices.size()) {
            run_stage_workers(HUNLFlatStageKind::Reach, StageCommand::ReachReduceNodes, depth);

            for (auto& scratch : worker_scratch_) {
                for (const auto node_idx : scratch.dirty_nodes) {
                    scratch.player0_reach[node_idx] = 0.0;
                    scratch.player1_reach[node_idx] = 0.0;
                    scratch.chance_reach[node_idx] = 0.0;
                }
                scratch.dirty_nodes.clear();
            }
        }

        run_stage_workers(HUNLFlatStageKind::Reach, StageCommand::ReachReduceBuckets, depth);

        for (auto& scratch : worker_scratch_) {
            for (const auto bucket_idx : scratch.dirty_buckets) {
                scratch.bucket_reach[bucket_idx] = 0.0;
            }
            scratch.dirty_buckets.clear();
        }
    }
}

void HUNLFlatDCFR::worker_reach_seed_depth(std::size_t worker_index, std::size_t depth) {
    auto& scratch = worker_scratch_[worker_index];
    const auto worker_start = std::chrono::steady_clock::now();

    const auto& worker = parallel_plan_.workers[worker_index];
    const auto range = worker.depth_node_ranges[depth];
    const auto mark_dirty_node = [&](std::uint32_t node_idx) {
        if (scratch.player0_reach[node_idx] == 0.0 &&
            scratch.player1_reach[node_idx] == 0.0 &&
            scratch.chance_reach[node_idx] == 0.0) {
            scratch.dirty_nodes.push_back(node_idx);
        }
    };
    const auto mark_dirty_bucket = [&](std::uint32_t bucket_idx) {
        if (scratch.bucket_reach[bucket_idx] == 0.0) {
            scratch.dirty_buckets.push_back(bucket_idx);
        }
    };
    for (std::uint32_t order_idx = range.begin; order_idx < range.end; ++order_idx) {
        const auto node_idx = graph_.depth_order[order_idx];
        const auto& meta = graph_.node_meta[node_idx];
        const auto reach0 = player0_reach_[node_idx];
        const auto reach1 = player1_reach_[node_idx];
        const auto chance = chance_reach_[node_idx];
        if ((reach0 == 0.0 && reach1 == 0.0) || chance == 0.0 || meta.child_count == 0) {
            continue;
        }

        if (meta.type == HUNLFlatNodeType::Chance) {
            for (std::size_t i = 0; i < meta.chance_count; ++i) {
                const auto& outcome = graph_.chance_outcomes[meta.chance_begin + i];
                mark_dirty_node(outcome.child);
                scratch.player0_reach[outcome.child] += reach0;
                scratch.player1_reach[outcome.child] += reach1;
                scratch.chance_reach[outcome.child] += chance * outcome.probability;
            }
            continue;
        }

        if (meta.type != HUNLFlatNodeType::Decision || !meta.has_infoset) {
            continue;
        }

        const auto& infoset_meta = infoset_table_.meta().at(meta.infoset_id.value);
        const auto bucket_range = infoset_table_.infoset_bucket_range(meta.infoset_id);
        const auto* strategy = infoset_table_.current_strategy(meta.infoset_id);
        if (bucket_range.end > scratch.bucket_reach.size()) {
            throw std::logic_error("HUNLFlatDCFR reach stage bucket range out of scratch bounds");
        }
        if (infoset_meta.bucket_count > scratch.local_bucket_mass.size()) {
            throw std::logic_error("HUNLFlatDCFR reach stage local bucket mass out of scratch bounds");
        }
        const auto acting_reach = meta.player == 0 ? reach0 : reach1;
        if (acting_reach == 0.0) {
            continue;
        }

        for (std::size_t bucket = 0; bucket < infoset_meta.bucket_count; ++bucket) {
            const auto local_mass =
                acting_reach * prior_bucket_weight(bucket_map(), infoset_meta, meta.infoset_id, bucket);
            scratch.local_bucket_mass[bucket] = local_mass;
            mark_dirty_bucket(bucket_range.begin + static_cast<std::uint32_t>(bucket));
            scratch.bucket_reach[bucket_range.begin + bucket] += local_mass;
        }

        for (std::size_t i = 0; i < meta.child_count; ++i) {
            const auto child = graph_.children[meta.child_begin + i];
            double bucketed_action_mass = 0.0;
            for (std::size_t bucket = 0; bucket < infoset_meta.bucket_count; ++bucket) {
                const auto mass = scratch.local_bucket_mass[bucket];
                if (mass == 0.0) {
                    continue;
                }

                double action_prob = 1.0 / static_cast<double>(meta.child_count);
                if (infoset_table_.layout() == HUNLFlatValueLayout::InfosetActionHand) {
                    action_prob =
                        strategy[i * static_cast<std::size_t>(infoset_meta.bucket_count) + bucket];
                } else {
                    action_prob =
                        strategy[bucket * static_cast<std::size_t>(infoset_meta.action_count) + i];
                }
                bucketed_action_mass += mass * action_prob;
            }

            mark_dirty_node(child);
            if (meta.player == 0) {
                scratch.player0_reach[child] += bucketed_action_mass;
                scratch.player1_reach[child] += reach1;
            } else {
                scratch.player0_reach[child] += reach0;
                scratch.player1_reach[child] += bucketed_action_mass;
            }
            scratch.chance_reach[child] += chance;
        }
    }
    mark_worker_scope(
        "hunl_flat.reach.seed",
        worker_index,
        std::chrono::duration<double>(std::chrono::steady_clock::now() - worker_start).count());
}

void HUNLFlatDCFR::worker_reach_reduce_nodes_depth(std::size_t worker_index, std::size_t depth) {
    const auto range = parallel_plan_.workers[worker_index].depth_reduce_ranges[depth];
    const auto worker_start = std::chrono::steady_clock::now();
    for (std::uint32_t order_idx = range.begin; order_idx < range.end; ++order_idx) {
        const auto node_idx = graph_.depth_order[order_idx];
        double add0 = 0.0;
        double add1 = 0.0;
        double addc = 0.0;
        for (const auto& scratch : worker_scratch_) {
            add0 += scratch.player0_reach[node_idx];
            add1 += scratch.player1_reach[node_idx];
            addc += scratch.chance_reach[node_idx];
        }
        player0_reach_[node_idx] += add0;
        player1_reach_[node_idx] += add1;
        chance_reach_[node_idx] += addc;
    }
    mark_worker_scope(
        "hunl_flat.reach.reduce_nodes",
        worker_index,
        std::chrono::duration<double>(std::chrono::steady_clock::now() - worker_start).count());
}

void HUNLFlatDCFR::worker_reach_reduce_buckets_depth(std::size_t worker_index) {
    const auto range = parallel_plan_.workers[worker_index].bucket_range;
    const auto worker_start = std::chrono::steady_clock::now();
    for (std::uint32_t bucket_offset = range.begin; bucket_offset < range.end; ++bucket_offset) {
        double add_bucket = 0.0;
        for (const auto& scratch : worker_scratch_) {
            add_bucket += scratch.bucket_reach[bucket_offset];
        }
        bucket_reach_[bucket_offset] += add_bucket;
    }
    mark_worker_scope(
        "hunl_flat.reach.reduce_buckets",
        worker_index,
        std::chrono::duration<double>(std::chrono::steady_clock::now() - worker_start).count());
}

void HUNLFlatDCFR::showdown_equity_stage() {
    run_stage_workers(HUNLFlatStageKind::Terminal, StageCommand::ShowdownEquity);
}

void HUNLFlatDCFR::depth_limited_eval_stage() {
    run_stage_workers(HUNLFlatStageKind::Terminal, StageCommand::DepthLimitedEval);
}

void HUNLFlatDCFR::terminal_utility_stage() {
    showdown_equity_stage();
    depth_limited_eval_stage();
}

void HUNLFlatDCFR::worker_showdown_equity_stage(std::size_t worker_index) {
    const auto range = parallel_plan_.workers[worker_index].node_range;
    const auto worker_start = std::chrono::steady_clock::now();
    for (std::uint32_t node_idx = range.begin; node_idx < range.end; ++node_idx) {
        const auto& meta = graph_.node_meta[node_idx];
        if (meta.type == HUNLFlatNodeType::TerminalFold) {
            terminal_values_[node_idx] = graph_.nodes[node_idx].terminal_utility[0];
            continue;
        }
        if (meta.type == HUNLFlatNodeType::TerminalShowdown) {
            if (terminal_table_ && terminal_table_->has_showdown_matrix(node_idx)) {
                terminal_values_[node_idx] = terminal_table_->expected_showdown_value(node_idx);
            } else {
                terminal_values_[node_idx] = graph_.nodes[node_idx].terminal_utility[0];
            }
            continue;
        }
    }
    mark_worker_scope(
        "hunl_flat.showdown_equity",
        worker_index,
        std::chrono::duration<double>(std::chrono::steady_clock::now() - worker_start).count());
}

void HUNLFlatDCFR::worker_depth_limited_eval_stage(std::size_t worker_index) {
    const auto range = parallel_plan_.workers[worker_index].node_range;
    const auto worker_start = std::chrono::steady_clock::now();
    for (std::uint32_t node_idx = range.begin; node_idx < range.end; ++node_idx) {
        const auto& meta = graph_.node_meta[node_idx];
        if (meta.type != HUNLFlatNodeType::DepthLimited) {
            continue;
        }
        terminal_values_[node_idx] = heuristic_depth_limited_value_p0(meta, *graph_.config);
    }
    mark_worker_scope(
        "hunl_flat.depth_limited_eval",
        worker_index,
        std::chrono::duration<double>(std::chrono::steady_clock::now() - worker_start).count());
}

void HUNLFlatDCFR::normalize_bucket_reach_stage() {
    run_stage_workers(HUNLFlatStageKind::Reach, StageCommand::NormalizeBucketReach);
}

void HUNLFlatDCFR::worker_normalize_bucket_reach_stage(std::size_t worker_index) {
    const auto& metas = infoset_table_.meta();
    const auto range = parallel_plan_.workers[worker_index].infoset_range;
    const auto worker_start = std::chrono::steady_clock::now();
    for (std::uint32_t infoset_index = range.begin; infoset_index < range.end; ++infoset_index) {
        const auto& meta = metas[infoset_index];
        const auto bucket_range = infoset_table_.infoset_bucket_range(meta.id);
        double total = 0.0;
        for (std::uint32_t bucket_offset = bucket_range.begin; bucket_offset < bucket_range.end; ++bucket_offset) {
            total += bucket_reach_[bucket_offset];
        }
        infoset_bucket_totals_[meta.id.value] = total;

        if (total > 0.0) {
            for (std::size_t bucket = 0; bucket < meta.bucket_count; ++bucket) {
                const auto bucket_offset = bucket_range.begin + static_cast<std::uint32_t>(bucket);
                normalized_bucket_reach_[bucket_offset] = bucket_reach_[bucket_offset] / total;
            }
            continue;
        }

        for (std::size_t bucket = 0; bucket < meta.bucket_count; ++bucket) {
            const auto bucket_offset = bucket_range.begin + static_cast<std::uint32_t>(bucket);
            normalized_bucket_reach_[bucket_offset] =
                prior_bucket_weight(bucket_map(), meta, meta.id, bucket);
        }
    }
    mark_worker_scope(
        "hunl_flat.normalize_bucket_reach",
        worker_index,
        std::chrono::duration<double>(std::chrono::steady_clock::now() - worker_start).count());
}

void HUNLFlatDCFR::backward_value_stage() {
    std::fill(node_values_.begin(), node_values_.end(), 0.0);
    std::fill(action_values_.begin(), action_values_.end(), 0.0);

    for (std::size_t depth = graph_.depth_slices.size(); depth-- > 0;) {
        run_stage_workers(HUNLFlatStageKind::Backward, StageCommand::BackwardDepth, depth);
    }
}

void HUNLFlatDCFR::worker_backward_depth(std::size_t worker_index, std::size_t depth) {
    const auto& worker = parallel_plan_.workers[worker_index];
    auto& scratch = worker_scratch_[worker_index];
    const auto range = worker.depth_node_ranges[depth];
    const auto worker_start = std::chrono::steady_clock::now();
    for (std::uint32_t order_idx = range.begin; order_idx < range.end; ++order_idx) {
        const auto node_idx = graph_.depth_order[order_idx];
        const auto& meta = graph_.node_meta[node_idx];
        if (meta.type == HUNLFlatNodeType::TerminalFold ||
            meta.type == HUNLFlatNodeType::TerminalShowdown ||
            meta.type == HUNLFlatNodeType::DepthLimited) {
            node_values_[node_idx] = terminal_values_[node_idx];
            continue;
        }
        if (meta.child_count == 0) {
            continue;
        }

        if (meta.type == HUNLFlatNodeType::Chance) {
            const auto chance_start = std::chrono::steady_clock::now();
            auto* row = scratch.row_values.data();
            auto* weights = scratch.row_weights.data();
            for (std::size_t i = 0; i < meta.chance_count; ++i) {
                const auto& outcome = graph_.chance_outcomes[meta.chance_begin + i];
                row[i] = node_values_[outcome.child];
                weights[i] = outcome.probability;
            }
            copy_values(action_values_.data() + meta.child_begin, row, meta.chance_count);
            node_values_[node_idx] = reduce_weighted_action_values(row, weights, meta.chance_count);
            mark_worker_detail_scope(
                "hunl_flat.backward.chance",
                worker_index,
                std::chrono::duration<double>(std::chrono::steady_clock::now() - chance_start).count());
            continue;
        }

        if (meta.type != HUNLFlatNodeType::Decision || !meta.has_infoset) {
            continue;
        }

        const auto decision_start = std::chrono::steady_clock::now();
        const auto lookup_start = std::chrono::steady_clock::now();
        const auto& infoset_meta = infoset_table_.meta().at(meta.infoset_id.value);
        const auto* strategy = infoset_table_.current_strategy(meta.infoset_id);
        const auto bucket_range = infoset_table_.infoset_bucket_range(meta.infoset_id);
        mark_worker_detail_scope(
            "hunl_flat.backward.strategy_lookup",
            worker_index,
            std::chrono::duration<double>(std::chrono::steady_clock::now() - lookup_start).count());
        if (infoset_meta.bucket_count == 0 || infoset_meta.action_count == 0) {
            throw std::logic_error("HUNLFlatDCFR backward stage requires non-empty bucket/action counts");
        }
        const auto* normalized_bucket_reach = normalized_bucket_reach_.data() + bucket_range.begin;
        double node_value = 0.0;
        for (std::size_t i = 0; i < meta.child_count; ++i) {
            const auto child = graph_.children[meta.child_begin + i];
            const auto child_value = node_values_[child];
            action_values_[meta.child_begin + i] = child_value;

            double action_prob = 0.0;
            if (infoset_table_.layout() == HUNLFlatValueLayout::InfosetActionHand) {
                action_prob = dot_product(
                    normalized_bucket_reach,
                    strategy + i * static_cast<std::size_t>(infoset_meta.bucket_count),
                    infoset_meta.bucket_count);
            } else {
                action_prob = dot_product_strided(
                    normalized_bucket_reach,
                    strategy + i,
                    infoset_meta.bucket_count,
                    static_cast<std::size_t>(infoset_meta.action_count));
            }
            node_value += child_value * action_prob;
        }
        const auto reduction_start = std::chrono::steady_clock::now();
        node_values_[node_idx] = node_value;
        mark_worker_detail_scope(
            "hunl_flat.backward.row_reduction",
            worker_index,
            std::chrono::duration<double>(std::chrono::steady_clock::now() - reduction_start).count());
        mark_worker_detail_scope(
            "hunl_flat.backward.decision",
            worker_index,
            std::chrono::duration<double>(std::chrono::steady_clock::now() - decision_start).count());
    }
    mark_worker_scope(
        "hunl_flat.backward",
        worker_index,
        std::chrono::duration<double>(std::chrono::steady_clock::now() - worker_start).count());
}

void HUNLFlatDCFR::regret_update_stage() {
    run_stage_workers(HUNLFlatStageKind::Regret, StageCommand::Regret);
}

void HUNLFlatDCFR::worker_regret_stage(std::size_t worker_index) {
    const auto& metas = infoset_table_.meta();
    const auto range = parallel_plan_.workers[worker_index].infoset_range;
    const auto worker_start = std::chrono::steady_clock::now();
    for (std::uint32_t infoset_index = range.begin; infoset_index < range.end; ++infoset_index) {
        const auto& meta = metas[infoset_index];
        if (meta.id.value >= graph_.infosets.size()) {
            throw std::logic_error("HUNLFlatDCFR regret stage infoset id out of graph bounds");
        }
        auto* regret = infoset_table_.regret_mut(meta.id);
        const auto node_idx = graph_.infoset_nodes[graph_.infosets[meta.id.value].node_begin];
        const auto& node_meta = graph_.node_meta[node_idx];
        const double cf_reach =
            chance_reach_[node_idx] *
            (meta.player == 0 ? player1_reach_[node_idx] : player0_reach_[node_idx]);
        const auto bucket_range = infoset_table_.infoset_bucket_range(meta.id);
        const auto* bucket_norm = normalized_bucket_reach_.data() + bucket_range.begin;
        const auto* edge_values = action_values_.data() + node_meta.child_begin;
        const double base_value = node_values_[node_idx];

        if (infoset_table_.layout() == HUNLFlatValueLayout::InfosetHandAction) {
            for (std::size_t bucket = 0; bucket < meta.bucket_count; ++bucket) {
                const auto bucket_offset = bucket * static_cast<std::size_t>(meta.action_count);
                const auto weighted_cf_reach = cf_reach * bucket_norm[bucket];
                update_regret_sum(
                    regret + bucket_offset,
                    edge_values,
                    meta.action_count,
                    base_value,
                    weighted_cf_reach);
            }
            continue;
        }

        for (std::size_t bucket = 0; bucket < meta.bucket_count; ++bucket) {
            const auto weighted_cf_reach = cf_reach * bucket_norm[bucket];
            update_regret_sum_strided(
                regret + bucket,
                edge_values,
                meta.action_count,
                meta.bucket_count,
                base_value,
                weighted_cf_reach);
        }
    }
    mark_worker_scope(
        "hunl_flat.regret",
        worker_index,
        std::chrono::duration<double>(std::chrono::steady_clock::now() - worker_start).count());
}

void HUNLFlatDCFR::average_strategy_stage() {
    run_stage_workers(HUNLFlatStageKind::AverageStrategy, StageCommand::AverageStrategy);
}

void HUNLFlatDCFR::worker_average_strategy_stage(std::size_t worker_index) {
    const auto& metas = infoset_table_.meta();
    const auto range = parallel_plan_.workers[worker_index].infoset_range;
    const auto worker_start = std::chrono::steady_clock::now();
    for (std::uint32_t infoset_index = range.begin; infoset_index < range.end; ++infoset_index) {
        const auto& meta = metas[infoset_index];
        if (meta.id.value >= graph_.infosets.size()) {
            throw std::logic_error("HUNLFlatDCFR average strategy stage infoset id out of graph bounds");
        }
        auto* strategy_sum = infoset_table_.strategy_sum_mut(meta.id);
        const auto* strategy = infoset_table_.current_strategy(meta.id);
        const auto node_idx = graph_.infoset_nodes[graph_.infosets[meta.id.value].node_begin];
        const double own_reach =
            chance_reach_[node_idx] *
            (meta.player == 0 ? player0_reach_[node_idx] : player1_reach_[node_idx]);
        const auto bucket_range = infoset_table_.infoset_bucket_range(meta.id);
        const auto* bucket_norm = normalized_bucket_reach_.data() + bucket_range.begin;

        if (infoset_table_.layout() == HUNLFlatValueLayout::InfosetHandAction) {
            for (std::size_t bucket = 0; bucket < meta.bucket_count; ++bucket) {
                const auto bucket_offset = bucket * static_cast<std::size_t>(meta.action_count);
                const auto weighted_own_reach = own_reach * bucket_norm[bucket];
                update_strategy_sum(
                    strategy_sum + bucket_offset,
                    strategy + bucket_offset,
                    meta.action_count,
                    weighted_own_reach);
            }
            continue;
        }

        for (std::size_t bucket = 0; bucket < meta.bucket_count; ++bucket) {
            const auto weighted_own_reach = own_reach * bucket_norm[bucket];
            update_strategy_sum_strided(
                strategy_sum + bucket,
                strategy + bucket,
                meta.action_count,
                meta.bucket_count,
                weighted_own_reach);
        }
    }
    mark_worker_scope(
        "hunl_flat.average",
        worker_index,
        std::chrono::duration<double>(std::chrono::steady_clock::now() - worker_start).count());
}

}  // namespace core
