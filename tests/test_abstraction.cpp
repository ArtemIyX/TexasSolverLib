#include "core/abstraction.hpp"
#include "test_harness.hpp"

#include <array>
#include <cstdint>
#include <fstream>
#include <filesystem>
#include <string>
#include <vector>

namespace {

constexpr std::uint8_t c(std::uint8_t rank, std::uint8_t suit) {
    return core::card_to_int(rank, suit);
}

std::uint32_t crc32_byte(std::uint32_t crc, std::uint8_t b) {
    crc ^= b;
    for (int i = 0; i < 8; ++i) {
        crc = (crc & 1U) ? (0xEDB88320U ^ (crc >> 1U)) : (crc >> 1U);
    }
    return static_cast<std::uint16_t>(crc);
}

std::uint32_t crc32(const std::vector<std::uint8_t>& data) {
    std::uint32_t crc = 0xFFFFFFFFU;
    for (auto b : data) {
        crc = crc32_byte(crc, b);
    }
    return ~crc;
}

void append_u16(std::vector<std::uint8_t>& out, std::uint16_t v) {
    out.push_back(static_cast<std::uint8_t>(v & 0xFFU));
    out.push_back(static_cast<std::uint8_t>((v >> 8U) & 0xFFU));
}

void append_u32(std::vector<std::uint8_t>& out, std::uint32_t v) {
    out.push_back(static_cast<std::uint8_t>(v & 0xFFU));
    out.push_back(static_cast<std::uint8_t>((v >> 8U) & 0xFFU));
    out.push_back(static_cast<std::uint8_t>((v >> 16U) & 0xFFU));
    out.push_back(static_cast<std::uint8_t>((v >> 24U) & 0xFFU));
}

std::vector<std::uint8_t> make_npy_u8(const std::vector<std::uint8_t>& data) {
    std::vector<std::uint8_t> out;
    const std::string header_dict =
        "{'descr': '|u1', 'fortran_order': False, 'shape': (" + std::to_string(data.size()) + ",), }";
    std::string header = header_dict;
    while ((10 + header.size() + 1) % 16 != 0) {
        header.push_back(' ');
    }
    header.push_back('\n');

    out.insert(out.end(), {0x93, 'N', 'U', 'M', 'P', 'Y', 1, 0});
    append_u16(out, static_cast<std::uint16_t>(header.size()));
    out.insert(out.end(), header.begin(), header.end());
    out.insert(out.end(), data.begin(), data.end());
    return out;
}

std::vector<std::uint8_t> make_json_bytes(const std::string& json) {
    return std::vector<std::uint8_t>(json.begin(), json.end());
}

struct ZipBuilder {
    struct Entry {
        std::string name;
        std::vector<std::uint8_t> data;
        std::uint32_t crc = 0;
        std::uint32_t local_offset = 0;
    };

    std::vector<Entry> entries;

    void add(const std::string& name, const std::vector<std::uint8_t>& data) {
        entries.push_back(Entry{name, data, crc32(data), 0});
    }

