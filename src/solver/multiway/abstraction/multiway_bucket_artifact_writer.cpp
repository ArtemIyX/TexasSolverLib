#include "solver/multiway/abstraction/multiway_bucket_artifact_writer.hpp"
#include "solver/multiway/abstraction/multiway_bucket_catalog.hpp"

#include "core/atomic_publish.hpp"
#include "core/fingerprint.hpp"

#include <array>
#include <vector>
#include <stdexcept>
#include <filesystem>

namespace texas::solver::multiway
{
	namespace
	{
		void write_u32(std::ofstream& output, std::uint32_t value)
		{
			std::array<char, 4> bytes{};
			for (std::size_t i = 0; i < bytes.size(); ++i)
				bytes[i] = static_cast<char>(value >> (i * 8U));
			output.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
		}
		void write_u64(std::ofstream& output, std::uint64_t value)
		{
			for (std::size_t i = 0; i < 8U; ++i)
				output.put(static_cast<char>(value >> (i * 8U)));
		}
		void hash_byte(std::uint64_t& hash, std::uint8_t value) noexcept
		{
			core::fingerprint::append_u8(hash, value);
		}
		void append_u32(std::vector<char>& bytes, std::uint32_t value)
		{
			for (std::size_t i = 0; i < 4U; ++i)
				bytes.push_back(static_cast<char>(value >> (i * 8U)));
		}
		void append_u64(std::vector<char>& bytes, std::uint64_t value)
		{
			for (std::size_t i = 0U; i < 8U; ++i)
				bytes.push_back(static_cast<char>(value >> (i * 8U)));
		}
		std::uint32_t read_u32(std::istream& input)
		{
			std::array<unsigned char, 4U> bytes{};
			input.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
			if (!input)
				throw std::invalid_argument("bucket staging stream is truncated");
			return static_cast<std::uint32_t>(bytes[0]) | (static_cast<std::uint32_t>(bytes[1]) << 8U) | (static_cast<std::uint32_t>(bytes[2]) << 16U) | (static_cast<std::uint32_t>(bytes[3]) << 24U);
		}
		std::uint32_t assignment_width(std::uint32_t bucket_count) noexcept
		{
			return bucket_count <= 0x100U ? 1U : bucket_count <= 0x10000U ? 2U
																		  : 4U;
		}
		void hash_u64(std::uint64_t& hash, std::uint64_t value) noexcept
		{
			for (std::size_t i = 0U; i < 8U; ++i)
				hash_byte(hash, static_cast<std::uint8_t>(value >> (i * 8U)));
		}
		void convert_v4_staging_to_v5(const std::filesystem::path& source,
			const std::filesystem::path& packed_path, const MultiwayModelIdentity& identity,
			std::uint64_t expected_count)
		{
			constexpr std::uint64_t header_v4 = 116U;
			constexpr std::uint64_t header_v5 = 168U;
			constexpr std::uint64_t directory_entry_size = 32U;
			const auto maximum_catalog_tables = multiway_bucket_board_count(core::Street::Flop) + multiway_bucket_board_count(core::Street::Turn) + multiway_bucket_board_count(core::Street::River);
			if (expected_count > maximum_catalog_tables || expected_count > UINT32_MAX || expected_count > (std::numeric_limits<std::uint64_t>::max() - header_v5) / directory_entry_size)
				throw std::overflow_error("bucket schema-5 directory size overflows");
			std::ifstream scan(source, std::ios::binary);
			if (!scan)
				throw std::runtime_error("cannot reopen bucket staging stream");
			scan.seekg(static_cast<std::streamoff>(header_v4), std::ios::beg);
			std::uint32_t max_bucket_count = 0U;
			for (std::uint64_t index = 0U; index < expected_count; ++index)
			{
				const auto street = scan.get();
				const auto board_size = scan.get();
				if (!scan || board_size < 3 || board_size > 5)
					throw std::invalid_argument("invalid bucket staging board metadata");
				std::vector<std::uint8_t> board(static_cast<std::size_t>(board_size));
				scan.read(reinterpret_cast<char*>(board.data()), board_size);
				if (!scan || !is_multiway_canonical_board(static_cast<core::Street>(street), board))
					throw std::invalid_argument("invalid bucket staging board");
				const auto bucket_count = read_u32(scan);
				if (bucket_count == 0U)
					throw std::invalid_argument("bucket staging table has zero buckets");
				max_bucket_count = std::max(max_bucket_count, bucket_count);
				scan.seekg(static_cast<std::streamoff>(MULTIWAY_HOLE_COMBINATION_COUNT * sizeof(std::uint32_t)), std::ios::cur);
				if (!scan)
					throw std::invalid_argument("bucket staging assignments are truncated");
			}
			if (scan.peek() != std::char_traits<char>::eof())
				throw std::invalid_argument("bucket staging stream has trailing data");
			const auto width = assignment_width(max_bucket_count);
			const auto directory_length = expected_count * directory_entry_size;
			const auto assignment_length = expected_count * MULTIWAY_HOLE_COMBINATION_COUNT * width;
			const auto assignment_offset = header_v5 + directory_length;
			const auto assignment_path = std::filesystem::path(packed_path.string() + ".assignments");
			std::error_code path_error;
			if (std::filesystem::exists(packed_path, path_error) || path_error || std::filesystem::exists(assignment_path, path_error) || path_error)
				throw std::runtime_error("compact bucket staging path already exists");
			std::vector<char> directory;
			directory.reserve(static_cast<std::size_t>(directory_length));
			std::ofstream assignment_output(assignment_path, std::ios::binary | std::ios::trunc);
			if (!assignment_output)
				throw std::runtime_error("cannot create compact assignment staging file");
			auto assignment_hash = core::fingerprint::FNV1A_OFFSET;
			std::ifstream input(source, std::ios::binary);
			input.seekg(static_cast<std::streamoff>(header_v4), std::ios::beg);
			core::Street prior_street = core::Street::Preflop;
			std::vector<std::uint8_t> prior_board;
			for (std::uint64_t index = 0U; index < expected_count; ++index)
			{
				const auto street = static_cast<core::Street>(input.get());
				const auto board_size = input.get();
				std::vector<std::uint8_t> board(static_cast<std::size_t>(board_size));
				input.read(reinterpret_cast<char*>(board.data()), board_size);
				const auto bucket_count = read_u32(input);
				if (!input || !is_multiway_canonical_board(street, board) || bucket_count == 0U || (index != 0U && !(prior_street < street || (prior_street == street && prior_board < board))))
					throw std::invalid_argument("bucket staging directory is not canonical");
				prior_street = street;
				prior_board = board;
				directory.push_back(static_cast<char>(street));
				directory.push_back(static_cast<char>(board_size));
				directory.push_back(0);
				directory.push_back(0);
				std::uint64_t board_key = 0U;
				for (std::size_t card = 0U; card < board.size(); ++card)
					board_key |= static_cast<std::uint64_t>(board[card]) << (card * 6U);
				append_u64(directory, board_key);
				append_u32(directory, bucket_count);
				append_u64(directory, assignment_offset + index * MULTIWAY_HOLE_COMBINATION_COUNT * width);
				auto table_hash = core::fingerprint::FNV1A_OFFSET;
				hash_u64(table_hash, identity.combined_hash);
				hash_u64(table_hash, static_cast<std::uint8_t>(street));
				hash_u64(table_hash, board.size());
				for (const auto card : board)
					hash_u64(table_hash, card);
				hash_u64(table_hash, bucket_count);
				auto assignment_bytes = std::array<char, 4U>{};
				for (std::size_t assignment = 0U; assignment < MULTIWAY_HOLE_COMBINATION_COUNT; ++assignment)
				{
					input.read(assignment_bytes.data(), 4);
					if (!input)
						throw std::invalid_argument("bucket staging assignments are truncated");
					const auto value = static_cast<std::uint32_t>(static_cast<unsigned char>(assignment_bytes[0])) | (static_cast<std::uint32_t>(static_cast<unsigned char>(assignment_bytes[1])) << 8U) | (static_cast<std::uint32_t>(static_cast<unsigned char>(assignment_bytes[2])) << 16U) | (static_cast<std::uint32_t>(static_cast<unsigned char>(assignment_bytes[3])) << 24U);
					if (value != MULTIWAY_INVALID_BUCKET && value >= bucket_count)
						throw std::invalid_argument("bucket staging assignment is out of range");
					hash_u64(table_hash, value);
					auto compact_value = value == MULTIWAY_INVALID_BUCKET && width < 4U ? (1U << (width * 8U)) - 1U : value;
					for (std::uint32_t byte = 0U; byte < width; ++byte)
					{
						const auto output_byte = static_cast<char>(compact_value >> (byte * 8U));
						assignment_output.put(output_byte);
						hash_byte(assignment_hash, static_cast<std::uint8_t>(static_cast<unsigned char>(output_byte)));
					}
				}
				append_u64(directory, table_hash);
			}
			assignment_output.close();
			if (!assignment_output)
				throw std::runtime_error("cannot flush compact assignment staging file");
			auto directory_hash = core::fingerprint::FNV1A_OFFSET;
			for (const auto byte : directory)
				hash_byte(directory_hash, static_cast<std::uint8_t>(static_cast<unsigned char>(byte)));
			std::ofstream output(packed_path, std::ios::binary | std::ios::trunc);
			if (!output)
				throw std::runtime_error("cannot create compact bucket artifact staging file");
			output.write("MWBK", 4);
			write_u32(output, MULTIWAY_BUCKET_ARTIFACT_SCHEMA_VERSION);
			visit_multiway_model_identity_fields(identity, [&](std::uint64_t field) { write_u64(output, field); });
			write_u32(output, static_cast<std::uint32_t>(expected_count));
			write_u32(output, width);
			write_u64(output, header_v5);
			write_u64(output, directory_length);
			write_u64(output, assignment_offset);
			write_u64(output, assignment_length);
			write_u64(output, directory_hash);
			write_u64(output, assignment_hash);
			output.write(directory.data(), static_cast<std::streamsize>(directory.size()));
			std::ifstream compact_assignments(assignment_path, std::ios::binary);
			std::array<char, 64U * 1024U> buffer{};
			while (compact_assignments)
			{
				compact_assignments.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
				const auto received = compact_assignments.gcount();
				if (received != 0)
					output.write(buffer.data(), received);
			}
			compact_assignments.close();
			output.close();
			if (!output || !compact_assignments.eof())
				throw std::runtime_error("cannot finish compact bucket artifact staging file");
			std::error_code error;
			std::filesystem::remove(assignment_path, error);
		}
		void append_table_bytes(const MultiwayBucketTable& table, std::vector<char>& bytes)
		{
			if (table.assignment_count() != MULTIWAY_HOLE_COMBINATION_COUNT || table.canonical_board().size() > 5U || !is_multiway_canonical_board(table.street(), table.canonical_board()))
				throw std::invalid_argument("invalid bucket table");
			bytes.push_back(static_cast<char>(table.street()));
			bytes.push_back(static_cast<char>(table.canonical_board().size()));
			for (const auto card : table.canonical_board())
				bytes.push_back(static_cast<char>(card));
			append_u32(bytes, table.bucket_count());
			for (std::size_t index = 0U; index < MULTIWAY_HOLE_COMBINATION_COUNT; ++index)
				append_u32(bytes, table.assignment_at(index));
		}
	} // namespace

