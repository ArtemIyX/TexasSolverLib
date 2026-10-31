#pragma once

/** @file
 * Helpers for building temporary suit-abstraction archive fixtures in tests.
 */

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

namespace test_support
{

	/** Converts a rank and suit pair to the repository card encoding. */
	inline std::uint8_t c(std::uint8_t rank, std::uint8_t suit)
	{
		return texas::card_to_int(rank, suit);
	}

	/** Adds one byte to an incremental CRC-32 value. */
	inline std::uint32_t crc32_byte(std::uint32_t crc, std::uint8_t b)
	{
		crc ^= b;
		for (int i = 0; i < 8; ++i)
		{
			crc = (crc & 1U) ? (0xEDB88320U ^ (crc >> 1U)) : (crc >> 1U);
		}
		return crc;
	}

	/** Computes the CRC-32 checksum of a byte vector. */
	inline std::uint32_t crc32(const std::vector<std::uint8_t>& data)
	{
		std::uint32_t crc = 0xFFFFFFFFU;
		for (const auto byte : data)
		{
			crc = crc32_byte(crc, byte);
		}
		return ~crc;
	}

	/** Appends a uint16 little-endian value to out. */
	inline void append_u16(std::vector<std::uint8_t>& out, std::uint16_t value)
	{
		out.push_back(static_cast<std::uint8_t>(value & 0xFFU));
		out.push_back(static_cast<std::uint8_t>((value >> 8U) & 0xFFU));
	}

	/** Appends a uint32 little-endian value to out. */
	inline void append_u32(std::vector<std::uint8_t>& out, std::uint32_t value)
	{
		out.push_back(static_cast<std::uint8_t>(value & 0xFFU));
		out.push_back(static_cast<std::uint8_t>((value >> 8U) & 0xFFU));
		out.push_back(static_cast<std::uint8_t>((value >> 16U) & 0xFFU));
		out.push_back(static_cast<std::uint8_t>((value >> 24U) & 0xFFU));
	}

	/** Creates an NPY v1 byte-array payload for fixture archives. */
	inline std::vector<std::uint8_t> make_npy_u8(const std::vector<std::uint8_t>& data)
	{
		std::vector<std::uint8_t> out;
		const std::string header_dict =
			"{'descr': '|u1', 'fortran_order': False, 'shape': (" + std::to_string(data.size()) + ",), }";
		std::string header = header_dict;
		while ((10 + header.size() + 1) % 16 != 0)
		{
			header.push_back(' ');
		}
		header.push_back('\n');

		out.insert(out.end(), { 0x93, 'N', 'U', 'M', 'P', 'Y', 1, 0 });
		append_u16(out, static_cast<std::uint16_t>(header.size()));
		out.insert(out.end(), header.begin(), header.end());
		out.insert(out.end(), data.begin(), data.end());
		return out;
	}

	/** Copies JSON text into archive payload bytes. */
	inline std::vector<std::uint8_t> make_json_bytes(const std::string& json)
	{
		return std::vector<std::uint8_t>(json.begin(), json.end());
	}

	/** Minimal stored-entry ZIP writer for deterministic test archives. */
#pragma pack(push, 8)
	struct ZipBuilder
	{
		struct Entry
		{
			std::string name;
			std::vector<std::uint8_t> data;
			std::uint32_t crc = 0;
			std::uint32_t local_offset = 0;
		};

		std::vector<Entry> entries;

		/** Adds one named file entry and computes its CRC. */
		void add(const std::string& name, const std::vector<std::uint8_t>& data)
		{
			entries.push_back(Entry{ name, data, crc32(data), 0 });
		}

