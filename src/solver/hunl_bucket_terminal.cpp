#include "solver/hunl_bucket_terminal.hpp"

#include <algorithm>
#include <array>
#include <stdexcept>

namespace core {

namespace {

std::vector<std::array<std::uint8_t, 2>> enumerate_live_hands_for_board(const std::vector<std::uint8_t>& board) {
    std::array<bool, 64> blocked = {};
    for (const auto card : board) {
        blocked[card] = true;
    }

    std::vector<std::uint8_t> live_cards;
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

bool overlaps(const std::array<std::uint8_t, 2>& lhs, const std::array<std::uint8_t, 2>& rhs) {
    return lhs[0] == rhs[0] || lhs[0] == rhs[1] || lhs[1] == rhs[0] || lhs[1] == rhs[1];
}

double showdown_utility_for_player0(
    const std::vector<std::uint8_t>& board,
    const std::array<std::uint8_t, 2>& hole0,
    const std::array<std::uint8_t, 2>& hole1,
    const HUNLFlatNodeMeta& node,
    const HUNLConfig& config) {
    const double bb = static_cast<double>(config.big_blind);
    const double init_c0 = static_cast<double>(config.initial_contributions[0]);
    const double init_c1 = static_cast<double>(config.initial_contributions[1]);
    const double cs0 = static_cast<double>(node.contributions[0]) - init_c0;
    const double cs1 = static_cast<double>(node.contributions[1]) - init_c1;
    const double pot_total = static_cast<double>(config.initial_pot) + cs0 + cs1;

    std::array<std::uint8_t, 7> seven0 = {};
    std::array<std::uint8_t, 7> seven1 = {};
    seven0[0] = hole0[0];
    seven0[1] = hole0[1];
    seven1[0] = hole1[0];
    seven1[1] = hole1[1];
    for (std::size_t i = 0; i < 5; ++i) {
        seven0[i + 2] = board[i];
        seven1[i + 2] = board[i];
    }

    const auto s0 = Strength::evaluate_7(seven0);
    const auto s1 = Strength::evaluate_7(seven1);
    if (s0 > s1) {
        return (pot_total - cs0) / bb;
    }
    if (s1 > s0) {
        return -cs0 / bb;
    }
    return (pot_total / 2.0 - cs0) / bb;
}

std::vector<double> make_uniform_weights(std::uint32_t count) {
    if (count == 0) {
        return {};
    }
    return std::vector<double>(count, 1.0 / static_cast<double>(count));
}

}  // namespace

double HUNLBucketShowdownMatrix::value(std::size_t bucket0, std::size_t bucket1) const {
    if (bucket0 >= bucket_count_p0 || bucket1 >= bucket_count_p1) {
        throw std::out_of_range("HUNLBucketShowdownMatrix bucket index out of range");
    }
    return values[bucket0 * static_cast<std::size_t>(bucket_count_p1) + bucket1];
}

std::uint32_t HUNLBucketShowdownMatrix::pair_count(std::size_t bucket0, std::size_t bucket1) const {
    if (bucket0 >= bucket_count_p0 || bucket1 >= bucket_count_p1) {
        throw std::out_of_range("HUNLBucketShowdownMatrix bucket index out of range");
    }
    return pair_counts[bucket0 * static_cast<std::size_t>(bucket_count_p1) + bucket1];
}

HUNLBucketTerminalTable HUNLBucketTerminalTable::build(
    const HUNLFlatSolveGraph& graph,
    const HUNLFlatBucketMap& bucket_map) {
    HUNLBucketTerminalTable table;

    for (const auto node_idx : graph.showdown_terminal_nodes) {
        const auto& meta = graph.node_meta[node_idx];
        if (!graph.config) {
            continue;
        }
        const auto& board = graph.nodes[node_idx].board;
        if (board.size() != 5) {
            continue;
        }
        const auto live_hands = enumerate_live_hands_for_board(board);

        if (bucket_map.abstraction().metadata.bucket_counts.size() <= 2) {
            continue;
        }
        const auto bucket_count = static_cast<std::uint32_t>(bucket_map.abstraction().metadata.bucket_counts[2]);
        if (bucket_count == 0) {
            continue;
        }

        HUNLBucketShowdownMatrix matrix;
        matrix.node_idx = node_idx;
        matrix.bucket_count_p0 = bucket_count;
        matrix.bucket_count_p1 = bucket_count;
        matrix.values.assign(static_cast<std::size_t>(bucket_count) * bucket_count, 0.0);
        matrix.pair_counts.assign(static_cast<std::size_t>(bucket_count) * bucket_count, 0U);

        for (const auto& hole0 : live_hands) {
            const auto bucket0 = lookup_bucket(bucket_map.abstraction(), board, hole0, Street::River);
            if (bucket0 < 0) {
                continue;
            }
            for (const auto& hole1 : live_hands) {
                if (overlaps(hole0, hole1)) {
                    continue;
                }
                const auto bucket1 = lookup_bucket(bucket_map.abstraction(), board, hole1, Street::River);
                if (bucket1 < 0) {
                    continue;
                }

                const auto index =
                    static_cast<std::size_t>(bucket0) * static_cast<std::size_t>(bucket_count) + static_cast<std::size_t>(bucket1);
                matrix.values[index] += showdown_utility_for_player0(board, hole0, hole1, meta, *graph.config);
                ++matrix.pair_counts[index];
            }
        }

        for (std::size_t i = 0; i < matrix.values.size(); ++i) {
            if (matrix.pair_counts[i] > 0U) {
                matrix.values[i] /= static_cast<double>(matrix.pair_counts[i]);
            }
        }

        table.showdown_matrices_.emplace(node_idx, std::move(matrix));
    }

    return table;
}

bool HUNLBucketTerminalTable::has_showdown_matrix(std::uint32_t node_idx) const noexcept {
    return showdown_matrices_.find(node_idx) != showdown_matrices_.end();
}

const HUNLBucketShowdownMatrix& HUNLBucketTerminalTable::showdown_matrix(std::uint32_t node_idx) const {
    const auto it = showdown_matrices_.find(node_idx);
    if (it == showdown_matrices_.end()) {
        throw std::out_of_range("HUNLBucketTerminalTable missing showdown matrix");
    }
    return it->second;
}

double HUNLBucketTerminalTable::expected_showdown_value(
    std::uint32_t node_idx,
    const std::vector<double>* p0_bucket_weights,
    const std::vector<double>* p1_bucket_weights) const {
    const auto& matrix = showdown_matrix(node_idx);
    const auto default0 = make_uniform_weights(matrix.bucket_count_p0);
    const auto default1 = make_uniform_weights(matrix.bucket_count_p1);
    const auto& weights0 = p0_bucket_weights != nullptr ? *p0_bucket_weights : default0;
    const auto& weights1 = p1_bucket_weights != nullptr ? *p1_bucket_weights : default1;
    if (weights0.size() != matrix.bucket_count_p0 || weights1.size() != matrix.bucket_count_p1) {
        throw std::invalid_argument("HUNLBucketTerminalTable weight size mismatch");
    }

    double value = 0.0;
    for (std::size_t b0 = 0; b0 < matrix.bucket_count_p0; ++b0) {
        for (std::size_t b1 = 0; b1 < matrix.bucket_count_p1; ++b1) {
            value += weights0[b0] * weights1[b1] * matrix.value(b0, b1);
        }
    }
    return value;
}

}  // namespace core