	MultiwayBucketArtifactWriter::MultiwayBucketArtifactWriter(
		std::filesystem::path temporary_path, MultiwayModelIdentity identity, std::uint64_t expected_table_count)
		: temporary_path_(std::move(temporary_path)), identity_(identity), expected_table_count_(expected_table_count)
	{
		identity.validate();
		if (expected_table_count_ == 0U || expected_table_count_ > UINT32_MAX)
			throw std::invalid_argument("invalid bucket table count");
		output_.open(temporary_path_, std::ios::binary | std::ios::trunc);
		if (!output_)
			throw std::runtime_error("cannot open bucket artifact temporary file");
		output_.write("MWBK", 4);
		// Checkpointable staging records remain schema 4; finish publishes schema 5.
		write_u32(output_, MULTIWAY_BUCKET_ARTIFACT_SCHEMA_V4);
		visit_multiway_model_identity_fields(identity, [&](std::uint64_t field) { write_u64(output_, field); });
		write_u32(output_, static_cast<std::uint32_t>(expected_table_count_));
		progress_.payload_hash = core::fingerprint::FNV1A_OFFSET;
		progress_.byte_length = 4U + 4U + 13U * 8U + 4U;
	}

	MultiwayBucketArtifactWriter MultiwayBucketArtifactWriter::resume(
		std::filesystem::path temporary_path, MultiwayModelIdentity identity,
		std::uint64_t expected_table_count, MultiwayBucketArtifactProgress progress)
	{
		identity.validate();
		if (expected_table_count == 0U || progress.table_count > expected_table_count || progress.next_board_index != progress.table_count || progress.byte_length < 116U)
		{
			throw std::invalid_argument("invalid bucket resume progress");
		}
		std::error_code error;
		if (!std::filesystem::exists(temporary_path, error) || error || std::filesystem::file_size(temporary_path, error) < progress.byte_length || error)
		{
			throw std::invalid_argument("bucket resume file does not match progress");
		}
		std::ifstream existing(temporary_path, std::ios::binary);
		existing.seekg(116, std::ios::beg);
		auto hash = core::fingerprint::FNV1A_OFFSET;
		std::array<char, 64U * 1024U> buffer{};
		std::uint64_t remaining = progress.byte_length - 116U;
		while (remaining != 0U)
		{
			const auto request = static_cast<std::streamsize>(std::min<std::uint64_t>(remaining, buffer.size()));
			existing.read(buffer.data(), request);
			const auto received = existing.gcount();
			if (received != request)
				throw std::invalid_argument("bucket resume file is truncated");
			for (std::streamsize index = 0; index < received; ++index)
			{
				core::fingerprint::append_u8(hash, static_cast<std::uint8_t>(static_cast<unsigned char>(buffer[static_cast<std::size_t>(index)])));
			}
			remaining -= static_cast<std::uint64_t>(received);
		}
		if (hash != progress.payload_hash)
			throw std::invalid_argument("bucket resume hash mismatch");
		const auto current_length = std::filesystem::file_size(temporary_path, error);
		if (error)
			throw std::invalid_argument("bucket resume file size cannot be determined");
		if (current_length > progress.byte_length)
		{
			std::filesystem::resize_file(temporary_path, progress.byte_length, error);
			if (error)
				throw std::runtime_error("bucket resume cannot discard uncheckpointed tail");
		}
		MultiwayBucketArtifactWriter writer;
		writer.temporary_path_ = std::move(temporary_path);
		writer.identity_ = identity;
		writer.expected_table_count_ = expected_table_count;
		writer.progress_ = progress;
		writer.output_.open(writer.temporary_path_, std::ios::binary | std::ios::app);
		if (!writer.output_)
			throw std::runtime_error("cannot open bucket resume file");
		return writer;
	}

