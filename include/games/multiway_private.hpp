#pragma once

#include "games/multiway_terminal.hpp"

#include <array>
#include <cstdint>
#include <vector>

namespace core {

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
};

struct MultiwayPrivateWorkerScratch {
    std::array<std::array<std::uint8_t, 2>, 6> holes = {};
    std::array<bool, 64> used = {};
    std::uint8_t seat_count = 0;
    std::uint32_t attempts = 0;
};

// Immutable, canonicalized range tables for traversal workers.  Reversed and
// duplicate hole-card entries are merged at compile time; cumulative weights
// make each draw allocation-free.
class MultiwayCompiledPrivateRanges {
public:
    explicit MultiwayCompiledPrivateRanges(const MultiwayPrivateConfig& config);

    void sample_into(std::uint64_t seed, MultiwayPrivateWorkerScratch& scratch) const;
    [[nodiscard]] std::size_t seat_count() const noexcept;

private:
    std::vector<std::uint8_t> board_;
    std::vector<std::vector<MultiwayWeightedHole>> ranges_;
    std::vector<std::vector<double>> cumulative_weights_;
    std::uint32_t max_rejection_attempts_ = 0;
};

// Samples independent per-seat ranges and rejects colliding deals. Accepted
// samples therefore follow the product range distribution conditioned on all
// cards being distinct, without materializing a Cartesian product.
MultiwayJointPrivateSample sample_multiway_private_hands(
    const MultiwayPrivateConfig& config,
    std::uint64_t seed);

struct MultiwayShowdownInput {
    std::vector<std::uint8_t> board;
    std::vector<std::array<std::uint8_t, 2>> holes;
    std::vector<int> contributions;
    std::vector<bool> folded;

    void validate() const;
};

// Evaluates all surviving seats' seven-card hands then delegates pot and
// utility settlement to the precomputed multiway terminal layer.
MultiwayTerminalResult evaluate_multiway_showdown(const MultiwayShowdownInput& input);

}  // namespace core
