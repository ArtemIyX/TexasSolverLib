#include "games/hunl_eval.hpp"

#include <algorithm>
#include <array>
#include <stdexcept>

namespace core {

namespace {

constexpr std::uint64_t HAND_HIGH_CARD = 0;
constexpr std::uint64_t HAND_PAIR = 1;
constexpr std::uint64_t HAND_TWO_PAIR = 2;
constexpr std::uint64_t HAND_THREE_OF_A_KIND = 3;
constexpr std::uint64_t HAND_STRAIGHT = 4;
constexpr std::uint64_t HAND_FLUSH = 5;
constexpr std::uint64_t HAND_FULL_HOUSE = 6;
constexpr std::uint64_t HAND_FOUR_OF_A_KIND = 7;
constexpr std::uint64_t HAND_STRAIGHT_FLUSH = 8;

Strength pack(
    std::uint64_t category,
    std::uint64_t tb1,
    std::uint64_t tb2,
    std::uint64_t tb3,
    std::uint64_t tb4,
    std::uint64_t tb5) {
    return Strength{
        (category << 56) | (tb1 << 52) | (tb2 << 48) | (tb3 << 44) | (tb4 << 40) | (tb5 << 36)};
}

std::uint8_t straight_high(const std::vector<std::uint8_t>& unique_ranks_desc) {
    if (unique_ranks_desc.empty()) {
        return 0;
    }

    std::vector<int> ranks(unique_ranks_desc.begin(), unique_ranks_desc.end());
    if (ranks.front() == 14) {
        ranks.push_back(1);
    }
    if (ranks.size() < 5) {
        return 0;
    }

    for (std::size_t i = 0; i + 4 < ranks.size(); ++i) {
        if (ranks[i] - ranks[i + 4] == 4) {
            return static_cast<std::uint8_t>(ranks[i]);
        }
    }
    return 0;
}

}  // namespace

Strength evaluate_n(const std::vector<std::uint8_t>& cards) {
    if (cards.size() < 5) {
        throw std::invalid_argument("evaluate_n requires at least 5 cards");
    }

    std::vector<std::uint8_t> ranks_desc;
    ranks_desc.reserve(cards.size());
    for (const auto card : cards) {
        ranks_desc.push_back(rank_of(card));
    }
    std::sort(ranks_desc.begin(), ranks_desc.end(), std::greater<>());

    std::array<std::uint8_t, 15> rank_counts = {};
    for (const auto rank : ranks_desc) {
        ++rank_counts[rank];
    }

    std::array<std::uint8_t, 4> suit_counts = {};
    for (const auto card : cards) {
        ++suit_counts[suit_of(card)];
    }

    std::optional<std::uint8_t> flush_suit;
    for (std::uint8_t suit = 0; suit < suit_counts.size(); ++suit) {
        if (suit_counts[suit] >= 5) {
            flush_suit = suit;
            break;
        }
    }

    if (flush_suit.has_value()) {
        std::vector<std::uint8_t> flush_ranks_desc;
        for (const auto card : cards) {
            if (suit_of(card) == *flush_suit) {
                flush_ranks_desc.push_back(rank_of(card));
            }
        }
        std::sort(flush_ranks_desc.begin(), flush_ranks_desc.end(), std::greater<>());
        const auto sf_high = straight_high(flush_ranks_desc);
        if (sf_high > 0) {
            return pack(HAND_STRAIGHT_FLUSH, sf_high, 0, 0, 0, 0);
        }
    }

    std::vector<std::pair<std::uint8_t, std::uint8_t>> grouped;
    grouped.reserve(13);
    for (std::uint8_t rank = 14; rank >= 2; --rank) {
        const auto count = rank_counts[rank];
        if (count > 0) {
            grouped.push_back({count, rank});
        }
        if (rank == 2) {
            break;
        }
    }
    std::stable_sort(
        grouped.begin(),
        grouped.end(),
        [](const auto& lhs, const auto& rhs) { return lhs.first > rhs.first; });

    if (!grouped.empty() && grouped[0].first == 4) {
        const auto quad = grouped[0].second;
        const auto kicker_it =
            std::find_if(ranks_desc.begin(), ranks_desc.end(), [quad](auto rank) { return rank != quad; });
        const auto kicker = kicker_it != ranks_desc.end() ? *kicker_it : 0;
        return pack(HAND_FOUR_OF_A_KIND, quad, kicker, 0, 0, 0);
    }

    if (!grouped.empty() && grouped[0].first == 3) {
        for (std::size_t i = 1; i < grouped.size(); ++i) {
            if (grouped[i].first >= 2) {
                return pack(HAND_FULL_HOUSE, grouped[0].second, grouped[i].second, 0, 0, 0);
            }
        }
    }

    if (flush_suit.has_value()) {
        std::vector<std::uint8_t> top5;
        for (const auto card : cards) {
            if (suit_of(card) == *flush_suit) {
                top5.push_back(rank_of(card));
            }
        }
        std::sort(top5.begin(), top5.end(), std::greater<>());
        top5.resize(5);
        return pack(HAND_FLUSH, top5[0], top5[1], top5[2], top5[3], top5[4]);
    }

    std::vector<std::uint8_t> unique_desc;
    for (std::uint8_t rank = 14; rank >= 2; --rank) {
        if (rank_counts[rank] > 0) {
            unique_desc.push_back(rank);
        }
        if (rank == 2) {
            break;
        }
    }
    const auto s_high = straight_high(unique_desc);
    if (s_high > 0) {
        return pack(HAND_STRAIGHT, s_high, 0, 0, 0, 0);
    }

    if (!grouped.empty() && grouped[0].first == 3) {
        const auto trips = grouped[0].second;
        std::vector<std::uint8_t> kickers;
        for (const auto rank : ranks_desc) {
            if (rank != trips) {
                kickers.push_back(rank);
            }
        }
        if (kickers.size() > 2) {
            kickers.resize(2);
        }
        const auto k0 = kickers.size() > 0 ? kickers[0] : 0;
        const auto k1 = kickers.size() > 1 ? kickers[1] : 0;
        return pack(HAND_THREE_OF_A_KIND, trips, k0, k1, 0, 0);
    }

    if (grouped.size() >= 2 && grouped[0].first == 2 && grouped[1].first == 2) {
        const auto p1 = grouped[0].second;
        const auto p2 = grouped[1].second;
        const auto kicker_it = std::find_if(
            ranks_desc.begin(), ranks_desc.end(), [p1, p2](auto rank) { return rank != p1 && rank != p2; });
        const auto kicker = kicker_it != ranks_desc.end() ? *kicker_it : 0;
        return pack(HAND_TWO_PAIR, p1, p2, kicker, 0, 0);
    }

    if (!grouped.empty() && grouped[0].first == 2) {
        const auto pair = grouped[0].second;
        std::vector<std::uint8_t> kickers;
        for (const auto rank : ranks_desc) {
            if (rank != pair) {
                kickers.push_back(rank);
            }
        }
        if (kickers.size() > 3) {
            kickers.resize(3);
        }
        const auto k0 = kickers.size() > 0 ? kickers[0] : 0;
        const auto k1 = kickers.size() > 1 ? kickers[1] : 0;
        const auto k2 = kickers.size() > 2 ? kickers[2] : 0;
        return pack(HAND_PAIR, pair, k0, k1, k2, 0);
    }

    std::vector<std::uint8_t> top5(ranks_desc.begin(), ranks_desc.begin() + 5);
    return pack(HAND_HIGH_CARD, top5[0], top5[1], top5[2], top5[3], top5[4]);
}

Strength Strength::evaluate_5(const std::array<std::uint8_t, 5>& cards) {
    return evaluate_n(std::vector<std::uint8_t>(cards.begin(), cards.end()));
}

Strength Strength::evaluate_7(const std::array<std::uint8_t, 7>& cards) {
    return evaluate_n(std::vector<std::uint8_t>(cards.begin(), cards.end()));
}

}  // namespace core


