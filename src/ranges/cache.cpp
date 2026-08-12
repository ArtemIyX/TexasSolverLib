#include "ranges/cache.hpp"

#include <array>
#include <cmath>
#include <cstring>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <limits>
#include <new>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <type_traits>

namespace texas::ranges {

namespace {

constexpr std::array<char, 8> RANGE_CACHE_MAGIC{{'T', 'S', 'R', 'C', 'A', 'C', 'H', 'E'}};
constexpr std::uint8_t RANGE_CACHE_VERSION = 2;
constexpr std::uint64_t MAX_CACHE_STRING_BYTES = 1'048'576ULL;
constexpr std::uint64_t MAX_CACHE_BOARD_CARDS = 5ULL;

class ContractFingerprint {
public:
    void add_byte(std::uint8_t value) noexcept {
        value_ ^= value;
        value_ *= 1099511628211ULL;
    }

    template <class Integer>
    void add_integer(Integer value) noexcept {
        using Unsigned = std::make_unsigned_t<Integer>;
        auto bits = static_cast<Unsigned>(value);
        for (std::size_t index = 0; index < sizeof(Unsigned); ++index) {
            add_byte(static_cast<std::uint8_t>(bits & 0xffU));
            bits >>= 8U;
        }
    }

    void add_bool(bool value) noexcept {
        add_byte(value ? 1U : 0U);
    }

    void add_double(double value) noexcept {
        std::uint64_t bits = 0;
        static_assert(sizeof(bits) == sizeof(value), "double fingerprint size");
        std::memcpy(&bits, &value, sizeof(bits));
        add_integer(bits);
    }

    void add_string(const std::string& value) noexcept {
        add_integer<std::uint64_t>(value.size());
        for (const auto ch : value) {
            add_byte(static_cast<std::uint8_t>(ch));
        }
    }

