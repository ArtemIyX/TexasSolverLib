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

std::vector<std::array<std::uint8_t, 2>> enumerate_live_hands(const std::vector<std::uint8_t>& board) {
    std::array<bool, 64> blocked = {};
    for (const auto card : board) {
        blocked[card] = true;
    }

    std::vector<std::uint8_t> live_cards;
    live_cards.reserve(52);
    for (std::uint8_t rank = 2; rank <= 14; ++rank) {
        for (std::uint8_t suit = 0; suit < 4; ++suit) {
            const auto card = card_to_int(rank, suit);
            if (!blocked[card]) {
                live_cards.push_back(card);
            }
        }
    }

    std::vector<std::array<std::uint8_t, 2>> hands;
    for (std::size_t i = 0; i < live_cards.size(); ++i) {
        for (std::size_t j = i + 1; j < live_cards.size(); ++j) {
            std::array<std::uint8_t, 2> hole = {live_cards[i], live_cards[j]};
            if (hole[1] < hole[0]) {
                std::swap(hole[0], hole[1]);
            }
            hands.push_back(hole);
        }
    }
    return hands;
}

std::vector<double> normalized_bucket_weights(const std::vector<std::uint32_t>& bucket_hand_counts) {
    std::vector<double> weights(bucket_hand_counts.size(), 0.0);
    double total = 0.0;
    for (const auto count : bucket_hand_counts) {
        total += static_cast<double>(count);
    }
    if (total <= 0.0) {
        return weights;
    }
    for (std::size_t i = 0; i < bucket_hand_counts.size(); ++i) {
        weights[i] = static_cast<double>(bucket_hand_counts[i]) / total;
    }
    return weights;
}

bool is_hand_live_on_board(
    const std::vector<std::uint8_t>& board,
    const std::array<std::uint8_t, 2>& hole) {
    if (hole[0] == hole[1]) {
        return false;
    }
    for (const auto card : board) {
        if (card == hole[0] || card == hole[1]) {
            return false;
        }
    }
    return true;
}

std::vector<double> normalize_weights(std::vector<double> weights) {
    double total = 0.0;
    for (const auto weight : weights) {
        if (weight < 0.0) {
            throw std::invalid_argument("HUNLFlatBucketMap weights must be non-negative");
        }
        total += weight;
    }
    if (total > 0.0) {
        for (auto& weight : weights) {
            weight /= total;
        }
    }
    return weights;
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
        entry.bucket_hand_counts.assign(entry.bucket_count, 0U);
        for (std::uint32_t bucket = 0; bucket < entry.bucket_count; ++bucket) {
            entry.dense_bucket_ids.push_back(bucket);
        }
        for (const auto& hole : enumerate_live_hands(infoset.board)) {
            const auto bucket = map.lookup_bucket(infoset.id, hole);
            if (bucket < 0 || static_cast<std::size_t>(bucket) >= entry.bucket_hand_counts.size()) {
                continue;
            }
            ++entry.bucket_hand_counts[static_cast<std::size_t>(bucket)];
        }
        entry.bucket_weights = normalized_bucket_weights(entry.bucket_hand_counts);

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

std::uint32_t HUNLFlatBucketMap::bucket_hand_count(InfosetId infoset_id, std::size_t bucket_idx) const {
    const auto& bucket_entry = entry(infoset_id);
    if (bucket_idx >= bucket_entry.bucket_hand_counts.size()) {
        throw std::out_of_range("HUNLFlatBucketMap bucket_idx out of range");
    }
    return bucket_entry.bucket_hand_counts[bucket_idx];
}

double HUNLFlatBucketMap::bucket_weight(InfosetId infoset_id, std::size_t bucket_idx) const {
    const auto& bucket_entry = entry(infoset_id);
    if (bucket_idx >= bucket_entry.bucket_weights.size()) {
        throw std::out_of_range("HUNLFlatBucketMap bucket_idx out of range");
    }
    return bucket_entry.bucket_weights[bucket_idx];
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
    bucket_entry.bucket_weights = normalize_weights(std::move(weights));
}

void HUNLFlatBucketMap::apply_range_inputs(
    const HUNLFlatSolveGraph& graph,
    const std::array<std::optional<HUNLRangeInput>, 2>& player_ranges) {
    if (entries_.size() != graph.infosets.size()) {
        throw std::invalid_argument("HUNLFlatBucketMap graph/entry size mismatch");
    }

    for (const auto& infoset : graph.infosets) {
        if (infoset.id.value >= entries_.size() || !entries_[infoset.id.value].has_value()) {
            continue;
        }
        if (infoset.player < 0 || infoset.player > 1) {
            continue;
        }

        const auto& range_input = player_ranges[static_cast<std::size_t>(infoset.player)];
        if (!range_input.has_value()) {
            continue;
        }

        auto weights = std::vector<double>(entry(infoset.id).bucket_count, 0.0);

        for (const auto& weighted_bucket : range_input->bucket_weights) {
            if (weighted_bucket.street != infoset.street) {
                continue;
            }
            if (weighted_bucket.bucket >= weights.size()) {
                throw std::invalid_argument("HUNLFlatBucketMap bucket range references invalid bucket");
            }
            weights[weighted_bucket.bucket] += weighted_bucket.weight;
        }

        for (const auto& weighted_hand : range_input->hand_weights) {
            auto hole = weighted_hand.hole;
            if (hole[1] < hole[0]) {
                std::swap(hole[0], hole[1]);
            }
            if (!is_hand_live_on_board(infoset.board, hole)) {
                continue;
            }
            const auto bucket = lookup_bucket(infoset.id, hole);
            if (bucket < 0 || static_cast<std::size_t>(bucket) >= weights.size()) {
                continue;
            }
            weights[static_cast<std::size_t>(bucket)] += weighted_hand.weight;
        }

        set_bucket_weights(infoset.id, std::move(weights));
    }
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
