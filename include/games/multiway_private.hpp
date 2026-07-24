#pragma once

#include "games/multiway_terminal.hpp"

#include <array>
#include <cstdint>
#include <vector>

namespace core {

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
    std::uint32_t max_rejection_attempts = 4096;

    void validate() const;
};

struct MultiwayJointPrivateSample {
    std::vector<std::array<std::uint8_t, 2>> holes;
    std::uint32_t attempts = 0;
    // The independent range product, the probability conditional on a
    // compatible deal, the proposal probability including bounded-retry
    // exhaustion, and the compatible-deal inclusion probability. Traversal
    // must consume these values directly.
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

    // Allocation-free worker path. On success, scratch contains the exact
    // chance/proposal/inclusion reaches for the accepted deal. False means the
    // deterministic rejection budget was exhausted; scratch records one
    // discarded trajectory and its rejected-attempt count.
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
    double compatible_deal_mass_ = 0.0;
    double accepted_trajectory_probability_ = 0.0;
    std::uint32_t max_rejection_attempts_ = 0;
};

// Samples independent per-seat ranges and rejects colliding deals. Accepted
// samples expose the product chance reach and its exact probability under the
// collision-conditioned proposal; callers must not recompute either value.
MultiwayJointPrivateSample sample_multiway_private_hands(
    const MultiwayPrivateConfig& config,
    std::uint64_t seed);

struct MultiwayShowdownInput {
    std::vector<std::uint8_t> board;
    std::vector<std::array<std::uint8_t, 2>> holes;
    std::vector<int> contributions;
    std::vector<bool> folded;
    PlayerId odd_chip_first_seat = 0;

    void validate() const;
};

// Evaluates all surviving seats' seven-card hands then delegates pot and
// utility settlement to the precomputed multiway terminal layer.
MultiwayTerminalResult evaluate_multiway_showdown(const MultiwayShowdownInput& input);

}  // namespace core