	MultiwayBucketArtifactWriter::~MultiwayBucketArtifactWriter()
	{
		output_.close();
	}

	void MultiwayBucketArtifactWriter::append(const MultiwayBucketTable& table)
	{
		append_chunk(std::vector<MultiwayBucketTable>{ table });
	}

	void MultiwayBucketArtifactWriter::append_chunk(const std::vector<MultiwayBucketTable>& tables)
	{
		if (finished_ || progress_.table_count >= expected_table_count_ || tables.size() > expected_table_count_ - progress_.table_count)
			throw std::logic_error("bucket artifact is already complete");
		if (tables.empty())
			return;
		std::vector<char> bytes;
		bytes.reserve(tables.size() * (MULTIWAY_HOLE_COMBINATION_COUNT * sizeof(std::uint32_t) + 16U));
		for (const auto& table : tables)
			append_table_bytes(table, bytes);
		output_.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
		if (!output_)
			throw std::runtime_error("cannot write bucket artifact");
		for (const auto byte : bytes)
			core::fingerprint::append_u8(progress_.payload_hash, static_cast<std::uint8_t>(static_cast<unsigned char>(byte)));
		progress_.table_count += tables.size();
		progress_.next_board_index = progress_.table_count;
		progress_.byte_length += bytes.size();
	}

