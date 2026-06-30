#include "solver/hunl_flat_dcfr.hpp"

#include "solver/dcfr.hpp"
#include "util/simd.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <exception>
#include <stdexcept>

namespace core {

HUNLFlatDCFR::WorkerPool::WorkerPool(std::size_t worker_count) {
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

void HUNLFlatDCFR::WorkerPool::run_stage(const std::function<void(std::size_t)>& fn) {
    if (threads_.empty()) {
        return;
    }

    {
        std::lock_guard<std::mutex> lock(mutex_);
        stage_fn_ = fn;
        stage_error_ = nullptr;
        completed_workers_ = 0;
        ++generation_;
    }
    cv_.notify_all();

    std::unique_lock<std::mutex> lock(mutex_);
    finished_cv_.wait(lock, [this] { return completed_workers_ == threads_.size(); });
    stage_fn_ = {};
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
        std::function<void(std::size_t)> stage_fn;
        {
            std::unique_lock<std::mutex> lock(mutex_);
            cv_.wait(lock, [this, seen_generation] {
                return stop_ || generation_ != seen_generation;
            });
            if (stop_) {
                return;
            }
            seen_generation = generation_;
            stage_fn = stage_fn_;
        }

        try {
            if (stage_fn) {
                stage_fn(worker_index);
            }
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
    std::array<std::size_t, 2> hand_count_per_player,
    HUNLFlatValueLayout layout,
    std::size_t workers,
    double alpha,
    double beta,
    double gamma)
    : graph_(std::move(graph)),
      infoset_table_(HUNLFlatInfosetTable::build(graph_, hand_count_per_player, layout)),
      player0_reach_(graph_.nodes.size(), 0.0),
      player1_reach_(graph_.nodes.size(), 0.0),
      chance_reach_(graph_.nodes.size(), 0.0),
      terminal_values_(graph_.nodes.size(), 0.0),
      node_values_(graph_.nodes.size(), 0.0),
      action_values_(graph_.children.size(), 0.0),
      worker_count_(std::max<std::size_t>(1, workers)),
      parallel_plan_(HUNLFlatParallelPlan::build(graph_, std::max<std::size_t>(1, workers))),
      worker_scratch_(std::max<std::size_t>(1, workers)),
      alpha_(alpha),
      beta_(beta),
      gamma_(gamma) {
    validate_alpha(alpha_);
    if (beta_ < 0.0 || gamma_ < 0.0) {
        throw std::invalid_argument("HUNLFlatDCFR beta and gamma must be non-negative");
    }
    for (auto& scratch : worker_scratch_) {
        scratch.ensure_capacity(graph_.nodes.size(), graph_.children.size());
    }
    scheduler_diagnostics_.worker_profiles.assign(worker_count_, HUNLFlatStageProfile{});
    worker_pool_ = std::make_unique<WorkerPool>(worker_count_);
}

HUNLFlatDCFR::~HUNLFlatDCFR() = default;

std::size_t HUNLFlatDCFR::worker_count() const noexcept {
    return worker_count_;
}

const HUNLFlatSchedulerDiagnostics& HUNLFlatDCFR::scheduler_diagnostics() const noexcept {
    return scheduler_diagnostics_;
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

void HUNLFlatDCFR::run_stage_workers(HUNLFlatStageKind stage, const std::function<void(std::size_t)>& fn) {
    if (!worker_pool_ || worker_count_ <= 1) {
        const auto start = std::chrono::steady_clock::now();
        fn(0);
        const auto finish = std::chrono::steady_clock::now();
        add_stage_seconds(
            scheduler_diagnostics_.worker_profiles[0],
            stage,
            std::chrono::duration<double>(finish - start).count());
        return;
    }
    worker_pool_->run_stage([&](std::size_t worker_index) {
        const auto start = std::chrono::steady_clock::now();
        fn(worker_index);
        const auto finish = std::chrono::steady_clock::now();
        add_stage_seconds(
            scheduler_diagnostics_.worker_profiles[worker_index],
            stage,
            std::chrono::duration<double>(finish - start).count());
    });
}

void HUNLFlatDCFR::run_iteration() {
    using clock = std::chrono::steady_clock;

    const auto discount_start = clock::now();
    apply_dcfr_discount_stage();
    const auto discount_end = clock::now();

    const auto strategy_start = discount_end;
    compute_strategy_stage();
    const auto strategy_end = clock::now();

    const auto reach_start = strategy_end;
    forward_reach_stage();
    const auto reach_end = clock::now();

    const auto terminal_start = reach_end;
    terminal_utility_stage();
    const auto terminal_end = clock::now();

    const auto backward_start = terminal_end;
    backward_value_stage();
    const auto backward_end = clock::now();

    const auto regret_start = backward_end;
    regret_update_stage();
    const auto regret_end = clock::now();

    const auto average_start = regret_end;
    average_strategy_stage();
    const auto average_end = clock::now();

    profile_.discount_seconds += std::chrono::duration<double>(discount_end - discount_start).count();
    profile_.strategy_seconds += std::chrono::duration<double>(strategy_end - strategy_start).count();
    profile_.reach_seconds += std::chrono::duration<double>(reach_end - reach_start).count();
    profile_.terminal_seconds += std::chrono::duration<double>(terminal_end - terminal_start).count();
    profile_.backward_seconds += std::chrono::duration<double>(backward_end - backward_start).count();
    profile_.regret_seconds += std::chrono::duration<double>(regret_end - regret_start).count();
    profile_.average_strategy_seconds += std::chrono::duration<double>(average_end - average_start).count();
    ++iterations_;
}

void HUNLFlatDCFR::run_iterations(std::uint32_t iterations) {
    for (std::uint32_t i = 0; i < iterations; ++i) {
        run_iteration();
    }
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
            for (std::size_t h = 0; h < meta.hand_count; ++h) {
                const auto hand_offset = h * static_cast<std::size_t>(meta.action_count);
                normalize_row(strategy_sum + hand_offset, average.data() + hand_offset, meta.action_count);
            }
        } else {
            for (std::size_t h = 0; h < meta.hand_count; ++h) {
                const auto hand_offset = h * static_cast<std::size_t>(meta.action_count);
                normalize_row(strategy_sum + hand_offset, average.data() + hand_offset, meta.action_count);
            }
        }

        out.emplace(infoset.key, std::move(average));
    }

    return out;
}

void HUNLFlatDCFR::apply_dcfr_discount_stage() {
    const auto target_iter = iterations_ + 1U;
    auto& metas = infoset_table_.meta_mut();
    run_stage_workers(HUNLFlatStageKind::Discount, [&](std::size_t worker_index) {
        const auto range = parallel_plan_.workers[worker_index].infoset_range;
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
    });
}

void HUNLFlatDCFR::compute_strategy_stage() {
    const auto& metas = infoset_table_.meta();
    run_stage_workers(HUNLFlatStageKind::Strategy, [&](std::size_t worker_index) {
        const auto range = parallel_plan_.workers[worker_index].infoset_range;
        for (std::uint32_t infoset_index = range.begin; infoset_index < range.end; ++infoset_index) {
            const auto& meta = metas[infoset_index];
            auto* regret = infoset_table_.regret_mut(meta.id);
            auto* strategy = infoset_table_.current_strategy_mut(meta.id);

            if (infoset_table_.layout() == HUNLFlatValueLayout::InfosetHandAction) {
                for (std::size_t h = 0; h < meta.hand_count; ++h) {
                    const auto hand_offset = h * static_cast<std::size_t>(meta.action_count);
                    compute_strategy_row_small(regret + hand_offset, strategy + hand_offset, meta.action_count);
                }
                continue;
            }

            for (std::size_t h = 0; h < meta.hand_count; ++h) {
                const auto hand_offset = h * static_cast<std::size_t>(meta.action_count);
                compute_strategy_row(regret + hand_offset, strategy + hand_offset, meta.action_count);
            }
        }
    });
}

void HUNLFlatDCFR::forward_reach_stage() {
    std::fill(player0_reach_.begin(), player0_reach_.end(), 0.0);
    std::fill(player1_reach_.begin(), player1_reach_.end(), 0.0);
    std::fill(chance_reach_.begin(), chance_reach_.end(), 0.0);
    if (!graph_.nodes.empty()) {
        player0_reach_[graph_.root] = 1.0;
        player1_reach_[graph_.root] = 1.0;
        chance_reach_[graph_.root] = 1.0;
    }

    for (std::size_t depth = 0; depth < graph_.depth_slices.size(); ++depth) {
        run_stage_workers(HUNLFlatStageKind::Reach, [&](std::size_t worker_index) {
            auto& scratch = worker_scratch_[worker_index];
            std::fill(scratch.player0_reach.begin(), scratch.player0_reach.end(), 0.0);
            std::fill(scratch.player1_reach.begin(), scratch.player1_reach.end(), 0.0);
            std::fill(scratch.chance_reach.begin(), scratch.chance_reach.end(), 0.0);

            const auto& worker = parallel_plan_.workers[worker_index];
            const auto range = worker.depth_node_ranges[depth];
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
                const auto* strategy = infoset_table_.current_strategy(meta.infoset_id);
                const std::size_t representative_hand = 0;
                for (std::size_t i = 0; i < meta.child_count; ++i) {
                    const auto child = graph_.children[meta.child_begin + i];
                    double action_prob = 1.0 / static_cast<double>(meta.child_count);
                    if (infoset_table_.layout() == HUNLFlatValueLayout::InfosetActionHand) {
                        action_prob = strategy[i * static_cast<std::size_t>(infoset_meta.hand_count) + representative_hand];
                    } else {
                        action_prob = strategy[representative_hand * static_cast<std::size_t>(infoset_meta.action_count) + i];
                    }

                    if (meta.player == 0) {
                        scratch.player0_reach[child] += reach0 * action_prob;
                        scratch.player1_reach[child] += reach1;
                    } else {
                        scratch.player0_reach[child] += reach0;
                        scratch.player1_reach[child] += reach1 * action_prob;
                    }
                    scratch.chance_reach[child] += chance;
                }
            }
        });

        run_stage_workers(HUNLFlatStageKind::Reach, [&](std::size_t worker_index) {
            const auto range = parallel_plan_.workers[worker_index].node_range;
            for (std::uint32_t node_idx = range.begin; node_idx < range.end; ++node_idx) {
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
        });
    }
}

void HUNLFlatDCFR::terminal_utility_stage() {
    run_stage_workers(HUNLFlatStageKind::Terminal, [&](std::size_t worker_index) {
        if (worker_index != 0) {
            return;
        }

        std::fill(terminal_values_.begin(), terminal_values_.end(), 0.0);
        for (std::size_t i = 0; i < graph_.terminal_nodes.size(); ++i) {
            terminal_values_[graph_.terminal_nodes[i]] = graph_.terminal_node_values[i];
        }
    });
}

void HUNLFlatDCFR::backward_value_stage() {
    std::fill(node_values_.begin(), node_values_.end(), 0.0);
    std::fill(action_values_.begin(), action_values_.end(), 0.0);

    for (std::size_t depth = graph_.depth_slices.size(); depth-- > 0;) {
        run_stage_workers(HUNLFlatStageKind::Backward, [&](std::size_t worker_index) {
            const auto& worker = parallel_plan_.workers[worker_index];
            const auto range = worker.depth_node_ranges[depth];
            for (std::uint32_t order_idx = range.begin; order_idx < range.end; ++order_idx) {
                const auto node_idx = graph_.depth_order[order_idx];
                const auto& meta = graph_.node_meta[node_idx];
                if (meta.type == HUNLFlatNodeType::TerminalFold || meta.type == HUNLFlatNodeType::TerminalShowdown) {
                    node_values_[node_idx] = terminal_values_[node_idx];
                    continue;
                }
                if (meta.child_count == 0) {
                    continue;
                }

                if (meta.type == HUNLFlatNodeType::Chance) {
                    double total = 0.0;
                    for (std::size_t i = 0; i < meta.chance_count; ++i) {
                        const auto& outcome = graph_.chance_outcomes[meta.chance_begin + i];
                        const auto child_value = node_values_[outcome.child];
                        action_values_[meta.child_begin + i] = child_value;
                        total += outcome.probability * child_value;
                    }
                    node_values_[node_idx] = total;
                    continue;
                }

                if (meta.type != HUNLFlatNodeType::Decision || !meta.has_infoset) {
                    continue;
                }

                const auto& infoset_meta = infoset_table_.meta().at(meta.infoset_id.value);
                const auto* strategy = infoset_table_.current_strategy(meta.infoset_id);
                const std::size_t representative_hand = 0;
                double node_value = 0.0;
                for (std::size_t i = 0; i < meta.child_count; ++i) {
                    const auto child = graph_.children[meta.child_begin + i];
                    const auto child_value = node_values_[child];
                    action_values_[meta.child_begin + i] = child_value;

                    double action_prob = 1.0 / static_cast<double>(meta.child_count);
                    if (infoset_table_.layout() == HUNLFlatValueLayout::InfosetActionHand) {
                        action_prob = strategy[i * static_cast<std::size_t>(infoset_meta.hand_count) + representative_hand];
                    } else {
                        action_prob = strategy[representative_hand * static_cast<std::size_t>(infoset_meta.action_count) + i];
                    }
                    node_value += action_prob * child_value;
                }
                node_values_[node_idx] = node_value;
            }
        });
    }
}

void HUNLFlatDCFR::regret_update_stage() {
    const auto& metas = infoset_table_.meta();
    run_stage_workers(HUNLFlatStageKind::Regret, [&](std::size_t worker_index) {
        const auto range = parallel_plan_.workers[worker_index].infoset_range;
        for (std::uint32_t infoset_index = range.begin; infoset_index < range.end; ++infoset_index) {
            const auto& meta = metas[infoset_index];
            auto* regret = infoset_table_.regret_mut(meta.id);
            const auto node_idx = graph_.infoset_nodes[graph_.infosets[meta.id.value].node_begin];
            const auto& node_meta = graph_.node_meta[node_idx];
            const double cf_reach =
                chance_reach_[node_idx] *
                (meta.player == 0 ? player1_reach_[node_idx] : player0_reach_[node_idx]);
            const double base_value = node_values_[node_idx];

            if (infoset_table_.layout() == HUNLFlatValueLayout::InfosetActionHand) {
                for (std::size_t h = 0; h < meta.hand_count; ++h) {
                    for (std::size_t a = 0; a < meta.action_count; ++a) {
                        const auto row_idx = a * static_cast<std::size_t>(meta.hand_count) + h;
                        const auto edge_idx = node_meta.child_begin + a;
                        regret[row_idx] += cf_reach * (action_values_[edge_idx] - base_value);
                    }
                }
                continue;
            }

            for (std::size_t h = 0; h < meta.hand_count; ++h) {
                const auto hand_offset = h * static_cast<std::size_t>(meta.action_count);
                update_regret_sum(
                    regret + hand_offset,
                    action_values_.data() + node_meta.child_begin,
                    meta.action_count,
                    base_value,
                    cf_reach);
            }
        }
    });
}

void HUNLFlatDCFR::average_strategy_stage() {
    const auto& metas = infoset_table_.meta();
    run_stage_workers(HUNLFlatStageKind::AverageStrategy, [&](std::size_t worker_index) {
        const auto range = parallel_plan_.workers[worker_index].infoset_range;
        for (std::uint32_t infoset_index = range.begin; infoset_index < range.end; ++infoset_index) {
            const auto& meta = metas[infoset_index];
            auto* strategy_sum = infoset_table_.strategy_sum_mut(meta.id);
            const auto* strategy = infoset_table_.current_strategy(meta.id);
            const auto node_idx = graph_.infoset_nodes[graph_.infosets[meta.id.value].node_begin];
            const double own_reach =
                chance_reach_[node_idx] *
                (meta.player == 0 ? player0_reach_[node_idx] : player1_reach_[node_idx]);
            update_strategy_sum(strategy_sum, strategy, meta.value_count, own_reach);
        }
    });
}

}  // namespace core
