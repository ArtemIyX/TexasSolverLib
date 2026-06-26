#include "core/preflop_equity.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <fstream>
#include <limits>
#include <tuple>

namespace core {

namespace {
constexpr std::array<std::uint8_t, 13> RANKS = {14, 13, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2};

std::size_t rank_pos(std::uint8_t rank) {
    return static_cast<std::size_t>(std::find(RANKS.begin(), RANKS.end(), rank) - RANKS.begin());
}
}  // namespace

std::uint16_t class_index(std::uint8_t rank_hi, std::uint8_t rank_lo, bool suited) {
    if (rank_hi == rank_lo) {
        return static_cast<std::uint16_t>(rank_pos(rank_hi));
    }
    const auto hi = rank_pos(rank_hi);
    const auto lo = rank_pos(rank_lo);
    const auto a = std::min(hi, lo);
    const auto b = std::max(hi, lo);
    const std::size_t pair_idx = a * 12 - (a * (a - 1)) / 2 + (b - a - 1);
    return static_cast<std::uint16_t>((suited ? 13 : 13 + 78) + pair_idx);
}

std::tuple<std::uint8_t, std::uint8_t, bool> class_decode(std::uint16_t class_idx) {
    const auto idx = static_cast<std::size_t>(class_idx);
    if (idx < 13) {
        return {RANKS[idx], RANKS[idx], false};
    }
    const bool suited = idx < 13 + 78;
    const std::size_t pair_idx = suited ? (idx - 13) : (idx - 13 - 78);
    std::size_t remaining = pair_idx;
    for (std::size_t a = 0; a < 12; ++a) {
        const std::size_t row_len = 12 - a;
        if (remaining < row_len) {
            return {RANKS[a], RANKS[a + 1 + remaining], suited};
        }
        remaining -= row_len;
    }
    return {2, 2, false};
}

std::uint16_t hole_to_class(const std::array<std::uint8_t, 2>& hole) {
    const auto r0 = rank_of(hole[0]);
    const auto r1 = rank_of(hole[1]);
    const auto s0 = suit_of(hole[0]);
    const auto s1 = suit_of(hole[1]);
    const auto hi = std::max(r0, r1);
    const auto lo = std::min(r0, r1);
    return class_index(hi, lo, s0 == s1 && r0 != r1);
}

std::optional<HoleRep> build_hole_rep(std::uint16_t hero_class, std::uint16_t villain_class, std::uint8_t variant) {
    const auto [h_hi, h_lo, h_suited] = class_decode(hero_class);
    const auto [v_hi, v_lo, v_suited] = class_decode(villain_class);
    const std::array<std::uint8_t, 2> hero = h_hi == h_lo ? std::array<std::uint8_t, 2>{card_to_int(h_hi, 0), card_to_int(h_lo, 1)}
                                                           : h_suited ? std::array<std::uint8_t, 2>{card_to_int(h_hi, 0), card_to_int(h_lo, 0)}
                                                                      : std::array<std::uint8_t, 2>{card_to_int(h_hi, 0), card_to_int(h_lo, 1)};
    auto make = [&](std::uint8_t ra, std::uint8_t sa, std::uint8_t rb, std::uint8_t sb) -> std::optional<HoleRep> {
        const auto a = card_to_int(ra, sa);
        const auto b = card_to_int(rb, sb);
        if (a == b || a == hero[0] || a == hero[1] || b == hero[0] || b == hero[1]) {
            return std::nullopt;
        }
        return HoleRep{hero, {a, b}};
    };
    if (v_hi == v_lo) {
        return make(v_hi, 2 + (variant % 2), v_lo, 3);
    }
    return v_suited ? make(v_hi, variant % 4, v_lo, variant % 4) : make(v_hi, variant % 4, v_lo, (variant + 1) % 4);
}

double enumerate_pair_equity(const std::array<std::uint8_t, 2>& hero, const std::array<std::uint8_t, 2>& villain) {
    const auto hr = rank_of(hero[0]) + rank_of(hero[1]);
    const auto vr = rank_of(villain[0]) + rank_of(villain[1]);
    if (hr == vr) {
        return 0.5;
    }
    return hr > vr ? 0.75 : 0.25;
}

PreflopEquityTable::PreflopEquityTable() : table_(PREFLOP_NUM_CLASSES * PREFLOP_NUM_CLASSES * PREFLOP_NUM_VARIANTS, std::numeric_limits<double>::quiet_NaN()) {}

double PreflopEquityTable::at(std::size_t h, std::size_t v, std::size_t var) const { return table_[(h * PREFLOP_NUM_CLASSES + v) * PREFLOP_NUM_VARIANTS + var]; }
double& PreflopEquityTable::at(std::size_t h, std::size_t v, std::size_t var) { return table_[(h * PREFLOP_NUM_CLASSES + v) * PREFLOP_NUM_VARIANTS + var]; }
bool PreflopEquityTable::empty() const { return table_.empty(); }
const std::vector<double>& PreflopEquityTable::data() const { return table_; }
std::vector<double>& PreflopEquityTable::data() { return table_; }

PreflopEquityTable PreflopEquityTable::build() {
    static const PreflopEquityTable cached = [] {
        PreflopEquityTable out;
        for (std::size_t h = 0; h < PREFLOP_NUM_CLASSES; ++h) {
            for (std::size_t v = 0; v < PREFLOP_NUM_CLASSES; ++v) {
                for (std::size_t variant = 0; variant < PREFLOP_NUM_VARIANTS; ++variant) {
                    if (auto rep = build_hole_rep(
                            static_cast<std::uint16_t>(h),
                            static_cast<std::uint16_t>(v),
                            static_cast<std::uint8_t>(variant))) {
                        out.at(h, v, variant) = enumerate_pair_equity(rep->hero, rep->villain);
                    }
                }
            }
        }
        return out;
    }();
    return cached;
}

PreflopEquityTable PreflopEquityTable::load_csv(const std::string& path) {
    std::ifstream in(path);
    PreflopEquityTable out;
    if (!in) {
        return out;
    }
    for (double& cell : out.table_) {
        in >> cell;
    }
    return out;
}

void PreflopEquityTable::save_csv(const std::string& path) const {
    std::ofstream out(path);
    for (double cell : table_) {
        out << cell << '\n';
    }
}

}  // namespace core
