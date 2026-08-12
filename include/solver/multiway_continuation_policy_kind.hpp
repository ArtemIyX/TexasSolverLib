#pragma once

#include "core/namespaces.hpp"

#include <array>
#include <cstdint>

namespace texas::solver::multiway {

enum class MultiwayContinuationPolicyKind : std::uint8_t {
    Blueprint,
    FoldBiased,
    CallBiased,
    RaiseBiased,
};

inline constexpr std::array<MultiwayContinuationPolicyKind, 4>
    MULTIWAY_FIXED_CONTINUATION_POLICIES = {
        MultiwayContinuationPolicyKind::Blueprint,
        MultiwayContinuationPolicyKind::FoldBiased,
        MultiwayContinuationPolicyKind::CallBiased,
        MultiwayContinuationPolicyKind::RaiseBiased,
    };

[[nodiscard]] constexpr bool is_valid_multiway_continuation_policy(
    MultiwayContinuationPolicyKind policy) noexcept {
    return policy == MultiwayContinuationPolicyKind::Blueprint ||
           policy == MultiwayContinuationPolicyKind::FoldBiased ||
           policy == MultiwayContinuationPolicyKind::CallBiased ||
           policy == MultiwayContinuationPolicyKind::RaiseBiased;
}

}  // namespace texas::solver::multiway
