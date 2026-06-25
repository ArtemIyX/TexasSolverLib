#pragma once

#include "core/hunl.hpp"

#include <array>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace core {

struct VectorInfosetData {
    std::size_t action_count = 0;
    std::size_t hand_count = 0;
    std::vector<double> regret;
    std::vector<double> strategy_sum;
    std::uint32_t last_discount_iter = 0;

    VectorInfosetData() = default;
    VectorInfosetData(std::size_t action_count, std::size_t hand_count);
};

struct VectorMemoryProfile {
    std::uint64_t total_bytes = 0;
    std::unordered_map<std::string, std::uint64_t> by_street;
    std::uint32_t infoset_count = 0;
    std::unordered_map<std::string, std::uint32_t> infoset_count_by_street;
    std::array<std::size_t, 2> hand_count = {0, 0};
};

struct VectorSolveOutput {
    std::unordered_map<std::string, std::vector<double>> average_strategy;
    std::uint32_t decision_node_count = 0;
    std::uint32_t strategy_entry_count = 0;
    std::uint32_t iterations = 0;
    std::array<std::size_t, 2> hand_count_per_player = {0, 0};
    VectorMemoryProfile memory_profile;
};

struct EvalContext {
    std::array<std::size_t, 2> hand_count = {0, 0};
    std::array<std::vector<std::array<std::uint8_t, 2>>, 2> hole;
    std::array<std::vector<std::string>, 2> hole_str;
    int big_blind = 0;

    static EvalContext from_root(const HUNLState& initial);
    static EvalContext from_suit_iso(const HUNLState& initial);
    static EvalContext from_hand_lists(
        std::vector<std::array<std::uint8_t, 2>> p0_holes,
        std::vector<std::array<std::uint8_t, 2>> p1_holes,
        int big_blind);
};

}  // namespace core
