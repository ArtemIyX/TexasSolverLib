#pragma once

#include "core/hunl.hpp"

#include <array>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace core {

inline constexpr std::uint8_t ABSTRACTION_SCHEMA_VERSION = 1;
inline constexpr std::array<std::array<std::uint8_t, 4>, 24> SUIT_PERMUTATIONS = {{
    {{0, 1, 2, 3}}, {{0, 1, 3, 2}}, {{0, 2, 1, 3}}, {{0, 2, 3, 1}}, {{0, 3, 1, 2}}, {{0, 3, 2, 1}},
    {{1, 0, 2, 3}}, {{1, 0, 3, 2}}, {{1, 2, 0, 3}}, {{1, 2, 3, 0}}, {{1, 3, 0, 2}}, {{1, 3, 2, 0}},
    {{2, 0, 1, 3}}, {{2, 0, 3, 1}}, {{2, 1, 0, 3}}, {{2, 1, 3, 0}}, {{2, 3, 0, 1}}, {{2, 3, 1, 0}},
    {{3, 0, 1, 2}}, {{3, 0, 2, 1}}, {{3, 1, 0, 2}}, {{3, 1, 2, 0}}, {{3, 2, 0, 1}}, {{3, 2, 1, 0}},
}};

struct AbstractionMetadata {
    std::uint8_t schema_version = ABSTRACTION_SCHEMA_VERSION;
    std::string version;
    std::vector<std::uint16_t> bucket_counts;
    std::uint16_t feature_bins = 0;
    std::uint64_t seed = 0;
};

struct AbstractionTables {
    std::vector<std::uint8_t> flop_assignments;
    std::vector<std::uint8_t> turn_assignments;
    std::vector<std::uint8_t> river_assignments;

    std::unordered_map<std::string, std::uint32_t> flop_board_index;
    std::unordered_map<std::string, std::uint32_t> turn_board_index;
    std::unordered_map<std::string, std::uint32_t> river_board_index;

    std::unordered_map<std::string, std::unordered_map<std::string, std::uint32_t>> flop_hand_index;
    std::unordered_map<std::string, std::unordered_map<std::string, std::uint32_t>> turn_hand_index;
    std::unordered_map<std::string, std::unordered_map<std::string, std::uint32_t>> river_hand_index;

    AbstractionMetadata metadata;
    std::filesystem::path source_path;
};

enum class AbstractionErrorCode {
    Io,
    Malformed,
    SchemaMismatch,
    VersionMismatch,
    Unsupported,
};

struct AbstractionError {
    AbstractionErrorCode code = AbstractionErrorCode::Unsupported;
    std::string message;
};

std::string canonicalize_board(const std::vector<std::uint8_t>& board, std::size_t* perm_index = nullptr);
std::pair<std::string, std::string> canonicalize(const std::vector<std::uint8_t>& board, const std::array<std::uint8_t, 2>& hole);
AbstractionTables load_abstraction(const std::filesystem::path& path);
std::int32_t lookup_bucket(
    const AbstractionTables& tables,
    const std::vector<std::uint8_t>& board,
    const std::array<std::uint8_t, 2>& hole,
    Street street);

}  // namespace core
