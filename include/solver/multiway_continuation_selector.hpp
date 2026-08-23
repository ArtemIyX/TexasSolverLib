#pragma once

#include "core/legacy_namespace_compat.hpp"

#include "solver/multiway_continuation_policy_kind.hpp"
#include "solver/multiway_solver.hpp"

#include <cstdint>

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
        const MultiwayContinuationSelectionKey& key) const noexcept;

private:
    MultiwayContinuationPolicyKind policy_ = MultiwayContinuationPolicyKind::Blueprint;
};

}  // namespace texas::solver::multiway
