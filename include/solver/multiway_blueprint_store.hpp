#pragma once

#include "solver/multiway_export.hpp"

#include <cstdint>
#include <vector>

namespace core {

// Compact immutable runtime blueprint. Rows are sorted by public state, seat,
// and bucket; action probabilities are quantized and action-menu bound.
struct MultiwayBlueprintRow {
    MultiwayInfosetId infoset{};
    std::uint32_t bucket = 0U;
    std::uint32_t action_menu_id = 0U;
    std::vector<MultiwayQuantizedRootAction> actions;

    void validate() const;
};

class MultiwayBlueprintStore {
public:
    MultiwayBlueprintStore(MultiwayModelIdentity identity, std::vector<MultiwayBlueprintRow> rows);

    [[nodiscard]] const MultiwayModelIdentity& identity() const noexcept { return identity_; }
    [[nodiscard]] std::size_t row_count() const noexcept { return rows_.size(); }
    [[nodiscard]] const MultiwayBlueprintRow* find(
        MultiwayInfosetId infoset,
        std::uint32_t bucket,
        std::uint32_t action_menu_id) const noexcept;

private:
    MultiwayModelIdentity identity_{};
    std::vector<MultiwayBlueprintRow> rows_;
};

}  // namespace core
