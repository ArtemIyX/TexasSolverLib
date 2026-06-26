#include "core/preflop_equity.hpp"

#include "core/hunl_eval.hpp"
#include "core/pcs.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <fstream>
#include <limits>
#include <mutex>
#include <thread>
#include <vector>

namespace core {

namespace {
constexpr std::array<std::uint8_t, 13> RANKS = {14, 13, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2};
constexpr std::size_t DECK_SIZE = 52;
constexpr std::size_t BOARD_CARDS = 5;

std::size_t rank_pos(std::uint8_t rank) {
    return static_cast<std::size_t>(std::find(RANKS.begin(), RANKS.end(), rank) - RANKS.begin());
}

std::array<std::uint8_t, DECK_SIZE> build_deck() {
    std::array<std::uint8_t, DECK_SIZE> deck{};
    std::size_t idx = 0;
    for (std::uint8_t rank = 2; rank <= 14; ++rank) {
        for (std::uint8_t suit = 0; suit < 4; ++suit) {
            deck[idx++] = card_to_int(rank, suit);
        }
    }
    return deck;
}

template <typename F>
void for_each_5_combo(std::size_t n, F&& fn) {
    for (std::size_t a = 0; a + 4 < n; ++a) {
        for (std::size_t b = a + 1; b + 3 < n; ++b) {
            for (std::size_t c = b + 1; c + 2 < n; ++c) {
                for (std::size_t d = c + 1; d + 1 < n; ++d) {
                    for (std::size_t e = d + 1; e < n; ++e) {
                        fn(a, b, c, d, e);
                    }
                }
            }
        }
    }
}

HoleRep make_rep(const std::array<std::uint8_t, 2>& hero, const std::array<std::uint8_t, 2>& villain) {
    return HoleRep{hero, villain};
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
    const std::array<std::uint8_t, 2> hero = h_hi == h_lo
        ? std::array<std::uint8_t, 2>{card_to_int(h_hi, 0), card_to_int(h_lo, 1)}
        : h_suited ? std::array<std::uint8_t, 2>{card_to_int(h_hi, 0), card_to_int(h_lo, 0)}
                   : std::array<std::uint8_t, 2>{card_to_int(h_hi, 0), card_to_int(h_lo, 1)};
    auto make = [&](std::uint8_t ra, std::uint8_t sa, std::uint8_t rb, std::uint8_t sb) -> std::optional<HoleRep> {
        const auto a = card_to_int(ra, sa);
        const auto b = card_to_int(rb, sb);
        if (a == b || a == hero[0] || a == hero[1] || b == hero[0] || b == hero[1]) {
            return std::nullopt;
        }
        return make_rep(hero, {a, b});
    };
    if (v_hi == v_lo) {
        return make(v_hi, 2 + (variant % 2), v_lo, 3);
    }
    return v_suited ? make(v_hi, variant % 4, v_lo, variant % 4) : make(v_hi, variant % 4, v_lo, (variant + 1) % 4);
}

double enumerate_pair_equity(const std::array<std::uint8_t, 2>& hero, const std::array<std::uint8_t, 2>& villain) {
    std::array<bool, 64> used{};
    for (const auto c : hero) {
        used[c] = true;
    }
    for (const auto c : villain) {
        used[c] = true;
    }

    const auto deck = build_deck();
    std::vector<std::uint8_t> live;
    live.reserve(48);
    for (const auto c : deck) {
        if (!used[c]) {
            live.push_back(c);
        }
    }

    std::array<std::uint8_t, 7> seven0{};
    std::array<std::uint8_t, 7> seven1{};
    seven0[0] = hero[0];
    seven0[1] = hero[1];
    seven1[0] = villain[0];
    seven1[1] = villain[1];

    std::uint64_t wins = 0;
    std::uint64_t ties = 0;
    std::uint64_t total = 0;
    for_each_5_combo(live.size(), [&](std::size_t a, std::size_t b, std::size_t c, std::size_t d, std::size_t e) {
        seven0[2] = live[a];
        seven0[3] = live[b];
        seven0[4] = live[c];
        seven0[5] = live[d];
        seven0[6] = live[e];
        seven1[2] = live[a];
        seven1[3] = live[b];
        seven1[4] = live[c];
        seven1[5] = live[d];
        seven1[6] = live[e];
        const auto s0 = Strength::evaluate_7(seven0);
        const auto s1 = Strength::evaluate_7(seven1);
        if (s0 > s1) {
            ++wins;
        } else if (s0 == s1) {
            ++ties;
        }
        ++total;
    });

    return (static_cast<double>(wins) + 0.5 * static_cast<double>(ties)) / static_cast<double>(total);
}

double monte_carlo_pair_equity(
    const std::array<std::uint8_t, 2>& hero,
    const std::array<std::uint8_t, 2>& villain,
    std::size_t n_samples,
    std::uint64_t seed) {
    std::array<bool, 64> used{};
    for (const auto c : hero) {
        used[c] = true;
    }
    for (const auto c : villain) {
        used[c] = true;
    }

    const auto deck = build_deck();
    std::vector<std::uint8_t> live;
    live.reserve(48);
    for (const auto c : deck) {
        if (!used[c]) {
            live.push_back(c);
        }
    }

    PcsRng rng(seed);
    std::array<std::uint8_t, 7> seven0{};
    std::array<std::uint8_t, 7> seven1{};
    seven0[0] = hero[0];
    seven0[1] = hero[1];
    seven1[0] = villain[0];
    seven1[1] = villain[1];

    std::uint64_t wins = 0;
    std::uint64_t ties = 0;
    std::array<std::size_t, BOARD_CARDS> picks{};
    for (std::size_t sample = 0; sample < n_samples; ++sample) {
        for (std::size_t i = 0; i < BOARD_CARDS; ++i) {
            while (true) {
                const auto pos = static_cast<std::size_t>(rng.gen_range(static_cast<std::uint64_t>(live.size())));
                bool duplicate = false;
                for (std::size_t j = 0; j < i; ++j) {
                    if (picks[j] == pos) {
                        duplicate = true;
                        break;
                    }
                }
                if (!duplicate) {
                    picks[i] = pos;
                    break;
                }
            }
        }
        for (std::size_t i = 0; i < BOARD_CARDS; ++i) {
            seven0[2 + i] = live[picks[i]];
            seven1[2 + i] = live[picks[i]];
        }
        const auto s0 = Strength::evaluate_7(seven0);
        const auto s1 = Strength::evaluate_7(seven1);
        if (s0 > s1) {
            ++wins;
        } else if (s0 == s1) {
            ++ties;
        }
    }
    return (static_cast<double>(wins) + 0.5 * static_cast<double>(ties)) / static_cast<double>(n_samples);
}

std::vector<double> build_equity_table_flat() {
    std::vector<double> table(PREFLOP_NUM_CLASSES * PREFLOP_NUM_CLASSES * PREFLOP_NUM_VARIANTS, std::numeric_limits<double>::quiet_NaN());
    for (std::size_t hero = 0; hero < PREFLOP_NUM_CLASSES; ++hero) {
        for (std::size_t villain = 0; villain < PREFLOP_NUM_CLASSES; ++villain) {
            for (std::size_t variant = 0; variant < PREFLOP_NUM_VARIANTS; ++variant) {
                if (auto rep = build_hole_rep(static_cast<std::uint16_t>(hero), static_cast<std::uint16_t>(villain), static_cast<std::uint8_t>(variant))) {
                    table[(hero * PREFLOP_NUM_CLASSES + villain) * PREFLOP_NUM_VARIANTS + variant] =
                        enumerate_pair_equity(rep->hero, rep->villain);
                }
            }
        }
    }
    return table;
}

std::vector<double> build_equity_table_flat_parallel(std::size_t n_threads) {
    n_threads = std::max<std::size_t>(1, n_threads);
    std::vector<double> table(PREFLOP_NUM_CLASSES * PREFLOP_NUM_CLASSES * PREFLOP_NUM_VARIANTS, std::numeric_limits<double>::quiet_NaN());
    std::mutex mutex;
    std::atomic<std::size_t> cursor{0};
    std::vector<std::tuple<std::uint16_t, std::uint16_t, std::uint8_t>> work;
    work.reserve(PREFLOP_NUM_CLASSES * PREFLOP_NUM_CLASSES * PREFLOP_NUM_VARIANTS);
    for (std::uint16_t h = 0; h < PREFLOP_NUM_CLASSES; ++h) {
        for (std::uint16_t v = 0; v < PREFLOP_NUM_CLASSES; ++v) {
            for (std::uint8_t variant = 0; variant < PREFLOP_NUM_VARIANTS; ++variant) {
                work.emplace_back(h, v, variant);
            }
        }
    }

    std::vector<std::thread> threads;
    threads.reserve(n_threads);
    for (std::size_t t = 0; t < n_threads; ++t) {
        threads.emplace_back([&] {
            while (true) {
                const auto i = cursor.fetch_add(1, std::memory_order_relaxed);
                if (i >= work.size()) {
                    break;
                }
                const auto [h, v, variant] = work[i];
                if (auto rep = build_hole_rep(h, v, variant)) {
                    const auto eq = enumerate_pair_equity(rep->hero, rep->villain);
                    std::lock_guard<std::mutex> lock(mutex);
                    table[(static_cast<std::size_t>(h) * PREFLOP_NUM_CLASSES + static_cast<std::size_t>(v)) * PREFLOP_NUM_VARIANTS + variant] = eq;
                }
            }
        });
    }
    for (auto& thread : threads) {
        thread.join();
    }
    return table;
}

PreflopEquityTable::PreflopEquityTable()
    : table_(PREFLOP_NUM_CLASSES * PREFLOP_NUM_CLASSES * PREFLOP_NUM_VARIANTS, std::numeric_limits<double>::quiet_NaN()) {}

double PreflopEquityTable::at(std::size_t h, std::size_t v, std::size_t var) const {
    return table_[(h * PREFLOP_NUM_CLASSES + v) * PREFLOP_NUM_VARIANTS + var];
}

double& PreflopEquityTable::at(std::size_t h, std::size_t v, std::size_t var) {
    return table_[(h * PREFLOP_NUM_CLASSES + v) * PREFLOP_NUM_VARIANTS + var];
}

bool PreflopEquityTable::empty() const {
    return table_.empty();
}

const std::vector<double>& PreflopEquityTable::data() const {
    return table_;
}

std::vector<double>& PreflopEquityTable::data() {
    return table_;
}

PreflopEquityTable PreflopEquityTable::build() {
    PreflopEquityTable out;
    out.table_ = build_equity_table_flat();
    return out;
}

PreflopEquityTable PreflopEquityTable::build_parallel(std::size_t n_threads) {
    PreflopEquityTable out;
    out.table_ = build_equity_table_flat_parallel(n_threads);
    return out;
}

namespace {
constexpr std::array<char, 8> MAGIC = {'T', 'S', 'P', 'E', 'Q', '0', '0', '1'};

void write_u64(std::ostream& out, std::uint64_t value) {
    out.write(reinterpret_cast<const char*>(&value), sizeof(value));
}

std::uint64_t read_u64(std::istream& in) {
    std::uint64_t value = 0;
    in.read(reinterpret_cast<char*>(&value), sizeof(value));
    return value;
}
}  // namespace

PreflopEquityTable PreflopEquityTable::load(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    PreflopEquityTable out;
    if (!in) {
        return out;
    }
    std::array<char, MAGIC.size()> magic{};
    in.read(magic.data(), static_cast<std::streamsize>(magic.size()));
    if (magic != MAGIC) {
        return load_csv(path.string());
    }
    const auto n = read_u64(in);
    if (n != out.table_.size()) {
        return out;
    }
    in.read(reinterpret_cast<char*>(out.table_.data()), static_cast<std::streamsize>(out.table_.size() * sizeof(double)));
    return out;
}

void PreflopEquityTable::save(const std::filesystem::path& path) const {
    std::ofstream out(path, std::ios::binary);
    if (!out) {
        return;
    }
    out.write(MAGIC.data(), static_cast<std::streamsize>(MAGIC.size()));
    write_u64(out, static_cast<std::uint64_t>(table_.size()));
    out.write(reinterpret_cast<const char*>(table_.data()), static_cast<std::streamsize>(table_.size() * sizeof(double)));
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
    for (const double cell : table_) {
        out << cell << '\n';
    }
}

}  // namespace core
