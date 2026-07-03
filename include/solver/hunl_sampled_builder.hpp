#pragma once

#include "games/hunl_flat_graph.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <unordered_map>
#include <vector>

namespace core {

struct HUNLSampledStateKey {
    std::uint64_t value = 0;

    constexpr bool operator==(const HUNLSampledStateKey& other) const noexcept {
        return value == other.value;
    }
};

struct HUNLSampledStateKeyHash {
    std::size_t operator()(const HUNLSampledStateKey& key) const noexcept {
        return std::hash<std::uint64_t>{}(key.value);
    }
};

struct HUNLSampledNode {
    HUNLFlatNodeType type = HUNLFlatNodeType::Decision;
    HUNLSampledStateKey key{};
    InfosetId infoset_id{};
    HUNLFlatPackedBoard board{};
    std::array<int, 2> contributions = {0, 0};
    PlayerId player = -1;
    Street street = Street::Preflop;
    std::uint32_t first_child = 0;
    std::uint16_t child_count = 0;
    bool expanded = false;
};

class HUNLSampledBuilder {
public:
    [[nodiscard]] std::uint32_t find_or_create(const HUNLSampledStateKey& key);
    [[nodiscard]] const HUNLSampledNode& node(std::uint32_t node_id) const;
    [[nodiscard]] HUNLSampledNode& node_mut(std::uint32_t node_id);
    [[nodiscard]] std::size_t node_count() const noexcept;

private:
    std::unordered_map<HUNLSampledStateKey, std::uint32_t, HUNLSampledStateKeyHash> node_lookup_;
    std::vector<HUNLSampledNode> nodes_;
};

}  // namespace core
