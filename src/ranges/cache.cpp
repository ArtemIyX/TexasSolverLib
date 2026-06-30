#include "ranges/cache.hpp"

#include <array>
#include <cstdint>
#include <fstream>
#include <limits>
#include <optional>
#include <sstream>
#include <string>

namespace core {

namespace {

constexpr std::array<char, 8> RANGE_CACHE_MAGIC{{'T', 'S', 'R', 'C', 'A', 'C', 'H', 'E'}};
constexpr std::uint8_t RANGE_CACHE_VERSION = 1;

template <class T>
void write_pod(std::ostream& out, const T& value) {
    out.write(reinterpret_cast<const char*>(&value), sizeof(T));
}

template <class T>
bool read_pod(std::istream& in, T& value) {
    return static_cast<bool>(in.read(reinterpret_cast<char*>(&value), sizeof(T)));
}

void write_string(std::ostream& out, const std::string& value) {
    const auto size = static_cast<std::uint64_t>(value.size());
    write_pod(out, size);
    out.write(value.data(), static_cast<std::streamsize>(value.size()));
}

bool read_string(std::istream& in, std::string& value) {
    std::uint64_t size = 0;
    if (!read_pod(in, size)) {
        return false;
    }
    if (size > static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max())) {
        return false;
    }
    value.resize(static_cast<std::size_t>(size));
    return static_cast<bool>(in.read(value.data(), static_cast<std::streamsize>(value.size())));
}

void write_optional_string(std::ostream& out, const std::optional<std::string>& value) {
    const std::uint8_t present = value.has_value() ? 1U : 0U;
    write_pod(out, present);
    if (value.has_value()) {
        write_string(out, *value);
    }
}

bool read_optional_string(std::istream& in, std::optional<std::string>& value) {
    std::uint8_t present = 0;
    if (!read_pod(in, present)) {
        return false;
    }
    if (present == 0) {
        value = std::nullopt;
        return true;
    }
    std::string decoded;
    if (!read_string(in, decoded)) {
        return false;
    }
    value = std::move(decoded);
    return true;
}

std::string board_token(const std::vector<std::uint8_t>& board) {
    if (board.empty()) {
        return "preflop";
    }
    return sorted_card_string(board);
}

}  // namespace

RangeCacheKey make_range_cache_key(
    const HUNLConfig& config,
    std::uint8_t player,
    RangeVector::Kind range_kind) {
    RangeCacheKey key;
    key.player = player;
    key.street = config.starting_street;
    key.board = config.initial_board;
    key.starting_stack = config.starting_stack;
    key.small_blind = config.small_blind;
    key.big_blind = config.big_blind;
    key.ante = config.ante;
    key.initial_pot = config.initial_pot;
    key.initial_contributions = config.initial_contributions;
    key.abstraction_path = config.abstraction_path;
    key.abstraction_version = config.abstraction_version;
    key.range_kind = range_kind;
    return key;
}

bool range_cache_key_matches(
    const RangeCacheKey& key,
    const HUNLConfig& config,
    std::uint8_t player,
    RangeVector::Kind range_kind) {
    return key.player == player &&
           key.street == config.starting_street &&
           key.board == config.initial_board &&
           key.starting_stack == config.starting_stack &&
           key.small_blind == config.small_blind &&
           key.big_blind == config.big_blind &&
           key.ante == config.ante &&
           key.initial_pot == config.initial_pot &&
           key.initial_contributions == config.initial_contributions &&
           key.abstraction_path == config.abstraction_path &&
           key.abstraction_version == config.abstraction_version &&
           key.range_kind == range_kind;
}

std::string range_cache_basename(const RangeCacheKey& key) {
    std::ostringstream oss;
    oss << "p" << static_cast<unsigned>(key.player)
        << "_" << street_token(key.street)
        << "_" << board_token(key.board)
        << "_s" << key.starting_stack
        << "_b" << key.big_blind
        << "_" << (key.range_kind == RangeVector::Kind::Combo ? "combo" : "bucket");
    return oss.str();
}

