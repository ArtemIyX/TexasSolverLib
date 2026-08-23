#pragma once

#include "core/legacy_namespace_compat.hpp"

#include <cstdint>

namespace texas::games::multiway {

enum class MultiwayRakeMode : std::uint8_t {
    ExplicitZero,
    PercentageOfContestedPot,
};

// Immutable cash-game settlement policy. Percentage rake is rounded down in
// chips and applied once to the combined contested-pot total, never refunds.
struct MultiwayRakePolicy {
    MultiwayRakeMode mode = MultiwayRakeMode::ExplicitZero;
    std::uint16_t basis_points = 0;
    int cap = 0;
    bool no_flop_no_drop = true;

    [[nodiscard]] static constexpr MultiwayRakePolicy explicit_zero() noexcept {
        return {};
    }

    void validate() const;
    [[nodiscard]] int rake_for_contested_pot(int contested_pot, bool flop_seen) const;
    [[nodiscard]] std::uint64_t identity() const noexcept;
};

}  // namespace texas::games::multiway
