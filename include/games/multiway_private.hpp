#pragma once

#include "core/namespaces.hpp"

#include "games/multiway_terminal.hpp"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace texas::games::multiway {

namespace detail {

// Keeps the public uint32 attempt domain bounded even when the configured
// limit is UINT32_MAX. The 64-bit cursor is also directly boundary-testable.
inline bool next_multiway_rejection_attempt(
    std::uint64_t& cursor,
    std::uint32_t limit,
    std::uint32_t& attempt) noexcept {
    if (cursor >= static_cast<std::uint64_t>(limit)) return false;
    ++cursor;
    attempt = static_cast<std::uint32_t>(cursor);
    return true;
}

}  // namespace detail

struct MultiwayWeightedHole {
    std::array<std::uint8_t, 2> hole = {0, 0};
    double weight = 0.0;
};

struct MultiwayPrivateConfig {
    std::vector<std::uint8_t> board;
    std::vector<std::vector<MultiwayWeightedHole>> ranges;
    // Retained for source compatibility. Compiled sampling makes one proposal
    // per trajectory and never retries.
    std::uint32_t max_rejection_attempts = 1;

    void validate() const;
};

enum class MultiwayPrivateRangeFeasibilityStatus : std::uint8_t {
    Feasible,
    Infeasible,
    SearchBudgetExhausted,
};

// Coordinator-only preflight for a compiled private-range request. This
// bounded search must complete before workers begin trajectory sampling.
struct MultiwayPrivateRangeFeasibilityResult {
    MultiwayPrivateRangeFeasibilityStatus status = MultiwayPrivateRangeFeasibilityStatus::Infeasible;
    std::uint64_t visited_nodes = 0;
    std::uint64_t node_budget = 0;
    std::string reason;
};

MultiwayPrivateRangeFeasibilityResult preflight_multiway_private_range_feasibility(
    const MultiwayPrivateConfig& config,
    std::uint64_t node_budget = 1'000'000U);

struct MultiwayJointPrivateSample {
    std::vector<std::array<std::uint8_t, 2>> holes;
    std::uint32_t attempts = 0;
    // One independent proposal is made per trajectory. Compatible samples
    // retain their independent range-product probability; collisions discard
    // the trajectory. No global compatible-deal normalization is used.
    double chance_reach = 0.0;
    double conditional_deal_probability = 0.0;
    double proposal_reach = 0.0;
    double inclusion_reach = 0.0;
    std::uint32_t accepted_trajectories = 0;
    std::uint32_t rejected_trajectories = 0;
    std::uint32_t discarded_trajectories = 0;
};

struct MultiwayPrivateWorkerScratch {
    std::array<std::array<std::uint8_t, 2>, 6> holes = {};
    std::array<bool, 64> used = {};
    std::uint8_t seat_count = 0;
    std::uint32_t attempts = 0;
    double chance_reach = 0.0;
    double conditional_deal_probability = 0.0;
    double proposal_reach = 0.0;
    double inclusion_reach = 0.0;
    std::uint32_t accepted_trajectories = 0;
    std::uint32_t rejected_trajectories = 0;
    std::uint32_t discarded_trajectories = 0;
};

// Immutable, canonicalized range tables for traversal workers.  Reversed and
// duplicate hole-card entries are merged at compile time; cumulative weights
// make each draw allocation-free.
class MultiwayCompiledPrivateRanges {
public:
    explicit MultiwayCompiledPrivateRanges(const MultiwayPrivateConfig& config);

    // Allocation-free worker path. Exactly one independent proposal is made
    // per trajectory. False records a discarded collision.
    [[nodiscard]] bool try_sample_into(
        std::uint64_t seed,
        MultiwayPrivateWorkerScratch& scratch) const noexcept;
    void sample_into(std::uint64_t seed, MultiwayPrivateWorkerScratch& scratch) const;
    [[nodiscard]] std::size_t seat_count() const noexcept;

private:
    std::vector<std::uint8_t> board_;
    std::vector<std::vector<MultiwayWeightedHole>> ranges_;
    std::vector<std::vector<double>> cumulative_weights_;
    std::vector<double> range_totals_;
};

// Samples one independent per-seat proposal and discards colliding deals.
// Accepted samples expose their product proposal probability directly.
MultiwayJointPrivateSample sample_multiway_private_hands(
    const MultiwayPrivateConfig& config,
    std::uint64_t seed);

struct MultiwayShowdownInput {
    std::vector<std::uint8_t> board;
    std::vector<std::array<std::uint8_t, 2>> holes;
    std::vector<int> contributions;
    std::vector<bool> folded;
    PlayerId odd_chip_first_seat = 0;
    MultiwayRakePolicy rake_policy = MultiwayRakePolicy::explicit_zero();
    bool flop_seen = true;

    void validate() const;
};

// Evaluates all surviving seats' seven-card hands then delegates pot and
// utility settlement to the precomputed multiway terminal layer.
MultiwayTerminalResult evaluate_multiway_showdown(const MultiwayShowdownInput& input);

}  // namespace texas::games::multiway
