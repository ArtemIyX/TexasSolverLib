#pragma once

#include "core/types.hpp"
#include "games/hunl.hpp"
#include "util/infoset_registry.hpp"

#include <unordered_map>
#include <vector>

namespace core {

namespace detail {

template <class LockedByIdTable>
void store_locked_strategy(
    LockedByIdTable& locked_by_id,
    InfosetId id,
    const std::vector<Probability>& strategy) {
    locked_by_id.set(id, strategy);
}

inline void store_locked_strategy(
    std::unordered_map<InfosetId, std::vector<Probability>>& locked_by_id,
    InfosetId id,
    const std::vector<Probability>& strategy) {
    locked_by_id.emplace(id, strategy);
}

}  // namespace detail

template <class LockedByIdTable>
InfosetId lookup_infoset_id(
    const HUNLState& state,
    PlayerId player,
    InfosetRegistry& registry,
    std::size_t action_count,
    const std::unordered_map<InfosetKey, std::vector<Probability>>* locked = nullptr,
    LockedByIdTable* locked_by_id = nullptr) {
    const auto encoding = state.infoset_encoding(player);
    const auto id = registry.intern(encoding, action_count);
    if (locked != nullptr && locked_by_id != nullptr) {
        const auto& key = registry.key_for(id);
        if (const auto locked_it = locked->find(key);
            locked_it != locked->end() && locked_it->second.size() == action_count) {
            detail::store_locked_strategy(*locked_by_id, id, locked_it->second);
        }
    }
    return id;
}

template <class G, class LockedByIdTable>
InfosetId lookup_infoset_id(
    const G& state,
    PlayerId player,
    InfosetRegistry& registry,
    std::size_t action_count,
    const std::unordered_map<InfosetKey, std::vector<Probability>>* locked = nullptr,
    LockedByIdTable* locked_by_id = nullptr) {
    const auto key = state.infoset_key(player);
    const auto id = registry.intern(key, action_count);
    if (locked != nullptr && locked_by_id != nullptr) {
        if (const auto locked_it = locked->find(key);
            locked_it != locked->end() && locked_it->second.size() == action_count) {
            detail::store_locked_strategy(*locked_by_id, id, locked_it->second);
        }
    }
    return id;
}

}  // namespace core