    std::vector<std::uint8_t> finish() {
        std::vector<std::uint8_t> out;
        std::vector<std::uint8_t> central;
        for (auto& e : entries) {
            e.local_offset = static_cast<std::uint32_t>(out.size());
            const auto name_len = static_cast<std::uint16_t>(e.name.size());
            append_u32(out, 0x04034b50U);
            append_u16(out, 20);
            append_u16(out, 0);
            append_u16(out, 0);
            append_u16(out, 0);
            append_u16(out, 0);
            append_u32(out, e.crc);
            append_u32(out, static_cast<std::uint32_t>(e.data.size()));
            append_u32(out, static_cast<std::uint32_t>(e.data.size()));
            append_u16(out, name_len);
            append_u16(out, 0);
            out.insert(out.end(), e.name.begin(), e.name.end());
            out.insert(out.end(), e.data.begin(), e.data.end());

            append_u32(central, 0x02014b50U);
            append_u16(central, 20);
            append_u16(central, 20);
            append_u16(central, 0);
            append_u16(central, 0);
            append_u16(central, 0);
            append_u16(central, 0);
            append_u32(central, e.crc);
            append_u32(central, static_cast<std::uint32_t>(e.data.size()));
            append_u32(central, static_cast<std::uint32_t>(e.data.size()));
            append_u16(central, name_len);
            append_u16(central, 0);
            append_u16(central, 0);
            append_u16(central, 0);
            append_u16(central, 0);
            append_u32(central, 0);
            append_u32(central, e.local_offset);
            central.insert(central.end(), e.name.begin(), e.name.end());
        }

        const auto cd_offset = static_cast<std::uint32_t>(out.size());
        out.insert(out.end(), central.begin(), central.end());
        append_u32(out, 0x06054b50U);
        append_u16(out, 0);
        append_u16(out, 0);
        append_u16(out, static_cast<std::uint16_t>(entries.size()));
        append_u16(out, static_cast<std::uint16_t>(entries.size()));
        append_u32(out, static_cast<std::uint32_t>(central.size()));
        append_u32(out, cd_offset);
        append_u16(out, 0);
        return out;
    }
};

std::filesystem::path make_tmp_path(const std::string& name) {
    return std::filesystem::temp_directory_path() / name;
}

}  // namespace

TEST_CASE(abstraction_canonicalize_board_and_hole) {
    std::vector<std::uint8_t> board = {c(14, 0), c(13, 1), c(7, 2)};
    const std::array<std::uint8_t, 2> hole = {c(12, 0), c(11, 3)};
    const auto [board_key, hand_key] = core::canonicalize(board, hole);
    const auto [board_key_again, hand_key_again] = core::canonicalize(board, hole);
    EXPECT_EQ(board_key, board_key_again);
    EXPECT_EQ(hand_key, hand_key_again);
    EXPECT_TRUE(!board_key.empty());
    EXPECT_TRUE(!hand_key.empty());
}

TEST_CASE(abstraction_loads_minimal_npz_layout) {
    const auto tmp = make_tmp_path("texas_abstraction_test.npz");

    ZipBuilder zip;
    auto add_named = [&](const std::string& name, const std::vector<std::uint8_t>& raw) {
        zip.add(name, make_npy_u8(raw));
    };

    const std::vector<std::uint8_t> board = {c(14, 0), c(13, 1), c(7, 2)};
    const std::array<std::uint8_t, 2> hole = {c(12, 0), c(11, 3)};
    const auto [board_key, hand_key] = core::canonicalize(board, hole);
    const std::string board_index = "{\"" + board_key + "\":0}";
    const std::string hand_index = "{\"" + board_key + "\":{\"" + hand_key + "\":0}}";
    const std::string metadata =
        "{\"bucket_counts\":[1,1,1],\"feature_bins\":1,\"schema_version\":1,\"seed\":7,\"version\":\"v1\"}";

    add_named("flop_assignments.npy", {7});
    add_named("turn_assignments.npy", {7});
    add_named("river_assignments.npy", {7});
    add_named("flop_board_index.npy", make_json_bytes(board_index));
    add_named("turn_board_index.npy", make_json_bytes(board_index));
    add_named("river_board_index.npy", make_json_bytes(board_index));
    add_named("flop_hand_index.npy", make_json_bytes(hand_index));
    add_named("turn_hand_index.npy", make_json_bytes(hand_index));
    add_named("river_hand_index.npy", make_json_bytes(hand_index));
    add_named("metadata.npy", make_json_bytes(metadata));

    const auto bytes = zip.finish();
    {
        std::ofstream out(tmp, std::ios::binary);
        out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    }

    const auto tables = core::load_abstraction(tmp);
    EXPECT_EQ(tables.metadata.schema_version, 1U);
    EXPECT_EQ(tables.metadata.version, "v1");
    EXPECT_EQ(core::lookup_bucket(tables, board, hole, core::Street::Flop), 7);
    std::filesystem::remove(tmp);
}
