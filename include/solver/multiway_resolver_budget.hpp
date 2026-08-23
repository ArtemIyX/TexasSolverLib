#pragma once

#include "core/legacy_namespace_compat.hpp"

#include <chrono>
#include <cstddef>
#include <cstdint>

namespace texas::solver::multiway {

struct MultiwayResolverBudgetConfig {
    std::chrono::steady_clock::time_point external_deadline{};
    std::chrono::milliseconds deadline_reserve{};
    std::uint32_t max_batches = 0U;
    std::uint32_t trajectories_per_batch = 0U;
    std::size_t max_sparse_rows = 0U;
    std::size_t max_sparse_values = 0U;

    void validate() const;
};

enum class MultiwayResolverBudgetCheckpoint : std::uint8_t {
    Ready,
    DeadlineExpired,
    LimitReached,
    Cancelled,
};

// Request-local budget state. It is controlled by the resolver thread at
// bounded batch checkpoints and does not own worker or solver state.
class MultiwayResolverBudget final {
public:
    explicit MultiwayResolverBudget(MultiwayResolverBudgetConfig config);

    [[nodiscard]] MultiwayResolverBudgetCheckpoint checkpoint(
        std::uint32_t batch,
        std::uint64_t first_trajectory) noexcept;

    [[nodiscard]] bool accept_clean_batch(
        bool clean,
        std::uint64_t accepted_trajectories,
        std::uint64_t merged_delta_entries,
        std::size_t admitted_rows,
        std::size_t admitted_values) noexcept;

    void cancel() noexcept { cancelled_ = true; }

    [[nodiscard]] bool deadline_reached() const noexcept;
    [[nodiscard]] bool deadline_expired() const noexcept { return deadline_expired_; }
    [[nodiscard]] bool cancelled() const noexcept { return cancelled_; }
    [[nodiscard]] std::uint64_t clean_batches() const noexcept { return clean_batches_; }

private:
    std::chrono::steady_clock::time_point internal_deadline_{};
    std::uint32_t max_batches_ = 0U;
    std::uint64_t max_trajectories_ = 0U;
    std::size_t max_sparse_rows_ = 0U;
    std::size_t max_sparse_values_ = 0U;
    std::uint64_t clean_batches_ = 0U;
    bool cancelled_ = false;
    bool deadline_expired_ = false;
};

}  // namespace texas::solver::multiway