bool save_range_cache_entry(const std::filesystem::path& path, const RangeCacheEntry& entry) {
    std::ofstream out(path, std::ios::binary);
    if (!out) {
        return false;
    }

    out.write(RANGE_CACHE_MAGIC.data(), static_cast<std::streamsize>(RANGE_CACHE_MAGIC.size()));
    write_pod(out, RANGE_CACHE_VERSION);
    write_pod(out, entry.key.player);
    write_pod(out, entry.key.street);
    write_pod(out, entry.key.starting_stack);
    write_pod(out, entry.key.small_blind);
    write_pod(out, entry.key.big_blind);
    write_pod(out, entry.key.ante);
    write_pod(out, entry.key.initial_pot);
    write_pod(out, entry.key.initial_contributions[0]);
    write_pod(out, entry.key.initial_contributions[1]);
    write_pod(out, entry.key.range_kind);
    write_optional_string(out, entry.key.abstraction_path);
    write_optional_string(out, entry.key.abstraction_version);
    const auto board_size = static_cast<std::uint64_t>(entry.key.board.size());
    write_pod(out, board_size);
    for (const auto card : entry.key.board) {
        write_pod(out, card);
    }
    write_pod(out, entry.iterations);
    write_pod(out, entry.exploitability);
    write_string(out, entry.label);
    serialize(out, entry.range.range);
    serialize(out, entry.range.mask);
    return static_cast<bool>(out);
}

bool load_range_cache_entry(const std::filesystem::path& path, RangeCacheEntry& entry) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        return false;
    }

    std::array<char, 8> magic{};
    if (!in.read(magic.data(), static_cast<std::streamsize>(magic.size())) || magic != RANGE_CACHE_MAGIC) {
        return false;
    }
    std::uint8_t version = 0;
    if (!read_pod(in, version) || version != RANGE_CACHE_VERSION) {
        return false;
    }

    if (!read_pod(in, entry.key.player) ||
        !read_pod(in, entry.key.street) ||
        !read_pod(in, entry.key.starting_stack) ||
        !read_pod(in, entry.key.small_blind) ||
        !read_pod(in, entry.key.big_blind) ||
        !read_pod(in, entry.key.ante) ||
        !read_pod(in, entry.key.initial_pot) ||
        !read_pod(in, entry.key.initial_contributions[0]) ||
        !read_pod(in, entry.key.initial_contributions[1]) ||
        !read_pod(in, entry.key.range_kind) ||
        !read_optional_string(in, entry.key.abstraction_path) ||
        !read_optional_string(in, entry.key.abstraction_version)) {
        return false;
    }

    std::uint64_t board_size = 0;
    if (!read_pod(in, board_size) ||
        board_size > static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max())) {
        return false;
    }
    entry.key.board.resize(static_cast<std::size_t>(board_size));
    for (auto& card : entry.key.board) {
        if (!read_pod(in, card)) {
            return false;
        }
    }

    if (!read_pod(in, entry.iterations) ||
        !read_pod(in, entry.exploitability) ||
        !read_string(in, entry.label) ||
        !deserialize(in, entry.range.range) ||
        !deserialize(in, entry.range.mask)) {
        return false;
    }
    entry.range.source_kind = RangeSourceKind::CachedFile;
    entry.range.context.source_path = path;
    entry.range.context.street = entry.key.street;
    if (!entry.label.empty()) {
        entry.range.context.label = entry.label;
    }
    return true;
}

std::optional<RangeCacheEntry> load_range_cache_if_compatible(
    const std::filesystem::path& path,
    const HUNLConfig& config,
    std::uint8_t player,
    RangeVector::Kind range_kind) {
    RangeCacheEntry entry;
    if (!load_range_cache_entry(path, entry)) {
        return std::nullopt;
    }
    if (!range_cache_key_matches(entry.key, config, player, range_kind)) {
        return std::nullopt;
    }
    return entry;
}

}  // namespace core