		/** Encodes all local and central directory records into one byte vector. */
		std::vector<std::uint8_t> finish()
		{
			std::vector<std::uint8_t> out;
			std::vector<std::uint8_t> central;
			for (auto& entry : entries)
			{
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
#pragma pack(pop)

	/** Returns the platform temporary directory joined with name. */
	inline std::filesystem::path make_tmp_path(const std::string& name)
	{
		return std::filesystem::temp_directory_path() / name;
	}

	/** Enumerates sorted two-card hands that do not overlap the board. */
	inline std::vector<std::array<std::uint8_t, 2>> enumerate_live_hands(const std::vector<std::uint8_t>& board)
	{
		std::array<bool, 64> blocked = {};
		for (const auto card : board)
		{
			blocked[card] = true;
		}

		std::vector<std::uint8_t> live_cards;
		for (std::uint8_t rank = 2; rank <= 14; ++rank)
		{
			for (std::uint8_t suit = 0; suit < 4; ++suit)
			{
				const auto card = texas::card_to_int(rank, suit);
				if (!blocked[card])
				{
					live_cards.push_back(card);
				}
			}
		}

		std::vector<std::array<std::uint8_t, 2>> hands;
		for (std::size_t i = 0; i < live_cards.size(); ++i)
		{
			for (std::size_t j = i + 1; j < live_cards.size(); ++j)
			{
				std::array<std::uint8_t, 2> hole = { live_cards[i], live_cards[j] };
				if (hole[1] < hole[0])
				{
					std::swap(hole[0], hole[1]);
				}
				hands.push_back(hole);
			}
		}
		return hands;
	}

	/** Metadata and omission settings for a generated abstraction archive. */
#pragma pack(push, 8)
	struct AbstractionFixtureOptions
	{
		std::vector<std::uint16_t> bucket_counts = { 1, 1, 1 };
		std::uint8_t schema_version = texas::ABSTRACTION_SCHEMA_VERSION;
		std::string version = "v1";
		std::optional<std::string> omit_entry = std::nullopt;
	};
#pragma pack(pop)

	/** Assignment and canonical indexes for one street's test archive data. */
#pragma pack(push, 8)
	struct StreetFixtureData
	{
		std::vector<std::uint8_t> assignments;
		std::string board_index_json = "{}";
		std::string hand_index_json = "{}";
	};
#pragma pack(pop)

	/** Builds assignment and index data for one optional board fixture. */
	inline StreetFixtureData make_street_fixture_data(
		texas::Street street,
		const std::optional<std::vector<std::uint8_t>>& board,
		const std::function<std::uint8_t(texas::Street, std::size_t, const std::array<std::uint8_t, 2>&)>& bucket_for)
	{
		StreetFixtureData data;
		if (!board.has_value())
		{
			return data;
		}

		const auto board_key = texas::canonicalize_board(*board);
		const auto live_hands = enumerate_live_hands(*board);
		data.board_index_json = "{\"" + board_key + "\":0}";
		data.hand_index_json = "{\"" + board_key + "\":{";
		data.assignments.reserve(live_hands.size());

		for (std::size_t i = 0; i < live_hands.size(); ++i)
		{
			const auto [canonical_board_key, hand_key] = texas::canonicalize(*board, live_hands[i]);
			(void)canonical_board_key;
			if (i > 0)
			{
				data.hand_index_json += ",";
			}
			data.hand_index_json += "\"" + hand_key + "\":" + std::to_string(i);
			data.assignments.push_back(bucket_for(street, i, live_hands[i]));
		}
		data.hand_index_json += "}}";
		return data;
	}

	/** Writes and returns a temporary multi-street abstraction fixture archive. */
	inline std::filesystem::path write_abstraction_fixture(
		const std::string& name,
		const std::optional<std::vector<std::uint8_t>>& flop_board,
		const std::optional<std::vector<std::uint8_t>>& turn_board,
		const std::optional<std::vector<std::uint8_t>>& river_board,
		const std::function<std::uint8_t(texas::Street, std::size_t, const std::array<std::uint8_t, 2>&)>& bucket_for,
		const AbstractionFixtureOptions& options = {})
	{
		const auto tmp = make_tmp_path(name);
		const auto flop = make_street_fixture_data(texas::Street::Flop, flop_board, bucket_for);
		const auto turn = make_street_fixture_data(texas::Street::Turn, turn_board, bucket_for);
		const auto river = make_street_fixture_data(texas::Street::River, river_board, bucket_for);

		std::string metadata = "{\"bucket_counts\":[";
		for (std::size_t i = 0; i < options.bucket_counts.size(); ++i)
		{
			if (i > 0)
			{
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
			if (options.omit_entry.has_value() && *options.omit_entry == entry_name)
			{
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

} // namespace test_support
