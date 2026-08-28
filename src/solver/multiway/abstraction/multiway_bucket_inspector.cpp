#include "solver/multiway/abstraction/multiway_bucket_artifact.hpp"
#include "core/fingerprint.hpp"

#include <fstream>
#include <stdexcept>
#include <array>

namespace texas::solver::multiway {
namespace {
void hash_u32(std::uint64_t& hash, std::uint32_t value) noexcept {
    for (std::size_t index = 0U; index < 4U; ++index)
        core::fingerprint::append_u8(hash, static_cast<std::uint8_t>(value >> (index * 8U)));
}
std::uint32_t read_u32(std::ifstream& input) {
    std::uint32_t value = 0U;
    for (std::size_t i = 0U; i < 4U; ++i) { const auto byte = input.get(); if (byte == EOF) throw std::runtime_error("bucket artifact is truncated"); value |= static_cast<std::uint32_t>(static_cast<unsigned char>(byte)) << (i * 8U); }
    return value;
}
std::uint64_t read_u64(std::ifstream& input) {
    std::uint64_t value = 0U;
    for (std::size_t i = 0U; i < 8U; ++i) { const auto byte = input.get(); if (byte == EOF) throw std::runtime_error("bucket artifact is truncated"); value |= static_cast<std::uint64_t>(static_cast<unsigned char>(byte)) << (i * 8U); }
    return value;
}
}

MultiwayBucketArtifactInspection inspect_multiway_bucket_artifact(
    const std::filesystem::path& path, const MultiwayModelIdentity& expected_identity) {
    std::ifstream input(path, std::ios::binary);
    if (!input) throw std::runtime_error("multiway bucket artifact cannot be opened");
    std::array<char, 4U> magic{};
    input.read(magic.data(), 4);
    if (!input || magic != std::array<char, 4U>{'M', 'W', 'B', 'K'}) throw std::invalid_argument("invalid bucket artifact header");
    if (read_u32(input) != MULTIWAY_BUCKET_ARTIFACT_SCHEMA_VERSION) throw std::invalid_argument("unsupported bucket artifact schema");
    MultiwayModelIdentity identity;
    visit_multiway_model_identity_fields(identity, [&](std::uint64_t& field) { field = read_u64(input); });
    identity.validate();
    expected_identity.validate();
    if (identity != expected_identity) throw std::invalid_argument("bucket artifact identity mismatch");
    MultiwayBucketArtifactInspection result;
    result.identity = identity;
    result.payload_hash = core::fingerprint::FNV1A_OFFSET;
    const auto table_count = read_u32(input);
    if (table_count == 0U) throw std::invalid_argument("bucket artifact has no tables");
    for (std::uint32_t table_index = 0U; table_index < table_count; ++table_index) {
        const auto street = static_cast<core::Street>(input.get());
        const auto board_size = static_cast<std::size_t>(input.get());
        if (!input || board_size == 0U || board_size > 5U) throw std::runtime_error("invalid bucket board metadata");
        std::vector<std::uint8_t> board(board_size);
        input.read(reinterpret_cast<char*>(board.data()), static_cast<std::streamsize>(board.size()));
        if (!input || !is_multiway_canonical_board(street, board)) throw std::invalid_argument("bucket board is not canonical");
        const auto bucket_count = read_u32(input);
        if (bucket_count == 0U) throw std::invalid_argument("bucket count is zero");
        switch (street) {
            case core::Street::Flop: ++result.flop_tables; break;
            case core::Street::Turn: ++result.turn_tables; break;
            case core::Street::River: ++result.river_tables; break;
            default: throw std::invalid_argument("bucket artifact contains a non-postflop table");
        }
        core::fingerprint::append_u8(result.payload_hash, static_cast<std::uint8_t>(street));
        core::fingerprint::append_u8(result.payload_hash, static_cast<std::uint8_t>(board_size));
        for (const auto card : board) core::fingerprint::append_u8(result.payload_hash, card);
        hash_u32(result.payload_hash, bucket_count);
        for (std::size_t assignment_index = 0U; assignment_index < MULTIWAY_HOLE_COMBINATION_COUNT; ++assignment_index) {
            const auto assignment = read_u32(input);
            if (assignment != MULTIWAY_INVALID_BUCKET) ++result.live_assignments;
            if (assignment != MULTIWAY_INVALID_BUCKET && assignment >= bucket_count) throw std::invalid_argument("bucket assignment is out of range");
            hash_u32(result.payload_hash, assignment);
        }
    }
    if (input.peek() != EOF) throw std::invalid_argument("bucket artifact has trailing data");
    result.payload_hash = core::fingerprint::finish(result.payload_hash);
    return result;
}
}  // namespace texas::solver::multiway
