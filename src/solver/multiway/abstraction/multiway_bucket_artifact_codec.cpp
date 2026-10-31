#include "solver/multiway/abstraction/multiway_bucket_artifact.hpp"
#include "multiway_bucket_artifact_detail.hpp"
#include "core/fingerprint.hpp"
#include "core/canonical_combo.hpp"

#include <algorithm>
#include <iterator>
#include <limits>
#include <stdexcept>
#include <utility>

namespace texas::solver::multiway
{
	using core::canonical_combos;
	using core::CanonicalComboId;
	using core::Street;

	std::vector<std::uint8_t> serialize_multiway_bucket_registry(const MultiwayBucketRegistry& registry)
	{
		registry.identity().validate();
		const auto& tables = registry.tables();
		if (tables.size() > std::numeric_limits<std::uint32_t>::max())
			throw std::overflow_error("multiway bucket artifact table count overflows format");
		std::uint32_t maximum_bucket_count = 0U;
		for (const auto& table : tables)
			maximum_bucket_count = std::max(maximum_bucket_count, table.bucket_count());
		const std::uint32_t width = bucket_artifact_detail::bucket_assignment_width(maximum_bucket_count);
		constexpr std::size_t kHeaderBytes = 4U + 4U + bucket_artifact_detail::kIdentityFieldCount * 8U + 4U + 4U + 6U * 8U;
		constexpr std::size_t kDirectoryEntryBytes = 32U;
		const auto directory_bytes = tables.size() * kDirectoryEntryBytes;
		if (tables.size() != 0U && directory_bytes / kDirectoryEntryBytes != tables.size())
			throw std::overflow_error("multiway bucket directory size overflows address space");
		const auto assignment_offset = kHeaderBytes + directory_bytes;
		if (tables.size() > (std::numeric_limits<std::size_t>::max() - assignment_offset) / (MULTIWAY_HOLE_COMBINATION_COUNT * width))
			throw std::overflow_error("multiway bucket payload size overflows address space");
		std::vector<std::uint8_t> directory;
		directory.reserve(directory_bytes);
		std::vector<std::uint8_t> assignments;
		assignments.reserve(tables.size() * MULTIWAY_HOLE_COMBINATION_COUNT * width);
		for (const auto& table : tables)
		{
			const auto board_size = table.canonical_board().size();
			if (board_size > 5U)
				throw std::invalid_argument("multiway bucket board is too large");
			directory.push_back(static_cast<std::uint8_t>(table.street()));
			directory.push_back(static_cast<std::uint8_t>(board_size));
			directory.push_back(0U);
			directory.push_back(0U);
			std::uint64_t board_key = 0U;
			for (std::size_t card = 0U; card < board_size; ++card)
				board_key |= static_cast<std::uint64_t>(table.canonical_board()[card]) << (card * 6U);
			bucket_artifact_detail::append_u64(directory, board_key);
			bucket_artifact_detail::append_u32(directory, table.bucket_count());
			bucket_artifact_detail::append_u64(directory, assignment_offset + assignments.size());
			bucket_artifact_detail::append_u64(directory, table.table_identity());
			for (std::size_t index = 0U; index < MULTIWAY_HOLE_COMBINATION_COUNT; ++index)
			{
				auto value = table.assignment_at(index);
				if (value == MULTIWAY_INVALID_BUCKET && width < 4U)
					value = (1U << (width * 8U)) - 1U;
				for (std::uint32_t byte = 0U; byte < width; ++byte)
					assignments.push_back(static_cast<std::uint8_t>(value >> (byte * 8U)));
			}
		}
		auto section_hash = [](const std::vector<std::uint8_t>& section) {
			auto hash = texas::core::fingerprint::FNV1A_OFFSET;
			for (const auto byte : section)
				texas::core::fingerprint::append_u8(hash, byte);
			return hash;
		};
		std::vector<std::uint8_t> output;
		output.reserve(assignment_offset + assignments.size());
		output.insert(output.end(), std::begin(bucket_artifact_detail::kMagic), std::end(bucket_artifact_detail::kMagic));
		bucket_artifact_detail::append_u32(output, MULTIWAY_BUCKET_ARTIFACT_SCHEMA_VERSION);
		bucket_artifact_detail::append_identity(output, registry.identity());
		bucket_artifact_detail::append_u32(output, static_cast<std::uint32_t>(tables.size()));
		bucket_artifact_detail::append_u32(output, width);
		bucket_artifact_detail::append_u64(output, kHeaderBytes);
		bucket_artifact_detail::append_u64(output, directory.size());
		bucket_artifact_detail::append_u64(output, assignment_offset);
		bucket_artifact_detail::append_u64(output, assignments.size());
		bucket_artifact_detail::append_u64(output, section_hash(directory));
		bucket_artifact_detail::append_u64(output, section_hash(assignments));
		output.insert(output.end(), directory.begin(), directory.end());
		output.insert(output.end(), assignments.begin(), assignments.end());
		return output;
	}

