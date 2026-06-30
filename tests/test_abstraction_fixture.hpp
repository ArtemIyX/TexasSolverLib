#pragma once

#include "util/abstraction.hpp"

#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace test_support {

inline std::uint8_t c(std::uint8_t rank, std::uint8_t suit) {
    return core::card_to_int(rank, suit);
}

inline std::uint32_t crc32_byte(std::uint32_t crc, std::uint8_t b) {
    crc ^= b;
    for (int i = 0; i < 8; ++i) {
        crc = (crc & 1U) ? (0xEDB88320U ^ (crc >> 1U)) : (crc >> 1U);
    }
    return static_cast<std::uint16_t>(crc);
}

inline std::uint32_t crc32(const std::vector<std::uint8_t>& data) {
    std::uint32_t crc = 0xFFFFFFFFU;
    for (const auto byte : data) {
        crc = crc32_byte(crc, byte);
    }
    return ~crc;
}

inline void append_u16(std::vector<std::uint8_t>& out, std::uint16_t value) {
    out.push_back(static_cast<std::uint8_t>(value & 0xFFU));
    out.push_back(static_cast<std::uint8_t>((value >> 8U) & 0xFFU));
}

inline void append_u32(std::vector<std::uint8_t>& out, std::uint32_t value) {
    out.push_back(static_cast<std::uint8_t>(value & 0xFFU));
    out.push_back(static_cast<std::uint8_t>((value >> 8U) & 0xFFU));
    out.push_back(static_cast<std::uint8_t>((value >> 16U) & 0xFFU));
    out.push_back(static_cast<std::uint8_t>((value >> 24U) & 0xFFU));
}

