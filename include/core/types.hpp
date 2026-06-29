#pragma once

#include <cstddef>
#include <cstdint>
#include <array>
#include <optional>
#include <functional>
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
 * @brief Internal numeric infoset identifier used by solver internals.
 */
struct InfosetId {
    std::uint32_t value = 0;

    constexpr bool operator==(const InfosetId& other) const noexcept {
        return value == other.value;
    }

    constexpr bool operator!=(const InfosetId& other) const noexcept {
        return value != other.value;
    }

    constexpr bool operator<(const InfosetId& other) const noexcept {
        return value < other.value;
    }
};
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

struct WorkerProfile {
    double cfr_seconds = 0.0;
    std::uint64_t batches_taken = 0;
    std::uint64_t seeds_processed = 0;
    std::uint64_t infoset_count = 0;
};

struct SolveProfile {
    bool enabled = false;
    double snapshot_seconds = 0.0;
    double merge_seconds = 0.0;
    double frontier_seconds = 0.0;
    double batch_build_seconds = 0.0;
    std::uint64_t frontier_seed_count = 0;
    std::uint64_t batch_count = 0;
    std::vector<WorkerProfile> workers;
};

/**
 * @brief Summary returned by solver entrypoints.
 */
struct SolveOutput {
    std::vector<std::pair<InfosetKey, std::vector<Probability>>> average_strategy;
    Value exploitability = 0.0;
    Value game_value = 0.0;
    std::uint32_t iterations = 0;
    double traversal_seconds = 0.0;
    double finalize_seconds = 0.0;
    bool used_parallel = false;
    SolveProfile profile;
};

}  // namespace core

template <>
struct std::hash<core::InfosetId> {
    std::size_t operator()(const core::InfosetId& id) const noexcept {
        return std::hash<std::uint32_t>{}(id.value);
    }
};


