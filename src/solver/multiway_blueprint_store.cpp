#include "solver/multiway_blueprint_store.hpp"

#include <algorithm>
#include <limits>
#include <stdexcept>

namespace core {
namespace {

bool row_less(const MultiwayBlueprintRow& left, const MultiwayBlueprintRow& right) noexcept {
    if (left.infoset.public_state != right.infoset.public_state) {
        return left.infoset.public_state.value < right.infoset.public_state.value;
    }
    if (left.infoset.seat != right.infoset.seat) return left.infoset.seat < right.infoset.seat;
    if (left.bucket != right.bucket) return left.bucket < right.bucket;
    return left.action_menu_id < right.action_menu_id;
}

bool same_key(const MultiwayBlueprintRow& left, const MultiwayBlueprintRow& right) noexcept {
    return left.infoset == right.infoset && left.bucket == right.bucket &&
        left.action_menu_id == right.action_menu_id;
}

}  // namespace

void MultiwayBlueprintRow::validate() const {
    if (infoset.public_state.value == 0U || infoset.seat < 0 || action_menu_id == 0U || actions.empty()) {
        throw std::invalid_argument("multiway blueprint row is incomplete");
    }
    std::uint32_t total = 0U;
    for (std::size_t index = 0U; index < actions.size(); ++index) {
        const auto& action = actions[index];
        if (action.action.action_index != index || action.action.action_menu_id != action_menu_id ||
            action.probability == 0U) {
            throw std::invalid_argument("multiway blueprint row has invalid action data");
        }
        total += action.probability;
    }
    if (total != 65535U) throw std::invalid_argument("multiway blueprint row is not normalized");
}

MultiwayBlueprintStore::MultiwayBlueprintStore(
    MultiwayModelIdentity identity,
    std::vector<MultiwayBlueprintRow> rows)
    : identity_(identity), rows_(std::move(rows)) {
    identity_.validate();
    std::sort(rows_.begin(), rows_.end(), row_less);
    for (std::size_t index = 0U; index < rows_.size(); ++index) {
        rows_[index].validate();
        if (index != 0U && same_key(rows_[index - 1U], rows_[index])) {
            throw std::invalid_argument("multiway blueprint store has duplicate row keys");
        }
    }
}

const MultiwayBlueprintRow* MultiwayBlueprintStore::find(
    MultiwayInfosetId infoset,
    std::uint32_t bucket,
    std::uint64_t action_menu_id) const noexcept {
    MultiwayBlueprintRow key;
    key.infoset = infoset;
    key.bucket = bucket;
    key.action_menu_id = action_menu_id;
    const auto it = std::lower_bound(rows_.begin(), rows_.end(), key, row_less);
    return it != rows_.end() && same_key(*it, key) ? &*it : nullptr;
}

std::uint64_t MultiwayBlueprintStore::memory_bytes() const noexcept {
    constexpr auto maximum = std::numeric_limits<std::uint64_t>::max();
    const auto saturating_add = [maximum](std::uint64_t left, std::uint64_t right) noexcept {
        return right > maximum - left ? maximum : left + right;
    };
    const auto saturating_multiply = [maximum](std::uint64_t left, std::uint64_t right) noexcept {
        return left != 0U && right > maximum / left ? maximum : left * right;
    };
    auto bytes = saturating_multiply(rows_.capacity(), sizeof(MultiwayBlueprintRow));
    for (const auto& row : rows_) {
        bytes = saturating_add(bytes, saturating_multiply(
            row.actions.capacity(), sizeof(MultiwayQuantizedRootAction)));
    }
    return bytes;
}

}  // namespace core