inline std::vector<std::uint8_t> make_npy_u8(const std::vector<std::uint8_t>& data) {
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

inline std::vector<std::uint8_t> make_json_bytes(const std::string& json) {
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
        for (auto& entry : entries) {
            entry.local_offset = static_cast<std::uint32_t>(out.size());
            const auto name_len = static_cast<std::uint16_t>(entry.name.size());
            append_u32(out, 0x04034b50U);
            append_u16(out, 20);
            append_u16(out, 0);
            append_u16(out, 0);
            append_u16(out, 0);
            append_u16(out, 0);
            append_u32(out, entry.crc);
            append_u32(out, static_cast<std::uint32_t>(entry.data.size()));
            append_u32(out, static_cast<std::uint32_t>(entry.data.size()));
            append_u16(out, name_len);
            append_u16(out, 0);
            out.insert(out.end(), entry.name.begin(), entry.name.end());
            out.insert(out.end(), entry.data.begin(), entry.data.end());

            append_u32(central, 0x02014b50U);
            append_u16(central, 20);
            append_u16(central, 20);
            append_u16(central, 0);
            append_u16(central, 0);
            append_u16(central, 0);
            append_u16(central, 0);
            append_u32(central, entry.crc);
            append_u32(central, static_cast<std::uint32_t>(entry.data.size()));
            append_u32(central, static_cast<std::uint32_t>(entry.data.size()));
            append_u16(central, name_len);
            append_u16(central, 0);
            append_u16(central, 0);
            append_u16(central, 0);
            append_u16(central, 0);
            append_u32(central, 0);
            append_u32(central, entry.local_offset);
            central.insert(central.end(), entry.name.begin(), entry.name.end());
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

inline std::filesystem::path make_tmp_path(const std::string& name) {
    return std::filesystem::temp_directory_path() / name;
}

inline std::vector<std::array<std::uint8_t, 2>> enumerate_live_hands(const std::vector<std::uint8_t>& board) {
    std::array<bool, 64> blocked = {};
    for (const auto card : board) {
        blocked[card] = true;
    }

    std::vector<std::uint8_t> live_cards;
    for (std::uint8_t rank = 2; rank <= 14; ++rank) {
        for (std::uint8_t suit = 0; suit < 4; ++suit) {
            const auto card = core::card_to_int(rank, suit);
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

struct AbstractionFixtureOptions {
    std::vector<std::uint16_t> bucket_counts = {1, 1, 1};
    std::uint8_t schema_version = core::ABSTRACTION_SCHEMA_VERSION;
    std::string version = "v1";
    std::optional<std::string> omit_entry = std::nullopt;
};

struct StreetFixtureData {
    std::vector<std::uint8_t> assignments;
    std::string board_index_json = "{}";
    std::string hand_index_json = "{}";
};

inline StreetFixtureData make_street_fixture_data(
    core::Street street,
    const std::optional<std::vector<std::uint8_t>>& board,
    const std::function<std::uint8_t(core::Street, std::size_t, const std::array<std::uint8_t, 2>&)>& bucket_for) {
    StreetFixtureData data;
    if (!board.has_value()) {
        return data;
    }

    const auto board_key = core::canonicalize_board(*board);
    const auto live_hands = enumerate_live_hands(*board);
    data.board_index_json = "{\"" + board_key + "\":0}";
    data.hand_index_json = "{\"" + board_key + "\":{";
    data.assignments.reserve(live_hands.size());

    for (std::size_t i = 0; i < live_hands.size(); ++i) {
        const auto [canonical_board_key, hand_key] = core::canonicalize(*board, live_hands[i]);
        (void)canonical_board_key;
        if (i > 0) {
            data.hand_index_json += ",";
        }
        data.hand_index_json += "\"" + hand_key + "\":" + std::to_string(i);
        data.assignments.push_back(bucket_for(street, i, live_hands[i]));
    }
    data.hand_index_json += "}}";
    return data;
}

inline std::filesystem::path write_abstraction_fixture(
    const std::string& name,
    const std::optional<std::vector<std::uint8_t>>& flop_board,
    const std::optional<std::vector<std::uint8_t>>& turn_board,
    const std::optional<std::vector<std::uint8_t>>& river_board,
    const std::function<std::uint8_t(core::Street, std::size_t, const std::array<std::uint8_t, 2>&)>& bucket_for,
    const AbstractionFixtureOptions& options = {}) {
    const auto tmp = make_tmp_path(name);
    const auto flop = make_street_fixture_data(core::Street::Flop, flop_board, bucket_for);
    const auto turn = make_street_fixture_data(core::Street::Turn, turn_board, bucket_for);
    const auto river = make_street_fixture_data(core::Street::River, river_board, bucket_for);

    std::string metadata = "{\"bucket_counts\":[";
    for (std::size_t i = 0; i < options.bucket_counts.size(); ++i) {
        if (i > 0) {
            metadata += ",";
        }
        metadata += std::to_string(options.bucket_counts[i]);
    }
    metadata += "],\"feature_bins\":1,\"schema_version\":";
    metadata += std::to_string(options.schema_version);
    metadata += ",\"seed\":7,\"version\":\"";
    metadata += options.version;
    metadata += "\"}";

    ZipBuilder zip;
    auto add_named = [&](const std::string& entry_name, const std::vector<std::uint8_t>& raw) {
        if (options.omit_entry.has_value() && *options.omit_entry == entry_name) {
            return;
        }
        zip.add(entry_name, make_npy_u8(raw));
    };

    add_named("flop_assignments.npy", flop.assignments);
    add_named("turn_assignments.npy", turn.assignments);
    add_named("river_assignments.npy", river.assignments);
    add_named("flop_board_index.npy", make_json_bytes(flop.board_index_json));
    add_named("turn_board_index.npy", make_json_bytes(turn.board_index_json));
    add_named("river_board_index.npy", make_json_bytes(river.board_index_json));
    add_named("flop_hand_index.npy", make_json_bytes(flop.hand_index_json));
    add_named("turn_hand_index.npy", make_json_bytes(turn.hand_index_json));
    add_named("river_hand_index.npy", make_json_bytes(river.hand_index_json));
    add_named("metadata.npy", make_json_bytes(metadata));

    const auto bytes = zip.finish();
    std::ofstream out(tmp, std::ios::binary);
    out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    return tmp;
}

}  // namespace test_support
