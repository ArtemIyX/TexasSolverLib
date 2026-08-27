#include "solver/multiway/blueprint/multiway_blueprint_store.hpp"

#include <algorithm>
#include <cstddef>
#include <limits>
#include <stdexcept>
#include <tuple>

namespace texas::solver::multiway {
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
    : identity_(identity) {
    identity_.validate();
    std::sort(rows.begin(), rows.end(), row_less);
    std::size_t action_count = 0U;
    for (std::size_t index = 0U; index < rows.size(); ++index) {
        rows[index].validate();
        if (rows[index].actions.size() > std::numeric_limits<std::size_t>::max() - action_count) {
            throw std::length_error("multiway blueprint action storage is too large");
        }
        action_count += rows[index].actions.size();
        if (index != 0U && same_key(rows[index - 1U], rows[index])) {
            throw std::invalid_argument("multiway blueprint store has duplicate row keys");
        }
    }
    rows_.reserve(rows.size());
    actions_.reserve(action_count);
    for (const auto& row : rows) {
        const auto action_offset = actions_.size();
        actions_.insert(actions_.end(), row.actions.begin(), row.actions.end());
        rows_.push_back({row.infoset, row.bucket, row.action_menu_id, action_offset, row.actions.size()});
    }
}

MultiwayBlueprintRowView MultiwayBlueprintStore::find(
    MultiwayInfosetId infoset,
    std::uint32_t bucket,
    std::uint64_t action_menu_id) const noexcept {
    const auto it = std::lower_bound(
        rows_.begin(),
        rows_.end(),
        std::tuple{infoset.public_state.value, infoset.seat, bucket, action_menu_id},
        [](const RuntimeRow& row, const auto& key) noexcept {
            return std::tuple{
                row.infoset.public_state.value, row.infoset.seat, row.bucket, row.action_menu_id} < key;
        });
    if (it == rows_.end() ||
        it->infoset != infoset || it->bucket != bucket || it->action_menu_id != action_menu_id) {
        return {};
    }
    return {
        it->infoset,
        it->bucket,
        it->action_menu_id,
        actions_.data() + it->action_offset,
        it->action_count};
}

std::uint64_t MultiwayBlueprintStore::memory_bytes() const noexcept {
    constexpr auto maximum = std::numeric_limits<std::uint64_t>::max();
    const auto saturating_add = [maximum](std::uint64_t left, std::uint64_t right) noexcept {
        return right > maximum - left ? maximum : left + right;
    };
    const auto saturating_multiply = [maximum](std::uint64_t left, std::uint64_t right) noexcept {
        return left != 0U && right > maximum / left ? maximum : left * right;
    };
    auto bytes = saturating_multiply(rows_.capacity(), sizeof(RuntimeRow));
    bytes = saturating_add(bytes, saturating_multiply(
        actions_.capacity(), sizeof(MultiwayQuantizedRootAction)));
    return bytes;
}

}  // namespace texas::solver::multiway
