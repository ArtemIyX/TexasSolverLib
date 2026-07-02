#pragma once

#include "games/hunl_flat_graph.hpp"

#include <array>
#include <cstdint>
#include <memory>
#include <unordered_map>

namespace core {

struct HUNLFlatBuilderMemoKey {
    PlayerId cur_player = -1;
    std::array<int, 2> contributions = {0, 0};
    std::array<int, 2> stacks = {0, 0};
    std::array<bool, 2> folded = {false, false};
    std::array<bool, 2> all_in = {false, false};
    std::array<std::uint8_t, 5> board = {0, 0, 0, 0, 0};
    std::array<std::array<std::uint8_t, 2>, 2> hole_cards = {{
        {0, 0},
        {0, 0},
    }};
    std::array<std::uint8_t, 4> street_lengths = {0, 0, 0, 0};
    std::array<int, HUNL_MAX_HISTORY_CODES> history_codes = {};
    Street street = Street::Preflop;
    std::uint8_t board_count = 0;
    std::uint8_t history_count = 0;
    std::uint8_t pending_board_deals = 0;
    std::uint8_t street_num_raises = 0;
    bool has_hole_cards = false;
    int to_call = 0;
    PlayerId street_aggressor = -1;

    static HUNLFlatBuilderMemoKey from_state(const HUNLState& state);

    bool operator==(const HUNLFlatBuilderMemoKey& other) const noexcept;
};

class HUNLFlatBuilder {
public:
    static HUNLFlatSolveGraph build(std::shared_ptr<const HUNLConfig> config);
};

}  // namespace core

namespace std {

template <>
struct hash<core::HUNLFlatBuilderMemoKey> {
    std::size_t operator()(const core::HUNLFlatBuilderMemoKey& key) const noexcept;
};

}  // namespace std
