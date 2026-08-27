#include "solver/hunl/sampled/hunl_sampled_trajectory.hpp"

#include "solver/hunl/sampled/hunl_sampled_storage.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace texas::solver::hunl {
namespace {

bool delta_cell_less(
    const HUNLSampledValueDelta& lhs,
    const HUNLSampledValueDelta& rhs) noexcept {
    if (lhs.infoset_id.value != rhs.infoset_id.value) {
        return lhs.infoset_id.value < rhs.infoset_id.value;
    }
    if (lhs.bucket != rhs.bucket) return lhs.bucket < rhs.bucket;
    return lhs.action < rhs.action;
}

bool same_delta_cell(
    const HUNLSampledValueDelta& lhs,
    const HUNLSampledValueDelta& rhs) noexcept {
    return lhs.infoset_id.value == rhs.infoset_id.value &&
           lhs.bucket == rhs.bucket && lhs.action == rhs.action;
}

bool delta_order_less(
    const HUNLSampledValueDelta& lhs,
    const HUNLSampledValueDelta& rhs) noexcept {
    if (delta_cell_less(lhs, rhs)) return true;
    if (delta_cell_less(rhs, lhs)) return false;
    return lhs.trajectory_id < rhs.trajectory_id;
}

template <class Visitor>
void visit_merged_delta_cells(
    HUNLSampledWorkerScratch* streams,
    std::size_t stream_count,
    Visitor&& visitor) {
    for (std::size_t stream = 0; stream < stream_count; ++stream) {
        streams[stream].merge_cursor = 0;
    }
    while (true) {
        const HUNLSampledValueDelta* cell = nullptr;
        for (std::size_t stream = 0; stream < stream_count; ++stream) {
            const auto cursor = streams[stream].merge_cursor;
            if (cursor >= streams[stream].deltas.size()) continue;
            const auto& candidate = streams[stream].deltas[cursor];
            if (cell == nullptr || delta_cell_less(candidate, *cell)) cell = &candidate;
        }
        if (cell == nullptr) return;
        const auto cell_key = *cell;
        double regret_delta = 0.0;
        double strategy_delta = 0.0;
        while (true) {
            std::size_t selected_stream = stream_count;
            const HUNLSampledValueDelta* selected = nullptr;
            for (std::size_t stream = 0; stream < stream_count; ++stream) {
                const auto cursor = streams[stream].merge_cursor;
                if (cursor >= streams[stream].deltas.size()) continue;
                const auto& candidate = streams[stream].deltas[cursor];
                if (!same_delta_cell(candidate, cell_key)) continue;
                if (selected == nullptr || delta_order_less(candidate, *selected) ||
                    (!delta_order_less(*selected, candidate) && stream < selected_stream)) {
                    selected = &candidate;
                    selected_stream = stream;
                }
            }
            if (selected == nullptr) break;
            if (!std::isfinite(selected->regret) || !std::isfinite(selected->strategy_sum) ||
                !std::isfinite(regret_delta + selected->regret) ||
                !std::isfinite(strategy_delta + selected->strategy_sum)) {
                throw std::overflow_error("sampled merge contains a non-finite delta");
            }
            regret_delta += selected->regret;
            strategy_delta += selected->strategy_sum;
            ++streams[selected_stream].merge_cursor;
        }
        visitor(cell_key, regret_delta, strategy_delta);
    }
}

void merge_delta_streams(
    HUNLSampledStorage& storage,
    HUNLSampledWorkerScratch* streams,
    std::size_t stream_count) {
    for (std::size_t stream = 0; stream < stream_count; ++stream) {
        std::sort(streams[stream].deltas.begin(), streams[stream].deltas.end(), delta_order_less);
    }
    visit_merged_delta_cells(
        streams,
        stream_count,
        [&storage](const HUNLSampledValueDelta& cell, double regret_delta, double strategy_delta) {
            const auto row = storage.view(cell.infoset_id);
            if (row.empty() || cell.bucket >= row.bucket_count || cell.action >= row.action_count) {
                throw std::logic_error("sampled traversal delta does not match its infoset row");
            }
            const auto offset = HUNLSampledStorage::value_index(
                row.layout, row.bucket_count, row.action_count, cell.bucket, cell.action);
            const auto next_regret = static_cast<double>(row.regret[offset]) + regret_delta;
            const auto next_strategy = static_cast<double>(row.strategy_sum[offset]) + strategy_delta;
            if (!std::isfinite(next_regret) || !std::isfinite(next_strategy) ||
                std::fabs(next_regret) > std::numeric_limits<float>::max() ||
                std::fabs(next_strategy) > std::numeric_limits<float>::max()) {
                throw std::overflow_error("sampled merge would produce a non-finite float row");
            }
        });
    visit_merged_delta_cells(
        streams,
        stream_count,
        [&storage](const HUNLSampledValueDelta& cell, double regret_delta, double strategy_delta) {
            const auto row = storage.view_mut(cell.infoset_id);
            const auto offset = HUNLSampledStorage::value_index(
                row.layout, row.bucket_count, row.action_count, cell.bucket, cell.action);
            row.regret[offset] = static_cast<float>(static_cast<double>(row.regret[offset]) + regret_delta);
            row.strategy_sum[offset] = static_cast<float>(
                static_cast<double>(row.strategy_sum[offset]) + strategy_delta);
        });
}

}  // namespace

void HUNLSampledWorkerScratch::clear_keep_capacity() noexcept {
    action_values.clear();
    strategy.clear();
    deltas.clear();
    merge_cursor = 0;
}

void HUNLSampledWorkerScratch::reserve_deltas(std::size_t count) {
    deltas.reserve(count);
}

void merge_hunl_sampled_worker_deltas(
    HUNLSampledStorage& storage,
    HUNLSampledWorkerScratch& scratch) {
    merge_delta_streams(storage, &scratch, 1U);
}

void merge_hunl_sampled_worker_streams(
    HUNLSampledStorage& storage,
    std::vector<HUNLSampledWorkerScratch>& streams) {
    merge_delta_streams(storage, streams.data(), streams.size());
}

}  // namespace texas::solver::hunl