    [[nodiscard]] std::uint64_t value() const noexcept {
        return value_;
    }

private:
    std::uint64_t value_ = 14695981039346656037ULL;
};

template <class T, class AddValue>
void add_optional(
    ContractFingerprint& fingerprint,
    const std::optional<T>& value,
    AddValue add_value) noexcept {
    fingerprint.add_bool(value.has_value());
    if (value.has_value()) {
        add_value(*value);
    }
}

void add_doubles(
    ContractFingerprint& fingerprint,
    const std::vector<double>& values) noexcept {
    fingerprint.add_integer<std::uint64_t>(values.size());
    for (const auto value : values) {
        fingerprint.add_double(value);
    }
}

void add_range_input(
    ContractFingerprint& fingerprint,
    const HUNLRangeInput& range) noexcept {
    fingerprint.add_integer<std::uint64_t>(range.hand_weights.size());
    for (const auto& weighted : range.hand_weights) {
        fingerprint.add_byte(weighted.hole[0]);
        fingerprint.add_byte(weighted.hole[1]);
        fingerprint.add_double(weighted.weight);
    }
    fingerprint.add_integer<std::uint64_t>(range.bucket_weights.size());
    for (const auto& weighted : range.bucket_weights) {
        fingerprint.add_byte(static_cast<std::uint8_t>(weighted.street));
        fingerprint.add_integer(weighted.bucket);
        fingerprint.add_double(weighted.weight);
    }
}

std::uint64_t solve_contract_fingerprint(const HUNLConfig& config) noexcept {
    ContractFingerprint fingerprint;
    fingerprint.add_integer(config.starting_stack);
    fingerprint.add_integer(config.small_blind);
    fingerprint.add_integer(config.big_blind);
    fingerprint.add_integer(config.ante);
    fingerprint.add_byte(static_cast<std::uint8_t>(config.starting_street));
    fingerprint.add_integer<std::uint64_t>(config.initial_board.size());
    for (const auto card : config.initial_board) fingerprint.add_byte(card);
    fingerprint.add_integer(config.initial_pot);
    for (const auto contribution : config.initial_contributions) {
        fingerprint.add_integer(contribution);
    }
    add_optional(
        fingerprint,
        config.initial_hole_cards,
        [&](const auto& holes) noexcept {
            for (const auto& hole : holes) {
                fingerprint.add_byte(hole[0]);
                fingerprint.add_byte(hole[1]);
            }
        });
    fingerprint.add_byte(config.preflop_raise_cap);
    fingerprint.add_byte(config.postflop_raise_cap);
    add_doubles(fingerprint, config.bet_size_fractions);
    add_optional(
        fingerprint,
        config.flop_bet_fractions,
        [&](const auto& values) noexcept { add_doubles(fingerprint, values); });
    add_optional(
        fingerprint,
        config.turn_bet_fractions,
        [&](const auto& values) noexcept { add_doubles(fingerprint, values); });
    add_optional(
        fingerprint,
        config.river_bet_fractions,
        [&](const auto& values) noexcept { add_doubles(fingerprint, values); });
    add_doubles(fingerprint, config.raise_size_xs);
    fingerprint.add_bool(config.include_all_in);
    add_optional(
        fingerprint,
        config.auto_all_in_spr_threshold,
        [&](double value) noexcept { fingerprint.add_double(value); });
    fingerprint.add_integer(config.force_allin_threshold);
    fingerprint.add_integer(config.min_bet_bb);
    fingerprint.add_bool(config.allow_oop_flop_lead);
    fingerprint.add_double(config.rake_rate);
    fingerprint.add_integer(config.rake_cap);
    for (const auto count : config.bucket_counts_by_street) {
        fingerprint.add_integer(count);
    }
    add_optional(
        fingerprint,
        config.abstraction_path,
        [&](const auto& value) noexcept { fingerprint.add_string(value); });
    add_optional(
        fingerprint,
        config.abstraction_version,
        [&](const auto& value) noexcept { fingerprint.add_string(value); });
    fingerprint.add_integer(config.depth_limit_plies);
    fingerprint.add_byte(static_cast<std::uint8_t>(config.flat_solve_mode));
    fingerprint.add_byte(static_cast<std::uint8_t>(config.range_policy));
    for (const auto& range : config.initial_ranges) {
        add_optional(
            fingerprint,
            range,
            [&](const auto& value) noexcept {
                add_range_input(fingerprint, value);
            });
    }
    for (const auto& range : config.player_ranges) {
        add_optional(
            fingerprint,
            range,
            [&](const auto& value) noexcept {
                add_range_input(fingerprint, value);
            });
    }
    fingerprint.add_bool(config.use_pcs);
    return fingerprint.value();
}

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
    if (size > MAX_CACHE_STRING_BYTES ||
        size > static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max())) {
        return false;
    }
    std::string decoded;
    try {
        decoded.resize(static_cast<std::size_t>(size));
    } catch (const std::bad_alloc&) {
        return false;
    } catch (const std::length_error&) {
        return false;
    }
    if (!in.read(
            decoded.data(),
            static_cast<std::streamsize>(decoded.size()))) {
        return false;
    }
    value = std::move(decoded);
    return true;
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
    if (present == 0U) {
        value = std::nullopt;
        return true;
    }
    if (present != 1U) {
        return false;
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

bool valid_range_kind(RangeVector::Kind kind) noexcept {
    return kind == RangeVector::Kind::Combo ||
           kind == RangeVector::Kind::Bucket;
}

bool valid_cache_key_metadata(const RangeCacheKey& key) {
    if (key.player > 1U || !valid_range_kind(key.range_kind)) {
        return false;
    }
    try {
        HUNLConfig config;
        config.starting_stack = key.starting_stack;
        config.small_blind = key.small_blind;
        config.big_blind = key.big_blind;
        config.ante = key.ante;
        config.starting_street = key.street;
        config.initial_board = key.board;
        config.initial_pot = key.initial_pot;
        config.initial_contributions = key.initial_contributions;
        config.abstraction_path = key.abstraction_path;
        config.abstraction_version = key.abstraction_version;
        config.validate();
    } catch (const std::exception&) {
        return false;
    }
    return true;
}

bool valid_cache_range(const RangeCacheEntry& entry) noexcept {
    const auto& range = entry.range.range;
    const auto& mask = entry.range.mask;
    if (!valid_range_kind(range.kind) ||
        range.kind != entry.key.range_kind ||
        mask.kind != range.kind ||
        range.weights.empty() ||
        range.weights.size() != mask.enabled.size()) {
        return false;
    }
    double total = 0.0;
    for (std::size_t index = 0; index < range.weights.size(); ++index) {
        const auto weight = range.weights[index];
        if (!std::isfinite(weight) ||
            weight < 0.0 ||
            mask.enabled[index] > 1U ||
            (mask.enabled[index] == 0U && weight != 0.0)) {
            return false;
        }
        total += weight;
    }
    return std::isfinite(total) &&
           std::abs(total - 1.0) <= 1e-9;
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
    key.solve_contract_fingerprint = solve_contract_fingerprint(config);
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
           key.range_kind == range_kind &&
           key.solve_contract_fingerprint == solve_contract_fingerprint(config);
}

std::string range_cache_basename(const RangeCacheKey& key) {
    std::ostringstream oss;
    oss << "p" << static_cast<unsigned>(key.player)
        << "_" << street_token(key.street)
        << "_" << board_token(key.board)
        << "_s" << key.starting_stack
        << "_b" << key.big_blind
        << "_" << (key.range_kind == RangeVector::Kind::Combo ? "combo" : "bucket")
        << "_c" << std::hex << std::setw(16) << std::setfill('0')
        << key.solve_contract_fingerprint;
    return oss.str();
}

bool save_range_cache_entry(const std::filesystem::path& path, const RangeCacheEntry& entry) {
    if (entry.key.board.size() > MAX_CACHE_BOARD_CARDS ||
        (entry.key.abstraction_path.has_value() &&
         entry.key.abstraction_path->size() > MAX_CACHE_STRING_BYTES) ||
        (entry.key.abstraction_version.has_value() &&
         entry.key.abstraction_version->size() > MAX_CACHE_STRING_BYTES) ||
        entry.label.size() > MAX_CACHE_STRING_BYTES ||
        !valid_cache_key_metadata(entry.key) ||
        !std::isfinite(entry.exploitability) ||
        entry.exploitability < 0.0 ||
        !valid_cache_range(entry)) {
        return false;
    }
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
    write_pod(out, entry.key.solve_contract_fingerprint);
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

    RangeCacheEntry decoded;
    if (!read_pod(in, decoded.key.player) ||
        !read_pod(in, decoded.key.street) ||
        !read_pod(in, decoded.key.starting_stack) ||
        !read_pod(in, decoded.key.small_blind) ||
        !read_pod(in, decoded.key.big_blind) ||
        !read_pod(in, decoded.key.ante) ||
        !read_pod(in, decoded.key.initial_pot) ||
        !read_pod(in, decoded.key.initial_contributions[0]) ||
        !read_pod(in, decoded.key.initial_contributions[1]) ||
        !read_pod(in, decoded.key.range_kind) ||
        !read_pod(in, decoded.key.solve_contract_fingerprint) ||
        !read_optional_string(in, decoded.key.abstraction_path) ||
        !read_optional_string(in, decoded.key.abstraction_version)) {
        return false;
    }

    std::uint64_t board_size = 0;
    if (!read_pod(in, board_size) ||
        board_size > MAX_CACHE_BOARD_CARDS ||
        board_size > static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max())) {
        return false;
    }
    decoded.key.board.resize(static_cast<std::size_t>(board_size));
    for (auto& card : decoded.key.board) {
        if (!read_pod(in, card)) {
            return false;
        }
    }

    if (!read_pod(in, decoded.iterations) ||
        !read_pod(in, decoded.exploitability) ||
        !read_string(in, decoded.label) ||
        !deserialize(in, decoded.range.range) ||
        !deserialize(in, decoded.range.mask) ||
        !valid_cache_key_metadata(decoded.key) ||
        !std::isfinite(decoded.exploitability) ||
        decoded.exploitability < 0.0 ||
        !valid_cache_range(decoded) ||
        in.peek() != std::char_traits<char>::eof()) {
        return false;
    }
    decoded.range.source_kind = RangeSourceKind::CachedFile;
    decoded.range.context.source_path = path;
    decoded.range.context.street = decoded.key.street;
    if (!decoded.label.empty()) {
        decoded.range.context.label = decoded.label;
    }
    entry = std::move(decoded);
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

}  // namespace texas::ranges
