#include "solver/hunl_flat_dcfr.hpp"

#include "util/simd.hpp"

#include <algorithm>
#include <chrono>
#include <stdexcept>

namespace core {

HUNLFlatDCFR::HUNLFlatDCFR(
    HUNLFlatSolveGraph graph,
    std::array<std::size_t, 2> hand_count_per_player,
    HUNLFlatValueLayout layout)
    : graph_(std::move(graph)),
      infoset_table_(HUNLFlatInfosetTable::build(graph_, hand_count_per_player, layout)),
      node_reach_(graph_.nodes.size(), 0.0),
      terminal_values_(graph_.nodes.size(), 0.0),
      backward_values_(graph_.nodes.size(), 0.0) {}

void HUNLFlatDCFR::run_iteration() {
    using clock = std::chrono::steady_clock;

    const auto strategy_start = clock::now();
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

std::unordered_map<std::string, std::vector<double>> HUNLFlatDCFR::export_average_strategy() const {
    std::unordered_map<std::string, std::vector<double>> out;
    out.reserve(graph_.infosets.size());

    for (const auto& infoset : graph_.infosets) {
        const auto& meta = infoset_table_.meta().at(infoset.id.value);
        const auto* strategy_sum = infoset_table_.strategy_sum(infoset.id);
        std::vector<double> average(meta.value_count, 0.0);

        if (infoset_table_.layout() == HUNLFlatValueLayout::InfosetActionHand) {
            for (std::size_t h = 0; h < meta.hand_count; ++h) {
                double total = 0.0;
                for (std::size_t a = 0; a < meta.action_count; ++a) {
                    total += strategy_sum[a * static_cast<std::size_t>(meta.hand_count) + h];
                }
                if (total > 0.0) {
                    for (std::size_t a = 0; a < meta.action_count; ++a) {
                        const auto idx = a * static_cast<std::size_t>(meta.hand_count) + h;
                        average[idx] = strategy_sum[idx] / total;
                    }
                } else {
                    const double uniform = 1.0 / static_cast<double>(meta.action_count);
                    for (std::size_t a = 0; a < meta.action_count; ++a) {
                        average[a * static_cast<std::size_t>(meta.hand_count) + h] = uniform;
                    }
                }
            }
        } else {
            for (std::size_t h = 0; h < meta.hand_count; ++h) {
                const auto hand_offset = h * static_cast<std::size_t>(meta.action_count);
                double total = 0.0;
                for (std::size_t a = 0; a < meta.action_count; ++a) {
                    total += strategy_sum[hand_offset + a];
                }
                if (total > 0.0) {
                    for (std::size_t a = 0; a < meta.action_count; ++a) {
                        average[hand_offset + a] = strategy_sum[hand_offset + a] / total;
                    }
                } else {
                    const double uniform = 1.0 / static_cast<double>(meta.action_count);
                    for (std::size_t a = 0; a < meta.action_count; ++a) {
                        average[hand_offset + a] = uniform;
                    }
                }
            }
        }

        out.emplace(infoset.key, std::move(average));
    }

    return out;
}

void HUNLFlatDCFR::compute_strategy_stage() {
    const auto& metas = infoset_table_.meta();
    for (const auto& meta : metas) {
        auto* regret = infoset_table_.regret_mut(meta.id);
        auto* strategy = infoset_table_.current_strategy_mut(meta.id);

        if (infoset_table_.layout() == HUNLFlatValueLayout::InfosetActionHand) {
            for (std::size_t h = 0; h < meta.hand_count; ++h) {
                double positive_total = 0.0;
                for (std::size_t a = 0; a < meta.action_count; ++a) {
                    const auto idx = a * static_cast<std::size_t>(meta.hand_count) + h;
                    strategy[idx] = std::max(regret[idx], 0.0);
                    positive_total += strategy[idx];
                }
                if (positive_total > 0.0) {
                    for (std::size_t a = 0; a < meta.action_count; ++a) {
                        const auto idx = a * static_cast<std::size_t>(meta.hand_count) + h;
                        strategy[idx] /= positive_total;
                    }
                } else {
                    const double uniform = 1.0 / static_cast<double>(meta.action_count);
                    for (std::size_t a = 0; a < meta.action_count; ++a) {
                        strategy[a * static_cast<std::size_t>(meta.hand_count) + h] = uniform;
                    }
                }
            }
            continue;
        }

        for (std::size_t h = 0; h < meta.hand_count; ++h) {
            const auto hand_offset = h * static_cast<std::size_t>(meta.action_count);
            const double positive_total =
                positive_regrets_and_total(regret + hand_offset, strategy + hand_offset, meta.action_count);
            if (positive_total > 0.0) {
                for (std::size_t a = 0; a < meta.action_count; ++a) {
                    strategy[hand_offset + a] /= positive_total;
                }
            } else {
                const double uniform = 1.0 / static_cast<double>(meta.action_count);
                for (std::size_t a = 0; a < meta.action_count; ++a) {
                    strategy[hand_offset + a] = uniform;
                }
            }
        }
    }
}

void HUNLFlatDCFR::forward_reach_stage() {
    std::fill(node_reach_.begin(), node_reach_.end(), 0.0);
    if (!node_reach_.empty()) {
        node_reach_[graph_.root] = 1.0;
    }
    for (const auto node_idx : graph_.forward_order) {
        const auto reach = node_reach_[node_idx];
        const auto& meta = graph_.node_meta[node_idx];
        if (reach == 0.0 || meta.child_count == 0) {
            continue;
        }

        if (meta.type == HUNLFlatNodeType::Chance) {
            for (std::size_t i = 0; i < meta.chance_count; ++i) {
                const auto& outcome = graph_.chance_outcomes[meta.chance_begin + i];
                node_reach_[outcome.child] += reach * outcome.probability;
            }
        } else {
            const double split = 1.0 / static_cast<double>(meta.child_count);
            for (std::size_t i = 0; i < meta.child_count; ++i) {
                node_reach_[graph_.children[meta.child_begin + i]] += reach * split;
            }
        }
    }
}

void HUNLFlatDCFR::terminal_utility_stage() {
    std::fill(terminal_values_.begin(), terminal_values_.end(), 0.0);
    for (std::size_t node_idx = 0; node_idx < graph_.node_meta.size(); ++node_idx) {
        const auto& meta = graph_.node_meta[node_idx];
        if (meta.type == HUNLFlatNodeType::TerminalFold) {
            terminal_values_[node_idx] = static_cast<double>(meta.terminal_kind.contribution_loss);
        } else if (meta.type == HUNLFlatNodeType::TerminalShowdown) {
            terminal_values_[node_idx] = meta.terminal_kind.board_complete ? 1.0 : 0.5;
        }
    }
}

void HUNLFlatDCFR::backward_value_stage() {
    std::fill(backward_values_.begin(), backward_values_.end(), 0.0);
    for (const auto node_idx : graph_.reverse_order) {
        const auto& meta = graph_.node_meta[node_idx];
        if (meta.type == HUNLFlatNodeType::TerminalFold || meta.type == HUNLFlatNodeType::TerminalShowdown) {
            backward_values_[node_idx] = terminal_values_[node_idx];
            continue;
        }
        if (meta.child_count == 0) {
            continue;
        }

        double total = 0.0;
        if (meta.type == HUNLFlatNodeType::Chance) {
            for (std::size_t i = 0; i < meta.chance_count; ++i) {
                const auto& outcome = graph_.chance_outcomes[meta.chance_begin + i];
                total += outcome.probability * backward_values_[outcome.child];
            }
        } else {
            for (std::size_t i = 0; i < meta.child_count; ++i) {
                total += backward_values_[graph_.children[meta.child_begin + i]];
            }
            total /= static_cast<double>(meta.child_count);
        }
        backward_values_[node_idx] = total;
    }
}

void HUNLFlatDCFR::regret_update_stage() {
    const auto& metas = infoset_table_.meta();
    for (const auto& meta : metas) {
        auto* regret = infoset_table_.regret_mut(meta.id);
        for (std::size_t i = 0; i < meta.value_count; ++i) {
            regret[i] += 0.001;
        }
    }
}

void HUNLFlatDCFR::average_strategy_stage() {
    const auto& metas = infoset_table_.meta();
    for (const auto& meta : metas) {
        auto* strategy_sum = infoset_table_.strategy_sum_mut(meta.id);
        const auto* strategy = infoset_table_.current_strategy(meta.id);
        for (std::size_t i = 0; i < meta.value_count; ++i) {
            strategy_sum[i] += strategy[i];
        }
    }
}

}  // namespace core
