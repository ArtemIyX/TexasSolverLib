#include "solver/hunl_bucket_terminal.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <stdexcept>
#include <unordered_map>

namespace core {

namespace {

double board_texture_score(const std::vector<std::uint8_t>& board) {
    if (board.empty()) {
        return 0.5;
    }

    std::array<std::uint8_t, 15> rank_counts = {};
    std::array<std::uint8_t, 4> suit_counts = {};
    for (const auto card : board) {
        ++rank_counts[rank_of(card)];
        ++suit_counts[suit_of(card)];
    }

    double score = 0.5;
    for (std::uint8_t rank = 2; rank <= 14; ++rank) {
        if (rank_counts[rank] >= 2) {
            score += 0.05 * static_cast<double>(rank_counts[rank] - 1);
        }
        if (rank == 14) {
            break;
        }
    }
    for (const auto suit_count : suit_counts) {
        if (suit_count >= 3) {
            score += 0.03 * static_cast<double>(suit_count - 2);
        }
    }

    if (board.size() >= 3) {
        std::vector<std::uint8_t> ranks;
        ranks.reserve(board.size());
        for (const auto card : board) {
            ranks.push_back(rank_of(card));
        }
        std::sort(ranks.begin(), ranks.end());
        std::size_t run = 1;
        std::size_t best_run = 1;
        for (std::size_t i = 1; i < ranks.size(); ++i) {
            if (ranks[i] == ranks[i - 1] || ranks[i] == static_cast<std::uint8_t>(ranks[i - 1] + 1)) {
                ++run;
                best_run = std::max(best_run, run);
            } else {
                run = 1;
            }
        }
        if (best_run >= 3) {
            score += 0.02 * static_cast<double>(best_run - 2);
        }
    }

    return std::clamp(score, 0.0, 1.0);
}

std::vector<std::array<std::uint8_t, 2>> enumerate_live_hands_for_board(const std::vector<std::uint8_t>& board) {
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
    hands.reserve((live_cards.size() * (live_cards.size() - 1U)) / 2U);
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

bool overlaps(const std::array<std::uint8_t, 2>& lhs, const std::array<std::uint8_t, 2>& rhs) {
    return lhs[0] == rhs[0] || lhs[0] == rhs[1] || lhs[1] == rhs[0] || lhs[1] == rhs[1];
}

std::vector<double> make_uniform_weights(std::uint32_t count) {
    if (count == 0) {
        return {};
    }
    return std::vector<double>(count, 1.0 / static_cast<double>(count));
}

std::size_t matrix_index(std::uint32_t bucket_count_p1, std::size_t bucket0, std::size_t bucket1) {
    return bucket0 * static_cast<std::size_t>(bucket_count_p1) + bucket1;
}

double showdown_bucket_pair_value_p0(
    std::uint32_t valid_pairs,
    std::int32_t net_wins,
    std::uint32_t ties,
    const std::array<int, 2>& contributions,
    int initial_pot,
    const std::array<int, 2>& initial_contributions,
    int big_blind) {
    if (valid_pairs == 0 || big_blind <= 0) {
        return 0.0;
    }

    const auto wins_times_two =
        static_cast<std::int64_t>(valid_pairs) + static_cast<std::int64_t>(net_wins) -
        static_cast<std::int64_t>(ties);
    const auto wins = static_cast<double>(wins_times_two) * 0.5;
    const auto losses = static_cast<double>(valid_pairs) - wins - static_cast<double>(ties);

    const double bb = static_cast<double>(big_blind);
    const double init_c0 = static_cast<double>(initial_contributions[0]);
    const double init_c1 = static_cast<double>(initial_contributions[1]);
    const double cs0 = static_cast<double>(contributions[0]) - init_c0;
    const double cs1 = static_cast<double>(contributions[1]) - init_c1;
    const double pot_total = static_cast<double>(initial_pot) + cs0 + cs1;

    const double win_value = (pot_total - cs0) / bb;
    const double lose_value = -cs0 / bb;
    const double tie_value = (pot_total / 2.0 - cs0) / bb;

    return (
               wins * win_value +
               losses * lose_value +
               static_cast<double>(ties) * tie_value) /
        static_cast<double>(valid_pairs);
}

std::vector<std::uint32_t> build_bucket_hand_counts(
    const AbstractionTables& abstraction,
    const std::vector<std::uint8_t>& board,
    std::uint32_t bucket_count) {
    std::vector<std::uint32_t> counts(bucket_count, 0U);
    for (const auto& hole : enumerate_live_hands_for_board(board)) {
        const auto bucket = lookup_bucket(abstraction, board, hole, Street::River);
        if (bucket < 0 || static_cast<std::size_t>(bucket) >= counts.size()) {
            continue;
        }
        ++counts[static_cast<std::size_t>(bucket)];
    }
    return counts;
}

HUNLBucketShowdownMatrix build_showdown_cache_entry(
    const std::vector<std::uint8_t>& board,
    const AbstractionTables& abstraction,
    std::uint32_t bucket_count_p0,
    std::uint32_t bucket_count_p1) {
    HUNLBucketShowdownMatrix matrix;
    matrix.bucket_count_p0 = bucket_count_p0;
    matrix.bucket_count_p1 = bucket_count_p1;
    matrix.valid_pair_counts.assign(
        static_cast<std::size_t>(bucket_count_p0) * static_cast<std::size_t>(bucket_count_p1),
        0U);
    matrix.net_win_counts.assign(
        static_cast<std::size_t>(bucket_count_p0) * static_cast<std::size_t>(bucket_count_p1),
        0);
    matrix.tie_pair_counts.assign(
        static_cast<std::size_t>(bucket_count_p0) * static_cast<std::size_t>(bucket_count_p1),
        0U);
    matrix.bucket_hand_counts_p0 = build_bucket_hand_counts(abstraction, board, bucket_count_p0);
    matrix.bucket_hand_counts_p1 = matrix.bucket_hand_counts_p0;

    const auto live_hands = enumerate_live_hands_for_board(board);
    std::vector<std::int32_t> hand_buckets(live_hands.size(), -1);
    std::vector<std::array<std::uint8_t, 7>> seven_card_hands(live_hands.size());

    for (std::size_t i = 0; i < live_hands.size(); ++i) {
        hand_buckets[i] = lookup_bucket(abstraction, board, live_hands[i], Street::River);
        auto& seven = seven_card_hands[i];
        seven[0] = live_hands[i][0];
        seven[1] = live_hands[i][1];
        for (std::size_t board_idx = 0; board_idx < 5; ++board_idx) {
            seven[board_idx + 2] = board[board_idx];
        }
    }

    for (std::size_t i = 0; i < live_hands.size(); ++i) {
        const auto bucket0 = hand_buckets[i];
        if (bucket0 < 0 || static_cast<std::size_t>(bucket0) >= bucket_count_p0) {
            continue;
        }
        for (std::size_t j = 0; j < live_hands.size(); ++j) {
            if (i == j || overlaps(live_hands[i], live_hands[j])) {
                continue;
            }
            const auto bucket1 = hand_buckets[j];
            if (bucket1 < 0 || static_cast<std::size_t>(bucket1) >= bucket_count_p1) {
                continue;
            }

            const auto index =
                matrix_index(bucket_count_p1, static_cast<std::size_t>(bucket0), static_cast<std::size_t>(bucket1));
            ++matrix.valid_pair_counts[index];
            const auto result = compare_7(seven_card_hands[i], seven_card_hands[j]);
            if (result > 0) {
                ++matrix.net_win_counts[index];
            } else if (result < 0) {
                --matrix.net_win_counts[index];
            } else {
                ++matrix.tie_pair_counts[index];
            }
        }
    }

    return matrix;
}

}  // namespace

std::uint32_t HUNLBucketShowdownMatrix::valid_pair_count(std::size_t bucket0, std::size_t bucket1) const {
    if (bucket0 >= bucket_count_p0 || bucket1 >= bucket_count_p1) {
        throw std::out_of_range("HUNLBucketShowdownMatrix bucket index out of range");
    }
    return valid_pair_counts[matrix_index(bucket_count_p1, bucket0, bucket1)];
}

std::int32_t HUNLBucketShowdownMatrix::net_win_count(std::size_t bucket0, std::size_t bucket1) const {
    if (bucket0 >= bucket_count_p0 || bucket1 >= bucket_count_p1) {
        throw std::out_of_range("HUNLBucketShowdownMatrix bucket index out of range");
    }
    return net_win_counts[matrix_index(bucket_count_p1, bucket0, bucket1)];
}

std::uint32_t HUNLBucketShowdownMatrix::tie_pair_count(std::size_t bucket0, std::size_t bucket1) const {
    if (bucket0 >= bucket_count_p0 || bucket1 >= bucket_count_p1) {
        throw std::out_of_range("HUNLBucketShowdownMatrix bucket index out of range");
    }
    return tie_pair_counts[matrix_index(bucket_count_p1, bucket0, bucket1)];
}

std::uint64_t HUNLBucketShowdownMatrix::estimated_bytes() const noexcept {
    return sizeof(HUNLBucketShowdownMatrix) +
        static_cast<std::uint64_t>(valid_pair_counts.capacity()) * sizeof(std::uint32_t) +
        static_cast<std::uint64_t>(net_win_counts.capacity()) * sizeof(std::int32_t) +
        static_cast<std::uint64_t>(tie_pair_counts.capacity()) * sizeof(std::uint32_t) +
        static_cast<std::uint64_t>(bucket_hand_counts_p0.capacity()) * sizeof(std::uint32_t) +
        static_cast<std::uint64_t>(bucket_hand_counts_p1.capacity()) * sizeof(std::uint32_t);
}

bool HUNLBucketTerminalCacheKey::operator==(const HUNLBucketTerminalCacheKey& other) const noexcept {
    return board.cards == other.board.cards &&
        board.count == other.board.count &&
        bucket_count_p0 == other.bucket_count_p0 &&
        bucket_count_p1 == other.bucket_count_p1;
}

HUNLBucketTerminalTable HUNLBucketTerminalTable::build(
    const HUNLFlatSolveGraph& graph,
    const HUNLFlatBucketMap& bucket_map) {
    HUNLBucketTerminalTable table;
    if (!graph.config) {
        return table;
    }

    table.initial_contributions_ = graph.config->initial_contributions;
    table.initial_pot_ = graph.config->initial_pot;
    table.big_blind_ = graph.config->big_blind;

    std::unordered_map<HUNLBucketTerminalCacheKey, std::uint32_t> cache_indices;

    for (const auto node_idx : graph.showdown_terminal_nodes) {
        const auto& meta = graph.node_meta[node_idx];
        const auto board = graph.node_board(node_idx);
        if (board.size() != 5) {
            continue;
        }

        if (bucket_map.abstraction().metadata.bucket_counts.size() <= 2) {
            continue;
        }
        const auto bucket_count =
            static_cast<std::uint32_t>(bucket_map.abstraction().metadata.bucket_counts[2]);
        if (bucket_count == 0) {
            continue;
        }

        const HUNLBucketTerminalCacheKey cache_key{
            HUNLFlatSolveGraph::pack_board(board),
            bucket_count,
            bucket_count,
        };
        auto cache_it = cache_indices.find(cache_key);
        std::uint32_t cache_index = 0;
        if (cache_it == cache_indices.end()) {
            cache_index = static_cast<std::uint32_t>(table.showdown_cache_.size());
            table.showdown_cache_.push_back(build_showdown_cache_entry(
                board,
                bucket_map.abstraction(),
                bucket_count,
                bucket_count));
            cache_indices.emplace(cache_key, cache_index);
        } else {
            cache_index = cache_it->second;
        }

        table.node_bindings_.emplace(node_idx, HUNLBucketTerminalBinding{
            cache_index,
            meta.contributions,
        });
    }

    return table;
}

bool HUNLBucketTerminalTable::has_showdown_matrix(std::uint32_t node_idx) const noexcept {
    return node_bindings_.find(node_idx) != node_bindings_.end();
}

const HUNLBucketShowdownMatrix& HUNLBucketTerminalTable::showdown_matrix(std::uint32_t node_idx) const {
    const auto it = node_bindings_.find(node_idx);
    if (it == node_bindings_.end() || it->second.cache_index >= showdown_cache_.size()) {
        throw std::out_of_range("HUNLBucketTerminalTable missing showdown cache");
    }
    return showdown_cache_[it->second.cache_index];
}

double HUNLBucketTerminalTable::expected_showdown_value(
    std::uint32_t node_idx,
    const std::vector<double>* p0_bucket_weights,
    const std::vector<double>* p1_bucket_weights) const {
    const auto binding_it = node_bindings_.find(node_idx);
    if (binding_it == node_bindings_.end()) {
        throw std::out_of_range("HUNLBucketTerminalTable missing node binding");
    }

    const auto& binding = binding_it->second;
    const auto& matrix = showdown_cache_.at(binding.cache_index);
    const auto default0 = make_uniform_weights(matrix.bucket_count_p0);
    const auto default1 = make_uniform_weights(matrix.bucket_count_p1);
    const auto& weights0 = p0_bucket_weights != nullptr ? *p0_bucket_weights : default0;
    const auto& weights1 = p1_bucket_weights != nullptr ? *p1_bucket_weights : default1;
    if (weights0.size() != matrix.bucket_count_p0 || weights1.size() != matrix.bucket_count_p1) {
        throw std::invalid_argument("HUNLBucketTerminalTable weight size mismatch");
    }

    double weighted_value = 0.0;
    double weighted_mass = 0.0;
    for (std::size_t b0 = 0; b0 < matrix.bucket_count_p0; ++b0) {
        const auto hand_count0 = matrix.bucket_hand_counts_p0[b0];
        if (hand_count0 == 0U || weights0[b0] == 0.0) {
            continue;
        }
        for (std::size_t b1 = 0; b1 < matrix.bucket_count_p1; ++b1) {
            const auto hand_count1 = matrix.bucket_hand_counts_p1[b1];
            if (hand_count1 == 0U || weights1[b1] == 0.0) {
                continue;
            }

            const auto valid_pairs = matrix.valid_pair_count(b0, b1);
            if (valid_pairs == 0U) {
                continue;
            }

            const auto pair_density =
                static_cast<double>(valid_pairs) /
                (static_cast<double>(hand_count0) * static_cast<double>(hand_count1));
            const auto joint_mass = weights0[b0] * weights1[b1] * pair_density;
            if (joint_mass == 0.0) {
                continue;
            }

            weighted_mass += joint_mass;
            weighted_value += joint_mass * showdown_bucket_pair_value_p0(
                valid_pairs,
                matrix.net_win_count(b0, b1),
                matrix.tie_pair_count(b0, b1),
                binding.contributions,
                initial_pot_,
                initial_contributions_,
                big_blind_);
        }
    }

    if (weighted_mass <= 0.0) {
        return 0.0;
    }
    return weighted_value / weighted_mass;
}

std::uint64_t HUNLBucketTerminalTable::estimated_bytes() const noexcept {
    std::uint64_t bytes = sizeof(HUNLBucketTerminalTable);
    bytes += static_cast<std::uint64_t>(showdown_cache_.capacity()) * sizeof(HUNLBucketShowdownMatrix);
    bytes += static_cast<std::uint64_t>(node_bindings_.size()) *
        (sizeof(std::uint32_t) + sizeof(HUNLBucketTerminalBinding) + sizeof(void*) * 2U);
    for (const auto& cache_entry : showdown_cache_) {
        bytes += cache_entry.estimated_bytes();
    }
    return bytes;
}

double heuristic_depth_limited_value_p0(const HUNLFlatNodeMeta& node, const HUNLConfig& config) {
    const double bb = static_cast<double>(config.big_blind);
    const double init_c0 = static_cast<double>(config.initial_contributions[0]);
    const double init_c1 = static_cast<double>(config.initial_contributions[1]);
    const double cs0 = static_cast<double>(node.contributions[0]) - init_c0;
    const double cs1 = static_cast<double>(node.contributions[1]) - init_c1;
    const double pot_total = static_cast<double>(config.initial_pot) + cs0 + cs1;
    const double pot_share = pot_total * 0.5 / bb;
    const double pressure = static_cast<double>(node.contributions[0] - node.contributions[1]) / bb;
    const double texture = board_texture_score(HUNLFlatSolveGraph::unpack_board(node.board));

    double street_bias = 0.0;
    switch (node.street) {
        case Street::Flop:
            street_bias = 0.04;
            break;
        case Street::Turn:
            street_bias = 0.08;
            break;
        case Street::River:
            street_bias = 0.12;
            break;
        default:
            street_bias = 0.0;
            break;
    }

    const double board_term = (texture - 0.5) * pot_share * 0.25;
    const double pressure_term = -0.05 * pressure;
    const double street_term = street_bias * (texture - 0.5);
    return pot_share + board_term + pressure_term + street_term;
}

}  // namespace core

namespace std {

std::size_t hash<core::HUNLBucketTerminalCacheKey>::operator()(
    const core::HUNLBucketTerminalCacheKey& key) const noexcept {
    std::size_t seed = 0;
    for (const auto card : key.board.cards) {
        seed ^= static_cast<std::size_t>(card) + 0x9e3779b9U + (seed << 6U) + (seed >> 2U);
    }
    seed ^= static_cast<std::size_t>(key.board.count) + 0x9e3779b9U + (seed << 6U) + (seed >> 2U);
    seed ^= static_cast<std::size_t>(key.bucket_count_p0) + 0x9e3779b9U + (seed << 6U) + (seed >> 2U);
    seed ^= static_cast<std::size_t>(key.bucket_count_p1) + 0x9e3779b9U + (seed << 6U) + (seed >> 2U);
    return seed;
}

}  // namespace std
