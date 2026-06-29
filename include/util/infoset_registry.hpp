#pragma once

#include "core/types.hpp"

#include <cstddef>
#include <optional>
#include <unordered_map>
#include <vector>

namespace core {

struct InfosetMeta {
    std::size_t action_count = 0;
};

/**
 * @brief Stable mapping between external infoset strings and internal numeric IDs.
 */
class InfosetRegistry {
public:
    InfosetId intern(const InfosetKey& key, std::size_t action_count);
    [[nodiscard]] std::optional<InfosetId> find(const InfosetKey& key) const;
    [[nodiscard]] const InfosetKey& key_for(InfosetId id) const;
    [[nodiscard]] const InfosetMeta& meta_for(InfosetId id) const;
    [[nodiscard]] const std::vector<InfosetKey>& keys() const noexcept;
    [[nodiscard]] std::size_t size() const noexcept;
    [[nodiscard]] bool empty() const noexcept;
    void clear() noexcept;

private:
    std::unordered_map<InfosetKey, InfosetId> key_to_id_;
    std::vector<InfosetKey> id_to_key_;
    std::vector<InfosetMeta> meta_;
};

}  // namespace core
