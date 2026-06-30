#include "solver/hunl_bucket_map.hpp"

#include <stdexcept>
#include <utility>

namespace core {

namespace {

std::size_t street_bucket_index(Street street) {
    switch (street) {
        case Street::Flop:
            return 0;
        case Street::Turn:
            return 1;
        case Street::River:
            return 2;
        default:
            return 3;
    }
}

}  // namespace

HUNLFlatBucketMap HUNLFlatBucketMap::from_abstraction(
    const HUNLFlatSolveGraph& graph,
    AbstractionTables tables) {
    HUNLFlatBucketMap map;
    map.tables_ = std::move(tables);
    map.entries_.resize(graph.infosets.size());

    for (const auto& infoset : graph.infosets) {
        if (infoset.street < Street::Flop || infoset.street > Street::River) {
            continue;
        }

        HUNLFlatBucketEntry entry;
        entry.infoset_id = infoset.id;
        entry.street = infoset.street;
        entry.board = infoset.board;
        entry.canonical_board = canonicalize_board(infoset.board);
        entry.bucket_count = bucket_count_for_street(map.tables_, infoset.street);
        entry.dense_bucket_ids.reserve(entry.bucket_count);
        for (std::uint32_t bucket = 0; bucket < entry.bucket_count; ++bucket) {
            entry.dense_bucket_ids.push_back(bucket);
        }

        map.entries_[infoset.id.value] = std::move(entry);
    }

    return map;
}

bool HUNLFlatBucketMap::empty() const noexcept {
    return entries_.empty();
}

const AbstractionTables& HUNLFlatBucketMap::abstraction() const noexcept {
    return tables_;
}

const HUNLFlatBucketEntry& HUNLFlatBucketMap::entry(InfosetId infoset_id) const {
    if (infoset_id.value >= entries_.size() || !entries_[infoset_id.value].has_value()) {
        throw std::out_of_range("HUNLFlatBucketMap missing infoset bucket entry");
    }
    return *entries_[infoset_id.value];
}

std::int32_t HUNLFlatBucketMap::lookup_bucket(
    InfosetId infoset_id,
    const std::array<std::uint8_t, 2>& hole) const {
    const auto& bucket_entry = entry(infoset_id);
    return core::lookup_bucket(tables_, bucket_entry.board, hole, bucket_entry.street);
}

std::uint32_t HUNLFlatBucketMap::bucket_count(InfosetId infoset_id) const {
    return entry(infoset_id).bucket_count;
}

const std::vector<std::uint32_t>& HUNLFlatBucketMap::dense_bucket_ids(InfosetId infoset_id) const {
    return entry(infoset_id).dense_bucket_ids;
}

const std::vector<double>* HUNLFlatBucketMap::bucket_weights(InfosetId infoset_id) const {
    const auto& bucket_entry = entry(infoset_id);
    if (bucket_entry.bucket_weights.empty()) {
        return nullptr;
    }
    return &bucket_entry.bucket_weights;
}

void HUNLFlatBucketMap::set_bucket_weights(InfosetId infoset_id, std::vector<double> weights) {
    auto& bucket_entry = entry_mut(infoset_id);
    if (!weights.empty() && weights.size() != bucket_entry.bucket_count) {
        throw std::invalid_argument("HUNLFlatBucketMap weight count must match bucket_count");
    }
    bucket_entry.bucket_weights = std::move(weights);
}

std::uint32_t HUNLFlatBucketMap::bucket_count_for_street(const AbstractionTables& tables, Street street) {
    const auto index = street_bucket_index(street);
    if (index >= tables.metadata.bucket_counts.size()) {
        throw std::out_of_range("HUNLFlatBucketMap metadata missing bucket count for street");
    }
    return tables.metadata.bucket_counts[index];
}

HUNLFlatBucketEntry& HUNLFlatBucketMap::entry_mut(InfosetId infoset_id) {
    if (infoset_id.value >= entries_.size() || !entries_[infoset_id.value].has_value()) {
        throw std::out_of_range("HUNLFlatBucketMap missing infoset bucket entry");
    }
    return *entries_[infoset_id.value];
}

}  // namespace core
