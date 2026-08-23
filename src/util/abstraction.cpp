#include "util/abstraction.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstring>
#include <fstream>
#include <limits>
#include <stdexcept>
#include <string_view>
#include <vector>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <compressapi.h>

#if defined(_MSC_VER)
#pragma comment(lib, "Cabinet.lib")
#endif

#ifndef COMPRESS_ALGORITHM_DEFLATE
#define COMPRESS_ALGORITHM_DEFLATE 4
#endif

namespace texas::util {

namespace {

constexpr std::size_t kMaxAbstractionArtifactBytes = 4ULL * 1024ULL * 1024ULL * 1024ULL;
constexpr std::size_t kMaxAbstractionEntryBytes = 512ULL * 1024ULL * 1024ULL;

bool fits(std::size_t offset, std::size_t length, std::size_t size) noexcept {
    return offset <= size && length <= size - offset;
}

struct ZipEntry {
    std::string name;
    std::uint32_t crc32 = 0;
    std::uint16_t method = 0;
    std::uint32_t compressed_size = 0;
    std::uint32_t uncompressed_size = 0;
    std::uint32_t local_header_offset = 0;
};

std::uint32_t crc32_bytes(const std::uint8_t* data, std::size_t size) noexcept {
    std::uint32_t crc = 0xFFFFFFFFU;
    for (std::size_t index = 0U; index < size; ++index) {
        crc ^= data[index];
        for (int bit = 0; bit < 8; ++bit) {
            crc = (crc & 1U) != 0U ? 0xEDB88320U ^ (crc >> 1U) : crc >> 1U;
        }
    }
    return ~crc;
}

std::string read_file_bytes(const std::filesystem::path& path) {
    std::error_code error;
    const auto file_size = std::filesystem::file_size(path, error);
    if (error || file_size > kMaxAbstractionArtifactBytes ||
        file_size > std::numeric_limits<std::size_t>::max()) {
        throw std::runtime_error("abstraction artifact is missing or exceeds the input budget");
    }
    std::ifstream f(path, std::ios::binary);
    if (!f) {
        throw std::runtime_error("abstraction artifact not found: " + path.string());
    }
    std::string bytes(static_cast<std::size_t>(file_size), '\0');
    f.read(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    if (!f && !f.eof()) throw std::runtime_error("abstraction artifact read failed");
    return bytes;
}

std::uint16_t read_u16(const char* p) {
    return static_cast<std::uint16_t>(static_cast<unsigned char>(p[0]) |
                                      (static_cast<unsigned char>(p[1]) << 8));
}

std::uint32_t read_u32(const char* p) {
    return static_cast<std::uint32_t>(static_cast<unsigned char>(p[0]) |
                                      (static_cast<unsigned char>(p[1]) << 8) |
                                      (static_cast<unsigned char>(p[2]) << 16) |
                                      (static_cast<unsigned char>(p[3]) << 24));
}

std::vector<ZipEntry> parse_zip_entries(const std::string& zip) {
    constexpr std::uint32_t EOCD_SIG = 0x06054b50;
    constexpr std::uint32_t CEN_SIG = 0x02014b50;
    const auto min_size = static_cast<std::ptrdiff_t>(22);
    if (zip.size() < static_cast<std::size_t>(min_size)) {
        throw std::runtime_error("npz too small");
    }

    std::ptrdiff_t eocd_pos = -1;
    for (std::ptrdiff_t i = static_cast<std::ptrdiff_t>(zip.size()) - 22; i >= 0 &&
                                i >= static_cast<std::ptrdiff_t>(zip.size()) - 0x10000 - 22; --i) {
        if (read_u32(zip.data() + i) == EOCD_SIG) {
            eocd_pos = i;
            break;
        }
    }
    if (eocd_pos < 0) {
        throw std::runtime_error("npz missing EOCD");
    }

    if (!fits(static_cast<std::size_t>(eocd_pos), 22U, zip.size())) {
        throw std::runtime_error("npz EOCD is truncated");
    }
    const auto cd_offset = read_u32(zip.data() + eocd_pos + 16);
    const auto cd_entries = read_u16(zip.data() + eocd_pos + 10);
    if (cd_offset > zip.size() || cd_offset > static_cast<std::size_t>(eocd_pos)) {
        throw std::runtime_error("npz central directory offset is invalid");
    }
    std::vector<ZipEntry> out;
    out.reserve(cd_entries);

    std::size_t pos = cd_offset;
    for (std::uint16_t n = 0; n < cd_entries; ++n) {
        if (!fits(pos, 46U, zip.size()) || pos > static_cast<std::size_t>(eocd_pos)) {
            throw std::runtime_error("npz central directory entry is truncated");
        }
        if (read_u32(zip.data() + pos) != CEN_SIG) {
            throw std::runtime_error("npz central directory corruption");
        }
        ZipEntry e;
        e.crc32 = read_u32(zip.data() + pos + 16);
        e.method = read_u16(zip.data() + pos + 10);
        e.compressed_size = read_u32(zip.data() + pos + 20);
        e.uncompressed_size = read_u32(zip.data() + pos + 24);
        const auto name_len = read_u16(zip.data() + pos + 28);
        const auto extra_len = read_u16(zip.data() + pos + 30);
        const auto comment_len = read_u16(zip.data() + pos + 32);
        e.local_header_offset = read_u32(zip.data() + pos + 42);
        const auto record_size = static_cast<std::size_t>(46U) + name_len + extra_len + comment_len;
        if (!fits(pos, record_size, zip.size()) || pos + record_size > static_cast<std::size_t>(eocd_pos) ||
            e.local_header_offset >= zip.size()) {
            throw std::runtime_error("npz central directory entry bounds are invalid");
        }
        e.name.assign(zip.data() + pos + 46, zip.data() + pos + 46 + name_len);
        out.push_back(std::move(e));
        pos += record_size;
    }
    return out;
}

std::vector<std::uint8_t> decompress_deflate(const std::uint8_t* input, std::size_t input_size, std::size_t output_size) {
    if (input == nullptr || input_size == 0U || output_size > kMaxAbstractionEntryBytes) {
        throw std::runtime_error("npz deflate entry exceeds the output budget");
    }
    DECOMPRESSOR_HANDLE handle = nullptr;
    if (!CreateDecompressor(COMPRESS_ALGORITHM_DEFLATE, nullptr, &handle)) {
        throw std::runtime_error("CreateDecompressor(COMPRESS_ALGORITHM_DEFLATE) failed");
    }

    std::vector<std::uint8_t> output(output_size);
    std::size_t produced = output_size;
    if (!Decompress(handle, const_cast<void*>(static_cast<const void*>(input)), input_size, output.data(), output_size, &produced)) {
        CloseDecompressor(handle);
        throw std::runtime_error("Decompress failed");
    }
    CloseDecompressor(handle);
    if (produced > output_size) {
        throw std::runtime_error("npz deflate output exceeds the declared size");
    }
    output.resize(produced);
    return output;
}

std::vector<std::uint8_t> read_zip_entry(const std::string& zip, const ZipEntry& entry) {
    constexpr std::uint32_t LOC_SIG = 0x04034b50;
    if (entry.compressed_size > kMaxAbstractionEntryBytes ||
        entry.uncompressed_size > kMaxAbstractionEntryBytes ||
        !fits(entry.local_header_offset, 30U, zip.size())) {
        throw std::runtime_error("npz local entry exceeds the input or output budget");
    }
    const auto* p = zip.data() + entry.local_header_offset;
    if (read_u32(p) != LOC_SIG) {
        throw std::runtime_error("npz local header corruption");
    }
    const auto name_len = read_u16(p + 26);
    const auto extra_len = read_u16(p + 28);
    const auto data_prefix = static_cast<std::size_t>(30U) + name_len + extra_len;
    if (!fits(entry.local_header_offset, data_prefix, zip.size())) {
        throw std::runtime_error("npz local entry header bounds are invalid");
    }
    const auto data_offset = entry.local_header_offset + data_prefix;
    if (!fits(data_offset, entry.compressed_size, zip.size())) {
        throw std::runtime_error("npz local entry data bounds are invalid");
    }
    const auto* data = reinterpret_cast<const std::uint8_t*>(zip.data() + data_offset);
    if (entry.method == 0) {
        if (entry.compressed_size != entry.uncompressed_size) {
            throw std::runtime_error("npz stored entry size metadata is inconsistent");
        }
        std::vector<std::uint8_t> output(data, data + entry.uncompressed_size);
        if (crc32_bytes(output.data(), output.size()) != entry.crc32) {
            throw std::runtime_error("npz stored entry CRC mismatch");
        }
        return output;
    }
    if (entry.method == 8) {
        auto output = decompress_deflate(data, entry.compressed_size, entry.uncompressed_size);
        if (crc32_bytes(output.data(), output.size()) != entry.crc32) {
            throw std::runtime_error("npz deflated entry CRC mismatch");
        }
        return output;
    }
    throw std::runtime_error("unsupported zip compression method");
}

std::vector<std::uint8_t> parse_npy_u8(const std::vector<std::uint8_t>& raw) {
    constexpr std::string_view MAGIC = "\x93NUMPY";
    if (raw.size() < 10 || std::string_view(reinterpret_cast<const char*>(raw.data()), 6) != MAGIC) {
        throw std::runtime_error("invalid npy magic");
    }
    const std::uint8_t major = raw[6];
    std::size_t header_len = 0;
    std::size_t offset = 0;
    if (major == 1) {
        header_len = read_u16(reinterpret_cast<const char*>(raw.data() + 8));
        offset = 10;
    } else if (major == 2 || major == 3) {
        header_len = read_u32(reinterpret_cast<const char*>(raw.data() + 8));
        offset = 12;
    } else {
        throw std::runtime_error("unsupported npy version");
    }
    const auto header_start = offset;
    if (!fits(header_start, header_len, raw.size())) {
        throw std::runtime_error("truncated npy header");
    }
    const auto header_end = header_start + header_len;
    const std::string header(reinterpret_cast<const char*>(raw.data() + header_start), header_len);
    if (header.find("'descr': '|u1'") == std::string::npos && header.find("\"descr\": \"|u1\"") == std::string::npos) {
        throw std::runtime_error("npy dtype must be |u1");
    }
    const auto pos = header.find("shape");
    if (pos == std::string::npos) {
        throw std::runtime_error("npy shape missing");
    }
    const auto data_start = header_end;
    return std::vector<std::uint8_t>(raw.begin() + static_cast<std::ptrdiff_t>(data_start), raw.end());
}

std::unordered_map<std::string, std::uint32_t> parse_str_int_dict(const std::vector<std::uint8_t>& raw) {
    const std::string text(raw.begin(), raw.end());
    std::unordered_map<std::string, std::uint32_t> out;
    std::size_t i = 0;
    auto skip_ws = [&]() {
        while (i < text.size() && std::isspace(static_cast<unsigned char>(text[i]))) ++i;
    };
    skip_ws();
    if (i >= text.size() || text[i] != '{') throw std::runtime_error("json object expected");
    ++i;
    while (true) {
        skip_ws();
        if (i < text.size() && text[i] == '}') {
            ++i;
            break;
        }
        if (i >= text.size() || text[i] != '"') throw std::runtime_error("json key expected");
        ++i;
        const auto key_start = i;
        while (i < text.size() && text[i] != '"') ++i;
        if (i >= text.size()) throw std::runtime_error("unterminated json key");
        std::string key = text.substr(key_start, i - key_start);
        ++i;
        skip_ws();
        if (i >= text.size() || text[i] != ':') throw std::runtime_error("json colon expected");
        ++i;
        skip_ws();
        const auto val_start = i;
        while (i < text.size() && (std::isdigit(static_cast<unsigned char>(text[i])) || text[i] == '-')) ++i;
        out.emplace(std::move(key), static_cast<std::uint32_t>(std::stoul(text.substr(val_start, i - val_start))));
        skip_ws();
        if (i < text.size() && text[i] == ',') {
            ++i;
            continue;
        }
        if (i < text.size() && text[i] == '}') {
            ++i;
            break;
        }
        throw std::runtime_error("json object syntax error");
    }
    return out;
}

std::unordered_map<std::string, std::unordered_map<std::string, std::uint32_t>> parse_nested_dict(const std::vector<std::uint8_t>& raw) {
    const std::string text(raw.begin(), raw.end());
    std::unordered_map<std::string, std::unordered_map<std::string, std::uint32_t>> out;
    // This artifact only stores compact JSON object-of-object maps; a simple parser is enough.
    std::size_t i = 0;
    auto skip_ws = [&]() {
        while (i < text.size() && std::isspace(static_cast<unsigned char>(text[i]))) ++i;
    };
    skip_ws();
    if (i >= text.size() || text[i] != '{') throw std::runtime_error("json object expected");
    ++i;
    while (true) {
        skip_ws();
        if (i < text.size() && text[i] == '}') {
            ++i;
            break;
        }
        if (i >= text.size() || text[i] != '"') throw std::runtime_error("json key expected");
        ++i;
        const auto key_start = i;
        while (i < text.size() && text[i] != '"') ++i;
        if (i >= text.size()) throw std::runtime_error("unterminated json key");
        std::string key = text.substr(key_start, i - key_start);
        ++i;
        skip_ws();
        if (i >= text.size() || text[i] != ':') throw std::runtime_error("json colon expected");
        ++i;
        skip_ws();
        if (i >= text.size() || text[i] != '{') throw std::runtime_error("nested json object expected");
        ++i;
        std::unordered_map<std::string, std::uint32_t> inner;
        while (true) {
            skip_ws();
            if (i < text.size() && text[i] == '}') {
                ++i;
                break;
            }
            if (i >= text.size() || text[i] != '"') throw std::runtime_error("nested json key expected");
            ++i;
            const auto ik_start = i;
            while (i < text.size() && text[i] != '"') ++i;
            if (i >= text.size()) throw std::runtime_error("unterminated nested json key");
            std::string ik = text.substr(ik_start, i - ik_start);
            ++i;
            skip_ws();
            if (i >= text.size() || text[i] != ':') throw std::runtime_error("nested json colon expected");
            ++i;
            skip_ws();
            const auto iv_start = i;
            while (i < text.size() && (std::isdigit(static_cast<unsigned char>(text[i])) || text[i] == '-')) ++i;
            inner.emplace(std::move(ik), static_cast<std::uint32_t>(std::stoul(text.substr(iv_start, i - iv_start))));
            skip_ws();
            if (i < text.size() && text[i] == ',') {
                ++i;
                continue;
            }
            if (i < text.size() && text[i] == '}') {
                ++i;
                break;
            }
            throw std::runtime_error("nested json syntax error");
        }
        out.emplace(std::move(key), std::move(inner));
        skip_ws();
        if (i < text.size() && text[i] == ',') {
            ++i;
            continue;
        }
        if (i < text.size() && text[i] == '}') {
            ++i;
            break;
        }
        throw std::runtime_error("json object syntax error");
    }
    return out;
}

AbstractionMetadata parse_metadata(const std::vector<std::uint8_t>& raw) {
    const std::string text(raw.begin(), raw.end());
    AbstractionMetadata meta;
    auto find_num = [&](std::string_view key) -> std::uint64_t {
        const auto pos = text.find(key);
        if (pos == std::string::npos) return 0;
        auto start = text.find_first_of("0123456789", pos);
        auto end = start;
        while (end < text.size() && std::isdigit(static_cast<unsigned char>(text[end]))) ++end;
        return std::stoull(text.substr(start, end - start));
    };
    auto find_str = [&](std::string_view key) -> std::string {
        const auto pos = text.find(key);
        if (pos == std::string::npos) return {};
        auto start = text.find('"', pos + key.size());
        if (start == std::string::npos) return {};
        auto end = text.find('"', start + 1);
        if (end == std::string::npos) return {};
        return text.substr(start + 1, end - start - 1);
    };
    auto find_num_list = [&](std::string_view key) -> std::vector<std::uint16_t> {
        const auto pos = text.find(key);
        if (pos == std::string::npos) return {};
        auto start = text.find('[', pos);
        if (start == std::string::npos) return {};
        auto end = text.find(']', start);
        if (end == std::string::npos) return {};

        std::vector<std::uint16_t> values;
        std::size_t i = start + 1;
        while (i < end) {
            while (i < end && !std::isdigit(static_cast<unsigned char>(text[i]))) {
                ++i;
            }
            if (i >= end) {
                break;
            }
            std::size_t j = i;
            while (j < end && std::isdigit(static_cast<unsigned char>(text[j]))) {
                ++j;
            }
            values.push_back(static_cast<std::uint16_t>(std::stoul(text.substr(i, j - i))));
            i = j;
        }
        return values;
    };
    meta.schema_version = static_cast<std::uint8_t>(find_num("schema_version"));
    meta.version = find_str("\"version\"");
    meta.bucket_counts = find_num_list("bucket_counts");
    meta.feature_bins = static_cast<std::uint16_t>(find_num("feature_bins"));
    meta.seed = find_num("seed");
    return meta;
}

std::string permuted_hole_key(const std::array<std::uint8_t, 2>& hole, std::size_t perm_index) {
    std::array<std::uint8_t, 2> cards = {
        card_to_int(rank_of(hole[0]), SUIT_PERMUTATIONS[perm_index][suit_of(hole[0])]),
        card_to_int(rank_of(hole[1]), SUIT_PERMUTATIONS[perm_index][suit_of(hole[1])]),
    };
    if (cards[1] < cards[0]) std::swap(cards[0], cards[1]);
    return std::string(card_to_string(cards[0])) + card_to_string(cards[1]);
}

}  // namespace

std::string canonicalize_board(const std::vector<std::uint8_t>& board, std::size_t* perm_index) {
    std::vector<std::pair<std::uint8_t, std::uint8_t>> best;
    std::size_t best_idx = 0;
    bool first = true;
    for (std::size_t i = 0; i < SUIT_PERMUTATIONS.size(); ++i) {
        std::vector<std::pair<std::uint8_t, std::uint8_t>> pairs;
        pairs.reserve(board.size());
        for (auto c : board) {
            pairs.push_back({rank_of(c), SUIT_PERMUTATIONS[i][suit_of(c)]});
        }
        std::sort(pairs.begin(), pairs.end());
        if (first || pairs < best) {
            best = std::move(pairs);
            best_idx = i;
            first = false;
        }
    }
    if (perm_index) *perm_index = best_idx;
    std::string out;
    for (std::size_t i = 0; i < best.size(); ++i) {
        if (i) out += '_';
        out += "r" + std::to_string(best[i].first) + "s" + std::to_string(best[i].second);
    }
    return out;
}

std::pair<std::string, std::string> canonicalize(const std::vector<std::uint8_t>& board, const std::array<std::uint8_t, 2>& hole) {
    std::size_t perm_index = 0;
    const auto board_key = canonicalize_board(board, &perm_index);
    const auto hand_key = permuted_hole_key(hole, perm_index);
    return {board_key, hand_key};
}

AbstractionTables load_abstraction(const std::filesystem::path& path) {
    const auto bytes = read_file_bytes(path);
    const auto entries = parse_zip_entries(bytes);
    auto find_entry = [&](std::string_view name) -> const ZipEntry& {
        for (const auto& e : entries) {
            if (e.name == name) return e;
        }
        throw std::runtime_error("missing npz entry: " + std::string(name));
    };

    AbstractionTables tables;
    tables.flop_assignments = parse_npy_u8(read_zip_entry(bytes, find_entry("flop_assignments.npy")));
    tables.turn_assignments = parse_npy_u8(read_zip_entry(bytes, find_entry("turn_assignments.npy")));
    tables.river_assignments = parse_npy_u8(read_zip_entry(bytes, find_entry("river_assignments.npy")));

    tables.flop_board_index = parse_str_int_dict(parse_npy_u8(read_zip_entry(bytes, find_entry("flop_board_index.npy"))));
    tables.turn_board_index = parse_str_int_dict(parse_npy_u8(read_zip_entry(bytes, find_entry("turn_board_index.npy"))));
    tables.river_board_index = parse_str_int_dict(parse_npy_u8(read_zip_entry(bytes, find_entry("river_board_index.npy"))));

    tables.flop_hand_index = parse_nested_dict(parse_npy_u8(read_zip_entry(bytes, find_entry("flop_hand_index.npy"))));
    tables.turn_hand_index = parse_nested_dict(parse_npy_u8(read_zip_entry(bytes, find_entry("turn_hand_index.npy"))));
    tables.river_hand_index = parse_nested_dict(parse_npy_u8(read_zip_entry(bytes, find_entry("river_hand_index.npy"))));

    tables.metadata = parse_metadata(parse_npy_u8(read_zip_entry(bytes, find_entry("metadata.npy"))));
    if (tables.metadata.schema_version != ABSTRACTION_SCHEMA_VERSION) {
        throw std::runtime_error("abstraction schema mismatch");
    }
    if (!tables.metadata.version.empty()) {
        // The current C++ port does not yet thread abstraction version checks
        // from HUNLConfig, but we keep the metadata around for parity.
    }
    tables.source_path = path;
    return tables;
}

std::int32_t lookup_bucket(
    const AbstractionTables& tables,
    const std::vector<std::uint8_t>& board,
    const std::array<std::uint8_t, 2>& hole,
    Street street) {
    if (street == Street::Preflop) {
        return -1;
    }
    std::size_t perm_index = 0;
    const auto board_key = canonicalize_board(board, &perm_index);
    const auto hand_key = permuted_hole_key(hole, perm_index);

    const auto& board_index = street == Street::Flop ? tables.flop_board_index
                              : street == Street::Turn ? tables.turn_board_index
                              : tables.river_board_index;
    const auto& hand_index = street == Street::Flop ? tables.flop_hand_index
                             : street == Street::Turn ? tables.turn_hand_index
                             : tables.river_hand_index;
    const auto& assignments = street == Street::Flop ? tables.flop_assignments
                                : street == Street::Turn ? tables.turn_assignments
                                : tables.river_assignments;

    const auto board_offset = board_index.at(board_key);
    const auto& per_board = hand_index.at(board_key);
    const auto within = per_board.at(hand_key);
    if (board_offset >= assignments.size() || within >= assignments.size() - board_offset) {
        throw std::out_of_range("abstraction assignment index is outside the table");
    }
    return static_cast<std::int32_t>(assignments[board_offset + within]);
}

}  // namespace texas::util


