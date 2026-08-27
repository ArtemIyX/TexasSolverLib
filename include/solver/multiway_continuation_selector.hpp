#pragma once

#include "solver/multiway_continuation_policy_kind.hpp"
#include "solver/multiway_solver.hpp"

#include <cstdint>
#include <array>
#include <mutex>
#include <vector>

namespace texas::solver::multiway {

// This key deliberately contains only public state, the acting seat, and the
// abstract future bucket. It must not acquire private cards or range weights.
struct MultiwayContinuationSelectionKey {
    MultiwayPublicStateId public_state{};
    PlayerId actor = -1;
    Street street = Street::Preflop;
    std::uint32_t future_bucket = 0;
    std::uint64_t action_abstraction_version = 0;
    std::uint64_t leaf_model_version = 0;

    [[nodiscard]] bool valid() const noexcept;
};

struct MultiwayContinuationDelta {
    MultiwayContinuationSelectionKey key{};
    std::array<double, MULTIWAY_FIXED_CONTINUATION_POLICIES.size()> mixture{};
    std::array<double, MULTIWAY_FIXED_CONTINUATION_POLICIES.size()> values{};
    std::uint64_t trajectory_id = 0U;
    std::uint32_t sequence = 0U;
    double importance_weight = 1.0;
};

class MultiwayContinuationDeltaStream {
public:
    MultiwayContinuationDeltaStream(std::size_t worker_index, std::size_t capacity);

    [[nodiscard]] std::size_t worker_index() const noexcept { return worker_index_; }
    [[nodiscard]] std::size_t size() const noexcept { return deltas_.size(); }
    [[nodiscard]] const std::vector<MultiwayContinuationDelta>& deltas() const noexcept { return deltas_; }
    [[nodiscard]] bool try_append(const MultiwayContinuationDelta& delta) noexcept;
    void rewind(std::size_t size) noexcept;
    void sort_fixed_order() noexcept;
    [[nodiscard]] bool is_fixed_order() const noexcept;

private:
    std::size_t worker_index_ = 0U;
    std::size_t capacity_ = 0U;
    std::vector<MultiwayContinuationDelta> deltas_;
};

// First-phase selector: a versioned fixed policy for every matching abstract
// information set. The structured key keeps later learned selection rows in
// the same information-set namespace without permitting private-card leakage.
class MultiwayFixedContinuationSelector {
public:
    explicit MultiwayFixedContinuationSelector(MultiwayContinuationPolicyKind policy) noexcept
        : policy_(policy) {}

    [[nodiscard]] bool valid() const noexcept;
    [[nodiscard]] MultiwayContinuationPolicyKind select(
        const MultiwayContinuationSelectionKey& key) const;

    // Returns the regret-matched policy mixture for one public continuation
    // information set. Rows without positive regret use the configured prior.
    [[nodiscard]] std::array<double, MULTIWAY_FIXED_CONTINUATION_POLICIES.size()>
    strategy(const MultiwayContinuationSelectionKey& key) const;

    // Adds one complete continuation-policy value vector to the owning row.
    // The update is serialized because traversal workers share this solver state.
    void update_regrets(
        const MultiwayContinuationSelectionKey& key,
        const std::array<double, MULTIWAY_FIXED_CONTINUATION_POLICIES.size()>& mixture,
        const std::array<double, MULTIWAY_FIXED_CONTINUATION_POLICIES.size()>& values) const;
    void update_regrets_weighted(
        const MultiwayContinuationSelectionKey& key,
        const std::array<double, MULTIWAY_FIXED_CONTINUATION_POLICIES.size()>& mixture,
        const std::array<double, MULTIWAY_FIXED_CONTINUATION_POLICIES.size()>& values,
        double importance_weight) const;

    // Installs the regret row for one abstract continuation information set.
    // Rows are request-owned and remain private-card independent.
    void set_regrets(
        const MultiwayContinuationSelectionKey& key,
        const std::array<double, MULTIWAY_FIXED_CONTINUATION_POLICIES.size()>& regrets);

    void merge_worker_streams(
        const std::vector<const MultiwayContinuationDeltaStream*>& streams) const;

private:
    MultiwayContinuationPolicyKind policy_ = MultiwayContinuationPolicyKind::Blueprint;
    struct Row {
        MultiwayContinuationSelectionKey key{};
        std::array<double, MULTIWAY_FIXED_CONTINUATION_POLICIES.size()> regrets{};
    };
    mutable std::vector<Row> rows_;
    mutable std::mutex mutex_;
};

}  // namespace texas::solver::multiway