	void MultiwayBucketArtifactWriter::append_serialized_chunk(
		std::uint64_t table_count, const std::vector<std::uint8_t>& payload)
	{
		if (finished_ || progress_.table_count >= expected_table_count_ || table_count == 0U || table_count > expected_table_count_ - progress_.table_count)
		{
			throw std::logic_error("bucket artifact chunk is invalid");
		}
		output_.write(reinterpret_cast<const char*>(payload.data()), static_cast<std::streamsize>(payload.size()));
		if (!output_)
			throw std::runtime_error("cannot write bucket artifact");
		for (const auto byte : payload)
			core::fingerprint::append_u8(progress_.payload_hash, byte);
		progress_.table_count += table_count;
		progress_.next_board_index = progress_.table_count;
		progress_.byte_length += payload.size();
	}

	void MultiwayBucketArtifactWriter::flush_checkpoint()
	{
		if (finished_)
			throw std::logic_error("bucket artifact is already complete");
		output_.flush();
		if (!output_)
			throw std::runtime_error("cannot flush bucket artifact checkpoint");
	}

	void MultiwayBucketArtifactWriter::finish(const std::filesystem::path& destination)
	{
		if (progress_.table_count != expected_table_count_)
			throw std::logic_error("bucket artifact has incomplete table count");
		output_.flush();
		output_.close();
		if (!output_)
			throw std::runtime_error("cannot flush bucket artifact");
		auto packed_path = std::filesystem::path(temporary_path_.string() + ".schema5");
		convert_v4_staging_to_v5(temporary_path_, packed_path, identity_, expected_table_count_);
		core::publish_atomic_replace(packed_path, destination, "cannot publish bucket artifact");
		std::error_code cleanup_error;
		std::filesystem::remove(temporary_path_, cleanup_error);
		finished_ = true;
	}

