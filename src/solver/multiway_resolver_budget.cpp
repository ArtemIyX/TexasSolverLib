#include "solver/multiway_resolver_budget.hpp"

#include <stdexcept>

namespace texas::solver::multiway {

namespace {

std::chrono::steady_clock::time_point make_internal_deadline(
    const MultiwayResolverBudgetConfig& config) {
    config.validate();
    return config.external_deadline - config.deadline_reserve;
}

}  // namespace

void MultiwayResolverBudgetConfig::validate() const {
    if (external_deadline == std::chrono::steady_clock::time_point{} ||
        deadline_reserve.count() < 0 || max_batches == 0U ||
        trajectories_per_batch == 0U || max_sparse_rows == 0U ||
        max_sparse_values == 0U) {
        throw std::invalid_argument("multiway resolver budget requires positive limits");
    }
}

MultiwayResolverBudget::MultiwayResolverBudget(MultiwayResolverBudgetConfig config)
    : internal_deadline_(make_internal_deadline(config)),
      max_batches_(config.max_batches),
      max_trajectories_(static_cast<std::uint64_t>(config.max_batches) *
          static_cast<std::uint64_t>(config.trajectories_per_batch)),
      max_sparse_rows_(config.max_sparse_rows),
      max_sparse_values_(config.max_sparse_values) {}

MultiwayResolverBudgetCheckpoint MultiwayResolverBudget::checkpoint(
    std::uint32_t batch,
    std::uint64_t first_trajectory) noexcept {
    if (cancelled_) return MultiwayResolverBudgetCheckpoint::Cancelled;
    if (batch >= max_batches_ || first_trajectory >= max_trajectories_) {
        cancelled_ = true;
        return MultiwayResolverBudgetCheckpoint::LimitReached;
    }
    if (deadline_reached()) {
        cancelled_ = true;
        deadline_expired_ = true;
        return MultiwayResolverBudgetCheckpoint::DeadlineExpired;
    }
    return MultiwayResolverBudgetCheckpoint::Ready;
}

bool MultiwayResolverBudget::accept_clean_batch(
    bool clean,
    std::uint64_t accepted_trajectories,
    std::uint64_t merged_delta_entries,
    std::size_t admitted_rows,
    std::size_t admitted_values) noexcept {
    if (cancelled_ || !clean || accepted_trajectories == 0U || merged_delta_entries == 0U ||
        admitted_rows > max_sparse_rows_ || admitted_values > max_sparse_values_) {
        cancelled_ = true;
        return false;
    }
    ++clean_batches_;
    return true;
}

bool MultiwayResolverBudget::deadline_reached() const noexcept {
    return std::chrono::steady_clock::now() >= internal_deadline_;
}

}  // namespace texas::solver::multiway
