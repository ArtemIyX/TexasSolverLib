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

    // Installs the regret row for one abstract continuation information set.
    // Rows are request-owned and remain private-card independent.
    void set_regrets(
        const MultiwayContinuationSelectionKey& key,
        const std::array<double, MULTIWAY_FIXED_CONTINUATION_POLICIES.size()>& regrets);

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
