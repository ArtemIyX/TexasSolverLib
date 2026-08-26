#pragma once

#include "core/legacy_namespace_compat.hpp"

#include "solver/multiway_export.hpp"

#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

namespace texas::solver::multiway {

// Compact immutable runtime blueprint. Rows are sorted by public state, seat,
// and bucket; action probabilities are quantized and action-menu bound.
struct MultiwayBlueprintRow {
    MultiwayInfosetId infoset{};
    std::uint32_t bucket = 0U;
    std::uint64_t action_menu_id = 0U;
    std::vector<MultiwayQuantizedRootAction> actions;

    void validate() const;
};

struct MultiwayBlueprintRowView {
    MultiwayInfosetId infoset{};
    std::uint32_t bucket = 0U;
    std::uint64_t action_menu_id = 0U;
    const MultiwayQuantizedRootAction* actions = nullptr;
    std::size_t action_count = 0U;

    [[nodiscard]] bool valid() const noexcept { return actions != nullptr && action_count != 0U; }
};

class MultiwayBlueprintStore {
public:
    MultiwayBlueprintStore(MultiwayModelIdentity identity, std::vector<MultiwayBlueprintRow> rows);

    [[nodiscard]] const MultiwayModelIdentity& identity() const noexcept { return identity_; }
    [[nodiscard]] std::size_t row_count() const noexcept { return rows_.size(); }
    [[nodiscard]] std::uint64_t memory_bytes() const noexcept;
    [[nodiscard]] MultiwayBlueprintRowView find(
        MultiwayInfosetId infoset,
        std::uint32_t bucket,
        std::uint64_t action_menu_id) const noexcept;

private:
    struct RuntimeRow {
        MultiwayInfosetId infoset{};
        std::uint32_t bucket = 0U;
        std::uint64_t action_menu_id = 0U;
        std::size_t action_offset = 0U;
        std::size_t action_count = 0U;
    };

    MultiwayModelIdentity identity_{};
    std::vector<RuntimeRow> rows_;
    std::vector<MultiwayQuantizedRootAction> actions_;
};

}  // namespace texas::solver::multiway
