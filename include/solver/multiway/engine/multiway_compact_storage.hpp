#pragma once

#include "solver/multiway/engine/multiway_solver.hpp"

#include <cstdint>
#include <vector>

namespace texas::solver::multiway {

// Bounded production accumulator. Regrets use signed fixed-point int32 cells;
// strategy mass uses unsigned fixed-point uint64 cells. Rows remain lazy and
// action-major. MultiwaySparseRowStorage is the Float64 reference backend.
class MultiwayCompactStorage {
public:
    static constexpr std::int32_t kRegretMin = -2'000'000'000;
    static constexpr std::int32_t kRegretMax = 2'000'000'000;
    static constexpr std::uint64_t kMassScale = 1ULL << 20U;

    MultiwayCompactStorage(std::size_t max_rows, std::size_t max_values);

    void admit_row(const MultiwaySparseRowShape& shape);
    [[nodiscard]] bool has_row(MultiwayInfosetId infoset) const noexcept;
    [[nodiscard]] const MultiwaySparseRowMetadata* metadata(MultiwayInfosetId infoset) const noexcept;
    void apply_delta(MultiwayInfosetId infoset, std::uint32_t bucket,
        std::uint8_t action, double regret, double strategy_sum);
    void scale_regrets(double factor);
    [[nodiscard]] std::size_t prune_negative_regrets(
        double threshold = 0.0, double regret_floor = 0.0) noexcept;
    [[nodiscard]] std::vector<Probability> regret_matched_strategy(
        MultiwayInfosetId infoset, std::uint32_t bucket) const;
    [[nodiscard]] std::vector<Probability> average_strategy(
        MultiwayInfosetId infoset, std::uint32_t bucket) const;
    [[nodiscard]] bool action_below_regret(
        MultiwayInfosetId infoset, std::uint32_t bucket, std::uint8_t action,
        double threshold) const noexcept;
    [[nodiscard]] std::vector<double> strategy_sums(
        MultiwayInfosetId infoset, std::uint32_t bucket) const;
    [[nodiscard]] const std::vector<MultiwaySparseRowMetadata>& rows() const noexcept { return rows_; }
    void export_checkpoint(MultiwaySparseStorageCheckpoint& output) const;
    [[nodiscard]] std::size_t row_count() const noexcept { return rows_.size(); }
    [[nodiscard]] std::size_t value_count() const noexcept { return regrets_.size(); }
    [[nodiscard]] std::size_t memory_bytes() const noexcept;

private:
    std::size_t max_rows_ = 0;
    std::size_t max_values_ = 0;
    std::vector<MultiwaySparseRowMetadata> rows_;
    std::vector<std::int32_t> regrets_;
    std::vector<std::uint64_t> strategy_mass_;
};

}  // namespace texas::solver::multiway