	MultiwayBucketRegistry deserialize_multiway_bucket_registry(const std::vector<std::uint8_t>& bytes)
	{
		constexpr std::size_t kMinimumSize = 4U + 4U + bucket_artifact_detail::kIdentityFieldCount * 8U + 4U;
		if (bytes.size() < kMinimumSize || !std::equal(std::begin(bucket_artifact_detail::kMagic), std::end(bucket_artifact_detail::kMagic), bytes.begin()))
		{
			throw std::invalid_argument("multiway bucket artifact has invalid header");
		}
		std::size_t cursor = 4U;
		const auto schema = bucket_artifact_detail::read_u32(bytes, cursor);
		if (schema == MULTIWAY_BUCKET_ARTIFACT_SCHEMA_VERSION)
		{
			const auto identity = bucket_artifact_detail::read_identity(bytes, cursor);
			const auto count = bucket_artifact_detail::read_u32(bytes, cursor);
			const auto width = bucket_artifact_detail::read_u32(bytes, cursor);
			const auto directory_offset = bucket_artifact_detail::read_u64(bytes, cursor);
			const auto directory_length = bucket_artifact_detail::read_u64(bytes, cursor);
			const auto assignment_offset = bucket_artifact_detail::read_u64(bytes, cursor);
			const auto assignment_length = bucket_artifact_detail::read_u64(bytes, cursor);
			const auto directory_hash = bucket_artifact_detail::read_u64(bytes, cursor);
			const auto assignment_hash = bucket_artifact_detail::read_u64(bytes, cursor);
			constexpr std::size_t header_size = 4U + 4U + bucket_artifact_detail::kIdentityFieldCount * 8U + 4U + 4U + 6U * 8U;
			constexpr std::size_t entry_size = 32U;
			if (count == 0U || (width != 1U && width != 2U && width != 4U) || directory_offset != header_size || directory_length != static_cast<std::uint64_t>(count) * entry_size || assignment_offset != directory_offset + directory_length || assignment_offset > bytes.size() || assignment_length != bytes.size() - static_cast<std::size_t>(assignment_offset) || assignment_length != static_cast<std::uint64_t>(count) * MULTIWAY_HOLE_COMBINATION_COUNT * width)
				throw std::invalid_argument("multiway bucket artifact has invalid section bounds");
			auto hash_section = [](const std::uint8_t* data, std::size_t size) {
				auto hash = texas::core::fingerprint::FNV1A_OFFSET;
				for (std::size_t index = 0U; index < size; ++index)
					texas::core::fingerprint::append_u8(hash, data[index]);
				return hash;
			};
			if (hash_section(bytes.data() + directory_offset, static_cast<std::size_t>(directory_length)) != directory_hash || hash_section(bytes.data() + assignment_offset, static_cast<std::size_t>(assignment_length)) != assignment_hash)
				throw std::invalid_argument("multiway bucket artifact section hash mismatch");
			std::vector<MultiwayBucketTable> tables;
			tables.reserve(count);
			std::size_t directory_cursor = static_cast<std::size_t>(directory_offset);
			for (std::uint32_t table_index = 0U; table_index < count; ++table_index)
			{
				const auto street = static_cast<Street>(bytes[directory_cursor++]);
				const auto board_size = bytes[directory_cursor++];
				const auto reserved_a = bytes[directory_cursor++];
				const auto reserved_b = bytes[directory_cursor++];
				const auto board_key = bucket_artifact_detail::read_u64(bytes, directory_cursor);
				const auto bucket_count = bucket_artifact_detail::read_u32(bytes, directory_cursor);
				const auto offset = bucket_artifact_detail::read_u64(bytes, directory_cursor);
				const auto expected_table_identity = bucket_artifact_detail::read_u64(bytes, directory_cursor);
				const auto expected_offset = assignment_offset + static_cast<std::uint64_t>(table_index) * MULTIWAY_HOLE_COMBINATION_COUNT * width;
				if (board_size != bucket_artifact_detail::board_size_for(street) || reserved_a != 0U || reserved_b != 0U || (board_key >> (board_size * 6U)) != 0U || bucket_count == 0U || bucket_artifact_detail::bucket_assignment_width(bucket_count) > width || offset != expected_offset || offset > assignment_offset + assignment_length - MULTIWAY_HOLE_COMBINATION_COUNT * width)
					throw std::invalid_argument("multiway bucket artifact has invalid table directory entry");
				std::vector<std::uint8_t> board(board_size);
				for (std::size_t card = 0U; card < board_size; ++card)
					board[card] = static_cast<std::uint8_t>((board_key >> (card * 6U)) & 0x3fU);
				if (!is_multiway_canonical_board(street, board))
					throw std::invalid_argument("multiway bucket artifact board is not canonical");
				std::vector<std::uint32_t> assignments;
				assignments.reserve(MULTIWAY_HOLE_COMBINATION_COUNT);
				std::size_t assignment_cursor = static_cast<std::size_t>(offset);
				for (std::size_t index = 0U; index < MULTIWAY_HOLE_COMBINATION_COUNT; ++index)
				{
					std::uint32_t value = 0U;
					for (std::uint32_t byte = 0U; byte < width; ++byte)
						value |= static_cast<std::uint32_t>(bytes[assignment_cursor++]) << (byte * 8U);
					if (width < 4U && value == ((1U << (width * 8U)) - 1U))
					{
						const auto& hole = canonical_combos().cards(static_cast<CanonicalComboId>(index));
						const bool blocked = std::find(board.begin(), board.end(), hole[0]) != board.end() || std::find(board.begin(), board.end(), hole[1]) != board.end();
						if (bucket_count != (1U << (width * 8U)) || blocked)
							value = MULTIWAY_INVALID_BUCKET;
					}
					assignments.push_back(value);
				}
				tables.emplace_back(identity, street, std::move(board), bucket_count, std::move(assignments));
				if (tables.back().table_identity() != expected_table_identity)
					throw std::invalid_argument("multiway bucket artifact table identity mismatch");
				if (table_index != 0U && !(tables[table_index - 1U].street() < street || (tables[table_index - 1U].street() == street && tables[table_index - 1U].canonical_board() < tables.back().canonical_board())))
					throw std::invalid_argument("multiway bucket artifact directory is not uniquely sorted");
			}
			std::uint32_t maximum_bucket_count = 0U;
			for (const auto& table : tables)
				maximum_bucket_count = std::max(maximum_bucket_count, table.bucket_count());
			if (bucket_artifact_detail::bucket_assignment_width(maximum_bucket_count) != width)
				throw std::invalid_argument("multiway bucket artifact assignment width is not minimal");
			return MultiwayBucketRegistry(std::move(tables));
		}
		if (schema != MULTIWAY_BUCKET_ARTIFACT_SCHEMA_V4)
			throw std::invalid_argument("multiway bucket artifact schema is unsupported");
		const auto identity = bucket_artifact_detail::read_identity(bytes, cursor);
		const auto table_count = bucket_artifact_detail::read_u32(bytes, cursor);
		if (table_count == 0U)
			throw std::invalid_argument("multiway bucket artifact has no tables");
		constexpr std::size_t kSmallestTableBytes = 2U + 3U + 4U + MULTIWAY_HOLE_COMBINATION_COUNT * 4U;
		if (static_cast<std::size_t>(table_count) > (bytes.size() - cursor) / kSmallestTableBytes)
		{
			throw std::invalid_argument("multiway bucket artifact table count is invalid");
		}
		std::vector<MultiwayBucketTable> tables;
		tables.reserve(table_count);
		for (std::uint32_t index = 0U; index < table_count; ++index)
		{
			if (bytes.size() - cursor < 2U)
				throw std::invalid_argument("multiway bucket artifact is truncated");
			const auto street = static_cast<Street>(bytes[cursor++]);
			const auto board_size = static_cast<std::size_t>(bytes[cursor++]);
			if (board_size != bucket_artifact_detail::board_size_for(street) || bytes.size() - cursor < board_size)
			{
				throw std::invalid_argument("multiway bucket artifact has invalid board metadata");
			}
			std::vector<std::uint8_t> board(bytes.begin() + static_cast<std::ptrdiff_t>(cursor),
				bytes.begin() + static_cast<std::ptrdiff_t>(cursor + board_size));
			cursor += board_size;
			const auto bucket_count = bucket_artifact_detail::read_u32(bytes, cursor);
			std::vector<std::uint32_t> assignments;
			assignments.reserve(MULTIWAY_HOLE_COMBINATION_COUNT);
			for (std::size_t assignment = 0U; assignment < MULTIWAY_HOLE_COMBINATION_COUNT; ++assignment)
			{
				assignments.push_back(bucket_artifact_detail::read_u32(bytes, cursor));
			}
			if (!is_multiway_canonical_board(street, board))
			{
				throw std::invalid_argument("multiway bucket artifact board is not canonical");
			}
			tables.emplace_back(identity, street, std::move(board), bucket_count, std::move(assignments));
		}
		if (cursor != bytes.size())
			throw std::invalid_argument("multiway bucket artifact has trailing data");
		return MultiwayBucketRegistry(std::move(tables));
	}
} // namespace texas::solver::multiway
