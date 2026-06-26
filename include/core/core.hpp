#pragma once

#include <cstddef>
#include <cstdint>
#include <array>
#include <optional>
#include <unordered_map>
#include <string>
#include <utility>
#include <vector>

namespace core {

using PlayerId = int;
using ActionId = int;
using InfosetKey = std::string;
using Probability = double;
using Value = double;

struct ChanceOutcome {
    ActionId action{};
    Probability probability{};
};

struct SolveOutput {
    std::vector<std::pair<InfosetKey, std::vector<Probability>>> average_strategy;
    Value exploitability = 0.0;
    Value game_value = 0.0;
    std::uint32_t iterations = 0;
};

}  // namespace core