	void save_multiway_bucket_progress_atomic(const std::filesystem::path& path,
		const MultiwayModelIdentity& identity, std::uint64_t expected_table_count,
		const MultiwayBucketArtifactProgress& progress)
	{
		identity.validate();
		if (progress.table_count > expected_table_count || progress.next_board_index != progress.table_count)
			throw std::invalid_argument("invalid bucket progress");
		const auto temporary = path.string() + ".tmp";
		std::ofstream output(temporary, std::ios::trunc);
		if (!output)
			throw std::runtime_error("cannot open bucket progress sidecar");
		output << "MWBK-PROGRESS 1\nidentity= " << identity.combined_hash << "\nexpected= " << expected_table_count
			   << "\nnext= " << progress.next_board_index << "\ntables= " << progress.table_count
			   << "\nhash= " << progress.payload_hash << "\nbytes= " << progress.byte_length << "\n";
		output.close();
		core::publish_atomic_replace(temporary, path, "cannot publish bucket progress sidecar");
	}

	MultiwayBucketArtifactProgress load_multiway_bucket_progress(
		const std::filesystem::path& path, const MultiwayModelIdentity& expected_identity,
		std::uint64_t expected_table_count)
	{
		std::ifstream input(path);
		if (!input)
			throw std::runtime_error("cannot open bucket progress sidecar");
		std::string magic, key;
		std::uint64_t identity_hash = 0U, expected = 0U;
		MultiwayBucketArtifactProgress progress;
		if (!(input >> magic) || magic != "MWBK-PROGRESS")
			throw std::invalid_argument("invalid bucket progress sidecar");
		unsigned version = 0U;
		if (!(input >> version) || version != 1U)
			throw std::invalid_argument("unsupported bucket progress sidecar");
		auto read = [&](std::uint64_t& value, const char* expected_key) {
			if (!(input >> key >> value) || key != expected_key)
				throw std::invalid_argument("invalid bucket progress sidecar");
		};
		read(identity_hash, "identity=");
		read(expected, "expected=");
		read(progress.next_board_index, "next=");
		read(progress.table_count, "tables=");
		read(progress.payload_hash, "hash=");
		read(progress.byte_length, "bytes=");
		expected_identity.validate();
		if (identity_hash != expected_identity.combined_hash || expected != expected_table_count || progress.next_board_index != progress.table_count || progress.table_count > expected)
			throw std::invalid_argument("bucket progress identity mismatch");
		return progress;
	}
} // namespace texas::solver::multiway
