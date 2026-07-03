#pragma once

#include "games/hunl_flat_graph.hpp"
#include "solver/hunl_flat_expected_value.hpp"
#include "solver/hunl_sampled_config.hpp"
#include "util/pcs.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <unordered_map>
#include <vector>

namespace core {

class HUNLFlatMCCFR {
public:
    explicit HUNLFlatMCCFR(
        HUNLFlatSolveGraph graph,
        std::array<std::size_t, 2> bucket_count_per_player,
        HUNLFlatMCCFRConfig config = {},
        HUNLFlatValueLayout layout = HUNLFlatValueLayout::InfosetActionHand,
        std::size_t workers = 1,
        HUNLFlatStoragePrecision precision = HUNLFlatStoragePrecision::Float64);

    void run_iteration();
    void run_iterations(std::uint32_t iterations);

    [[nodiscard]] const HUNLFlatSolveGraph& graph() const noexcept;
    [[nodiscard]] const HUNLFlatInfosetTable& infoset_table() const noexcept;
    [[nodiscard]] HUNLFlatInfosetTable& infoset_table_mut() noexcept;
    [[nodiscard]] std::uint32_t iterations() const noexcept;
    [[nodiscard]] const HUNLFlatMCCFRConfig& config() const noexcept;

    [[nodiscard]] std::unordered_map<std::string, std::vector<double>> export_average_strategy() const;
    [[nodiscard]] HUNLFlatAverageStrategyTable export_average_strategy_table() const;

private:
    struct TraversalContext {
        PlayerId traversing_player = 0;
        double p0 = 1.0;
        double p1 = 1.0;
        PcsRng* rng = nullptr;
    };

    [[nodiscard]] double traverse(std::uint32_t node_idx, TraversalContext& context);
    void update_current_strategy_row(InfosetId infoset_id);
    [[nodiscard]] double bucket_strategy_probability(
        InfosetId infoset_id,
        std::size_t bucket,
        std::size_t action) const;
    [[nodiscard]] std::vector<double> average_action_probabilities(InfosetId infoset_id) const;
    [[nodiscard]] std::uint32_t sample_chance_child(const HUNLFlatNodeMeta& meta, PcsRng& rng) const;

    HUNLFlatSolveGraph graph_;
    HUNLFlatInfosetTable infoset_table_;
    HUNLFlatMCCFRConfig config_;
    std::uint32_t iterations_ = 0;
};

}  // namespace core
