#include "solver/multiway/abstraction/multiway_bucket_artifact_writer.hpp"

#include "core/atomic_publish.hpp"
#include "core/fingerprint.hpp"

#include <array>
#include <vector>
#include <stdexcept>
#include <filesystem>

namespace texas::solver::multiway {
namespace {
void write_u32(std::ofstream& output, std::uint32_t value) {
    std::array<char, 4> bytes{};
    for (std::size_t i = 0; i < bytes.size(); ++i) bytes[i] = static_cast<char>(value >> (i * 8U));
    output.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
}
void write_u64(std::ofstream& output, std::uint64_t value) {
    for (std::size_t i = 0; i < 8U; ++i) output.put(static_cast<char>(value >> (i * 8U)));
}
void hash_byte(std::uint64_t& hash, std::uint8_t value) noexcept { core::fingerprint::append_u8(hash, value); }
void hash_u32(std::uint64_t& hash, std::uint32_t value) noexcept {
    for (std::size_t i = 0; i < 4U; ++i) hash_byte(hash, static_cast<std::uint8_t>(value >> (i * 8U)));
}
void append_u32(std::vector<char>& bytes, std::uint32_t value) {
    for (std::size_t i = 0; i < 4U; ++i) bytes.push_back(static_cast<char>(value >> (i * 8U)));
}
void append_table_bytes(const MultiwayBucketTable& table, std::vector<char>& bytes) {
    if (table.assignments().size() != MULTIWAY_HOLE_COMBINATION_COUNT || table.canonical_board().size() > 5U ||
        !is_multiway_canonical_board(table.street(), table.canonical_board())) throw std::invalid_argument("invalid bucket table");
    bytes.push_back(static_cast<char>(table.street()));
    bytes.push_back(static_cast<char>(table.canonical_board().size()));
    for (const auto card : table.canonical_board()) bytes.push_back(static_cast<char>(card));
    append_u32(bytes, table.bucket_count());
    for (const auto assignment : table.assignments()) append_u32(bytes, assignment);
}
}  // namespace

MultiwayBucketArtifactWriter::MultiwayBucketArtifactWriter(
    std::filesystem::path temporary_path, MultiwayModelIdentity identity, std::uint64_t expected_table_count)
    : temporary_path_(std::move(temporary_path)), expected_table_count_(expected_table_count) {
    identity.validate();
    if (expected_table_count_ == 0U || expected_table_count_ > UINT32_MAX) throw std::invalid_argument("invalid bucket table count");
    output_.open(temporary_path_, std::ios::binary | std::ios::trunc);
    if (!output_) throw std::runtime_error("cannot open bucket artifact temporary file");
    output_.write("MWBK", 4);
    write_u32(output_, MULTIWAY_BUCKET_ARTIFACT_SCHEMA_VERSION);
    visit_multiway_model_identity_fields(identity, [&](std::uint64_t field) { write_u64(output_, field); });
    write_u32(output_, static_cast<std::uint32_t>(expected_table_count_));
    progress_.payload_hash = core::fingerprint::FNV1A_OFFSET;
    progress_.byte_length = 4U + 4U + 13U * 8U + 4U;
}

MultiwayBucketArtifactWriter MultiwayBucketArtifactWriter::resume(
    std::filesystem::path temporary_path, MultiwayModelIdentity identity,
    std::uint64_t expected_table_count, MultiwayBucketArtifactProgress progress) {
    identity.validate();
    if (expected_table_count == 0U || progress.table_count > expected_table_count ||
        progress.next_board_index != progress.table_count || progress.byte_length < 116U) {
        throw std::invalid_argument("invalid bucket resume progress");
    }
    std::error_code error;
    if (!std::filesystem::exists(temporary_path, error) || error ||
        std::filesystem::file_size(temporary_path, error) < progress.byte_length || error) {
        throw std::invalid_argument("bucket resume file does not match progress");
    }
    std::ifstream existing(temporary_path, std::ios::binary);
    existing.seekg(116, std::ios::beg);
    auto hash = core::fingerprint::FNV1A_OFFSET;
    std::array<char, 64U * 1024U> buffer{};
    std::uint64_t remaining = progress.byte_length - 116U;
    while (remaining != 0U) {
        const auto request = static_cast<std::streamsize>(std::min<std::uint64_t>(remaining, buffer.size()));
        existing.read(buffer.data(), request);
        const auto received = existing.gcount();
        if (received != request) throw std::invalid_argument("bucket resume file is truncated");
        for (std::streamsize index = 0; index < received; ++index) {
            core::fingerprint::append_u8(hash, static_cast<std::uint8_t>(static_cast<unsigned char>(buffer[static_cast<std::size_t>(index)])));
        }
        remaining -= static_cast<std::uint64_t>(received);
    }
    if (hash != progress.payload_hash) throw std::invalid_argument("bucket resume hash mismatch");
    const auto current_length = std::filesystem::file_size(temporary_path, error);
    if (error) throw std::invalid_argument("bucket resume file size cannot be determined");
    if (current_length > progress.byte_length) {
        std::filesystem::resize_file(temporary_path, progress.byte_length, error);
        if (error) throw std::runtime_error("bucket resume cannot discard uncheckpointed tail");
    }
    MultiwayBucketArtifactWriter writer;
    writer.temporary_path_ = std::move(temporary_path);
    writer.expected_table_count_ = expected_table_count;
    writer.progress_ = progress;
    writer.output_.open(writer.temporary_path_, std::ios::binary | std::ios::app);
    if (!writer.output_) throw std::runtime_error("cannot open bucket resume file");
    return writer;
}

MultiwayBucketArtifactWriter::~MultiwayBucketArtifactWriter() { output_.close(); }

void MultiwayBucketArtifactWriter::append(const MultiwayBucketTable& table) {
    append_chunk(std::vector<MultiwayBucketTable>{table});
}

void MultiwayBucketArtifactWriter::append_chunk(const std::vector<MultiwayBucketTable>& tables) {
    if (finished_ || progress_.table_count >= expected_table_count_ || tables.size() > expected_table_count_ - progress_.table_count) throw std::logic_error("bucket artifact is already complete");
    if (tables.empty()) return;
    std::vector<char> bytes;
    bytes.reserve(tables.size() * (MULTIWAY_HOLE_COMBINATION_COUNT * sizeof(std::uint32_t) + 16U));
    for (const auto& table : tables) append_table_bytes(table, bytes);
    output_.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    if (!output_) throw std::runtime_error("cannot write bucket artifact");
    for (const auto byte : bytes) core::fingerprint::append_u8(progress_.payload_hash, static_cast<std::uint8_t>(static_cast<unsigned char>(byte)));
    progress_.table_count += tables.size();
    progress_.next_board_index = progress_.table_count;
    progress_.byte_length += bytes.size();
}

void MultiwayBucketArtifactWriter::append_serialized_chunk(
    std::uint64_t table_count, std::vector<std::uint8_t>&& payload) {
    if (finished_ || progress_.table_count >= expected_table_count_ || table_count == 0U ||
        table_count > expected_table_count_ - progress_.table_count) {
        throw std::logic_error("bucket artifact chunk is invalid");
    }
    output_.write(reinterpret_cast<const char*>(payload.data()), static_cast<std::streamsize>(payload.size()));
    if (!output_) throw std::runtime_error("cannot write bucket artifact");
    for (const auto byte : payload) core::fingerprint::append_u8(progress_.payload_hash, byte);
    progress_.table_count += table_count;
    progress_.next_board_index = progress_.table_count;
    progress_.byte_length += payload.size();
}

void MultiwayBucketArtifactWriter::flush_checkpoint() {
    if (finished_) throw std::logic_error("bucket artifact is already complete");
    output_.flush();
    if (!output_) throw std::runtime_error("cannot flush bucket artifact checkpoint");
}

void MultiwayBucketArtifactWriter::finish(const std::filesystem::path& destination) {
    if (progress_.table_count != expected_table_count_) throw std::logic_error("bucket artifact has incomplete table count");
    output_.flush();
    output_.close();
    if (!output_) throw std::runtime_error("cannot flush bucket artifact");
    core::publish_atomic_replace(temporary_path_, destination, "cannot publish bucket artifact");
    finished_ = true;
}

void save_multiway_bucket_progress_atomic(const std::filesystem::path& path,
    const MultiwayModelIdentity& identity, std::uint64_t expected_table_count,
    const MultiwayBucketArtifactProgress& progress) {
    identity.validate();
    if (progress.table_count > expected_table_count || progress.next_board_index != progress.table_count) throw std::invalid_argument("invalid bucket progress");
    const auto temporary = path.string() + ".tmp";
    std::ofstream output(temporary, std::ios::trunc);
    if (!output) throw std::runtime_error("cannot open bucket progress sidecar");
    output << "MWBK-PROGRESS 1\nidentity= " << identity.combined_hash << "\nexpected= " << expected_table_count
           << "\nnext= " << progress.next_board_index << "\ntables= " << progress.table_count
           << "\nhash= " << progress.payload_hash << "\nbytes= " << progress.byte_length << "\n";
    output.close();
    core::publish_atomic_replace(temporary, path, "cannot publish bucket progress sidecar");
}

MultiwayBucketArtifactProgress load_multiway_bucket_progress(
    const std::filesystem::path& path, const MultiwayModelIdentity& expected_identity,
    std::uint64_t expected_table_count) {
    std::ifstream input(path);
    if (!input) throw std::runtime_error("cannot open bucket progress sidecar");
    std::string magic, key;
    std::uint64_t identity_hash = 0U, expected = 0U;
    MultiwayBucketArtifactProgress progress;
    if (!(input >> magic) || magic != "MWBK-PROGRESS") throw std::invalid_argument("invalid bucket progress sidecar");
    unsigned version = 0U;
    if (!(input >> version) || version != 1U) throw std::invalid_argument("unsupported bucket progress sidecar");
    auto read = [&](std::uint64_t& value, const char* expected_key) {
        if (!(input >> key >> value) || key != expected_key) throw std::invalid_argument("invalid bucket progress sidecar");
    };
    read(identity_hash, "identity="); read(expected, "expected="); read(progress.next_board_index, "next=");
    read(progress.table_count, "tables="); read(progress.payload_hash, "hash="); read(progress.byte_length, "bytes=");
    expected_identity.validate();
    if (identity_hash != expected_identity.combined_hash || expected != expected_table_count ||
        progress.next_board_index != progress.table_count || progress.table_count > expected) throw std::invalid_argument("bucket progress identity mismatch");
    return progress;
}
}  // namespace texas::solver::multiway
