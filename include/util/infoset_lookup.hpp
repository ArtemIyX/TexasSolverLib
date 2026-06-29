#pragma once

#include "core/types.hpp"
#include "util/infoset_registry.hpp"

#include <unordered_map>
#include <vector>

namespace core {

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
            locked_by_id->set(id, locked_it->second);
        }
    }
    return id;
}

}  // namespace core
