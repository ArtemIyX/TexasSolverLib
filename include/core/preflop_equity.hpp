#pragma once

#include "core/hunl.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <tuple>
#include <vector>

namespace core {

inline constexpr std::size_t PREFLOP_NUM_CLASSES = 169;
inline constexpr std::size_t PREFLOP_NUM_VARIANTS = 3;

struct HoleRep {
    std::array<std::uint8_t, 2> hero{};
    std::array<std::uint8_t, 2> villain{};
};

std::uint16_t class_index(std::uint8_t rank_hi, std::uint8_t rank_lo, bool suited);
std::tuple<std::uint8_t, std::uint8_t, bool> class_decode(std::uint16_t class_idx);
std::uint16_t hole_to_class(const std::array<std::uint8_t, 2>& hole);
std::optional<HoleRep> build_hole_rep(std::uint16_t hero_class, std::uint16_t villain_class, std::uint8_t variant);

double enumerate_pair_equity(const std::array<std::uint8_t, 2>& hero, const std::array<std::uint8_t, 2>& villain);
std::vector<double> build_equity_table_flat();
std::vector<double> build_equity_table_flat_parallel(std::size_t n_threads);
double monte_carlo_pair_equity(
    const std::array<std::uint8_t, 2>& hero,
    const std::array<std::uint8_t, 2>& villain,
    std::size_t n_samples,
    std::uint64_t seed);

class PreflopEquityTable {
public:
    PreflopEquityTable();

    double at(std::size_t hero_class, std::size_t villain_class, std::size_t variant) const;
    double& at(std::size_t hero_class, std::size_t villain_class, std::size_t variant);
    bool empty() const;
    const std::vector<double>& data() const;
    std::vector<double>& data();

    static PreflopEquityTable build();
    static PreflopEquityTable build_parallel(std::size_t n_threads);
    static PreflopEquityTable load(const std::filesystem::path& path);
    void save(const std::filesystem::path& path) const;
    static PreflopEquityTable load_csv(const std::string& path);
    void save_csv(const std::string& path) const;

private:
    std::vector<double> table_;
};

}  // namespace core
