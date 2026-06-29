#include "util/infoset_registry.hpp"

#include <cstddef>
#include <stdexcept>

namespace core {

InfosetId InfosetRegistry::intern(const InfosetKey& key, std::size_t action_count) {
    if (action_count == 0) {
        throw std::invalid_argument("InfosetRegistry::intern requires action_count > 0");
    }

    if (const auto it = key_to_id_.find(key); it != key_to_id_.end()) {
        const auto& meta = meta_.at(it->second.value);
        if (meta.action_count != action_count) {
            throw std::logic_error("InfosetRegistry::intern encountered mismatched action_count");
        }
        return it->second;
    }

    const InfosetId id{static_cast<std::uint32_t>(id_to_key_.size())};
    id_to_key_.push_back(key);
    meta_.push_back(InfosetMeta{action_count});
    key_to_id_.emplace(id_to_key_.back(), id);
    return id;
}

std::optional<InfosetId> InfosetRegistry::find(const InfosetKey& key) const {
    if (const auto it = key_to_id_.find(key); it != key_to_id_.end()) {
        return it->second;
    }
    return std::nullopt;
}

const InfosetKey& InfosetRegistry::key_for(InfosetId id) const {
    if (id.value >= id_to_key_.size()) {
        throw std::out_of_range("InfosetRegistry invalid InfosetId");
    }
    return id_to_key_[id.value];
}

const InfosetMeta& InfosetRegistry::meta_for(InfosetId id) const {
    if (id.value >= meta_.size()) {
        throw std::out_of_range("InfosetRegistry invalid InfosetId");
    }
    return meta_[id.value];
}

const std::vector<InfosetKey>& InfosetRegistry::keys() const noexcept {
    return id_to_key_;
}

std::size_t InfosetRegistry::size() const noexcept {
    return id_to_key_.size();
}

bool InfosetRegistry::empty() const noexcept {
    return id_to_key_.empty();
}

void InfosetRegistry::clear() noexcept {
    key_to_id_.clear();
    id_to_key_.clear();
    meta_.clear();
}

}  // namespace core
