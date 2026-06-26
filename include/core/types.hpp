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

/**
 * @brief Player index used by game-state interfaces.
 *
 * The engine uses `-1` to mean chance / no current player.
 */
using PlayerId = int;
/**
 * @brief Action identifier used by game-state transitions.
 */
using ActionId = int;
/**
 * @brief Infoset lookup key.
 */
using InfosetKey = std::string;
/**
 * @brief Probability mass for strategy vectors and chance outcomes.
 */
using Probability = double;
/**
 * @brief Utility / EV value in solver output.
 */
using Value = double;

/**
 * @brief Chance action paired with a probability.
 */
struct ChanceOutcome {
    ActionId action{};
    Probability probability{};
};

/**
 * @brief Summary returned by solver entrypoints.
 */
struct SolveOutput {
    std::vector<std::pair<InfosetKey, std::vector<Probability>>> average_strategy;
    Value exploitability = 0.0;
    Value game_value = 0.0;
    std::uint32_t iterations = 0;
};

}  // namespace core


