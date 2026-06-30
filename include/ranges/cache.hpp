#pragma once

#include "games/hunl.hpp"
#include "ranges/source.hpp"

#include <array>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>

namespace core {

struct RangeCacheKey {
    std::uint8_t player = 0;
    Street street = Street::Preflop;
    std::vector<std::uint8_t> board;
    int starting_stack = 0;
    int small_blind = 0;
    int big_blind = 0;
    int ante = 0;
    int initial_pot = 0;
    std::array<int, 2> initial_contributions = {0, 0};
    std::optional<std::string> abstraction_path;
    std::optional<std::string> abstraction_version;
    RangeVector::Kind range_kind = RangeVector::Kind::Combo;
};

struct RangeCacheEntry {
    RangeCacheKey key;
    CanonicalRange range;
    std::uint64_t iterations = 0;
    double exploitability = 0.0;
    std::string label;
};

[[nodiscard]] RangeCacheKey make_range_cache_key(
    const HUNLConfig& config,
    std::uint8_t player,
    RangeVector::Kind range_kind);
[[nodiscard]] bool range_cache_key_matches(
    const RangeCacheKey& key,
    const HUNLConfig& config,
    std::uint8_t player,
    RangeVector::Kind range_kind);
[[nodiscard]] std::string range_cache_basename(const RangeCacheKey& key);

bool save_range_cache_entry(const std::filesystem::path& path, const RangeCacheEntry& entry);
bool load_range_cache_entry(const std::filesystem::path& path, RangeCacheEntry& entry);
std::optional<RangeCacheEntry> load_range_cache_if_compatible(
    const std::filesystem::path& path,
    const HUNLConfig& config,
    std::uint8_t player,
    RangeVector::Kind range_kind);

}  // namespace core
