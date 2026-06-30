#pragma once

#include "games/hunl_flat_graph.hpp"
#include "util/abstraction.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace core {

struct HUNLFlatBucketEntry {
    InfosetId infoset_id{};
    Street street = Street::Preflop;
    std::vector<std::uint8_t> board;
    std::string canonical_board;
    std::uint32_t bucket_count = 0;
    std::vector<std::uint32_t> dense_bucket_ids;
    std::vector<std::uint32_t> bucket_hand_counts;
    std::vector<double> bucket_weights;
};

class HUNLFlatBucketMap {
public:
    static HUNLFlatBucketMap from_abstraction(
        const HUNLFlatSolveGraph& graph,
        AbstractionTables tables);

    [[nodiscard]] bool empty() const noexcept;
    [[nodiscard]] const AbstractionTables& abstraction() const noexcept;
    [[nodiscard]] const HUNLFlatBucketEntry& entry(InfosetId infoset_id) const;
    [[nodiscard]] std::int32_t lookup_bucket(InfosetId infoset_id, const std::array<std::uint8_t, 2>& hole) const;
    [[nodiscard]] std::uint32_t bucket_count(InfosetId infoset_id) const;
    [[nodiscard]] const std::vector<std::uint32_t>& dense_bucket_ids(InfosetId infoset_id) const;
    [[nodiscard]] std::uint32_t bucket_hand_count(InfosetId infoset_id, std::size_t bucket_idx) const;
    [[nodiscard]] double bucket_weight(InfosetId infoset_id, std::size_t bucket_idx) const;
    [[nodiscard]] const std::vector<double>* bucket_weights(InfosetId infoset_id) const;

    void set_bucket_weights(InfosetId infoset_id, std::vector<double> weights);

private:
    static std::uint32_t bucket_count_for_street(const AbstractionTables& tables, Street street);
    HUNLFlatBucketEntry& entry_mut(InfosetId infoset_id);

    AbstractionTables tables_;
    std::vector<std::optional<HUNLFlatBucketEntry>> entries_;
};

}  // namespace core
