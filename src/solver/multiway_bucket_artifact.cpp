#include "solver/multiway_bucket_artifact.hpp"
#include "core/canonical_combo.hpp"
#include "core/fingerprint.hpp"
#include "core/poker.hpp"

#include <algorithm>
#include <limits>
#include <iterator>
#include <stdexcept>
#include <utility>

namespace texas::solver::multiway {

using core::CanonicalComboId;
using core::Street;
using core::canonical_combos;
using core::is_card_index;
using core::rank_of;
using core::suit_of;
namespace {

constexpr std::uint8_t kMagic[] = {'M', 'W', 'B', 'K'};
constexpr std::size_t kIdentityFieldCount = 13U;

std::size_t board_size_for(Street street) noexcept {
    switch (street) {
        case Street::Flop: return 3U;
        case Street::Turn: return 4U;
        case Street::River: return 5U;
        default: return 0U;
    }
}

void append_u32(std::vector<std::uint8_t>& output, std::uint32_t value) {
    for (std::uint8_t index = 0U; index < 4U; ++index) {
        output.push_back(static_cast<std::uint8_t>((value >> (index * 8U)) & 0xffU));
    }
}

void append_u64(std::vector<std::uint8_t>& output, std::uint64_t value) {
    for (std::uint8_t index = 0U; index < 8U; ++index) {
        output.push_back(static_cast<std::uint8_t>((value >> (index * 8U)) & 0xffU));
    }
}

std::uint32_t read_u32(const std::vector<std::uint8_t>& input, std::size_t& cursor) {
    if (input.size() - cursor < 4U) throw std::invalid_argument("multiway bucket artifact is truncated");
    std::uint32_t value = 0U;
    for (std::uint8_t index = 0U; index < 4U; ++index) {
        value |= static_cast<std::uint32_t>(input[cursor++]) << (index * 8U);
    }
    return value;
}

std::uint64_t read_u64(const std::vector<std::uint8_t>& input, std::size_t& cursor) {
    if (input.size() - cursor < 8U) throw std::invalid_argument("multiway bucket artifact is truncated");
    std::uint64_t value = 0U;
    for (std::uint8_t index = 0U; index < 8U; ++index) {
        value |= static_cast<std::uint64_t>(input[cursor++]) << (index * 8U);
    }
    return value;
}

void append_identity(std::vector<std::uint8_t>& output, const MultiwayModelIdentity& identity) {
    visit_multiway_model_identity_fields(identity, [&](std::uint64_t field) {
        append_u64(output, field);
    });
}

MultiwayModelIdentity read_identity(const std::vector<std::uint8_t>& input, std::size_t& cursor) {
    MultiwayModelIdentity identity;
    visit_multiway_model_identity_fields(identity, [&](std::uint64_t& field) {
        field = read_u64(input, cursor);
    });
    identity.validate();
    return identity;
}

std::uint64_t feature_hash(const MultiwayBucketFeatures& features, Street street) noexcept {
    auto hash = texas::core::fingerprint::FNV1A_OFFSET;
    const auto append = [&hash](std::uint64_t value) noexcept {
        texas::core::fingerprint::append_u64(hash, value);
    };
    append(static_cast<std::uint8_t>(street));
    append(features.board_rank_mask);
    append(features.board_suit_mask);
    append(features.board_pair_count);
    append(features.board_size);
    append(features.hole_high_rank);
    append(features.hole_low_rank);
    append(features.hole_suited);
    append(features.hole_pairs_board);
    append(features.hole_suit_matches_board);
    return hash;
}

bool request_less(const MultiwayBucketBoardRequest& left, const MultiwayBucketBoardRequest& right) {
    if (left.street != right.street) return left.street < right.street;
    return left.canonical_board < right.canonical_board;
}

bool are_valid_compact_cards(const std::uint8_t* cards, std::size_t count) noexcept {
    if (cards == nullptr && count != 0U) return false;
    for (std::size_t index = 0U; index < count; ++index) {
        if (!is_card_index(cards[index])) return false;
        for (std::size_t prior = 0U; prior < index; ++prior) {
            if (cards[index] == cards[prior]) return false;
        }
    }
    return true;
}

}  // namespace

MultiwayBucketBaselineProfile MultiwayBucketBaselineProfile::standard() noexcept {
    return {};
}

void MultiwayBucketBaselineProfile::validate() const {
    if (schema_version != MULTIWAY_BUCKET_ARTIFACT_SCHEMA_VERSION || feature_version == 0U ||
        flop_bucket_count == 0U || turn_bucket_count == 0U || river_bucket_count == 0U) {
        throw std::invalid_argument("multiway bucket baseline profile is invalid");
    }
}

std::uint32_t MultiwayBucketBaselineProfile::bucket_count(Street street) const {
    validate();
    switch (street) {
        case Street::Flop: return flop_bucket_count;
        case Street::Turn: return turn_bucket_count;
        case Street::River: return river_bucket_count;
        default: throw std::invalid_argument("multiway buckets require a postflop street");
    }
}

bool is_multiway_canonical_board(Street street, const std::vector<std::uint8_t>& board) noexcept {
    const auto expected_size = board_size_for(street);
    if (expected_size == 0U || board.size() != expected_size ||
        !are_valid_compact_cards(board.data(), board.size())) {
        return false;
    }
    return std::is_sorted(board.begin(), board.end());
}

MultiwayBucketFeatures make_multiway_bucket_features(
    Street street,
    const std::vector<std::uint8_t>& canonical_board,
    const std::array<std::uint8_t, 2>& hole) {
    if (!is_multiway_canonical_board(street, canonical_board) ||
        !are_valid_compact_cards(hole.data(), hole.size()) ||
        std::find(canonical_board.begin(), canonical_board.end(), hole[0]) != canonical_board.end() ||
        std::find(canonical_board.begin(), canonical_board.end(), hole[1]) != canonical_board.end()) {
        throw std::invalid_argument("multiway bucket features require a live hole pair and canonical board");
    }

    MultiwayBucketFeatures features;
    features.board_size = static_cast<std::uint8_t>(canonical_board.size());
    std::array<std::uint8_t, 15U> rank_counts = {};
    for (const auto card : canonical_board) {
        const auto rank = rank_of(card);
        const auto suit = suit_of(card);
        features.board_rank_mask |= static_cast<std::uint16_t>(1U << rank);
        features.board_suit_mask |= static_cast<std::uint8_t>(1U << suit);
        ++rank_counts[rank];
        if (rank == rank_of(hole[0]) || rank == rank_of(hole[1])) ++features.hole_pairs_board;
        if (suit == suit_of(hole[0])) ++features.hole_suit_matches_board;
        if (suit == suit_of(hole[1])) ++features.hole_suit_matches_board;
    }
    for (const auto count : rank_counts) {
        if (count >= 2U) ++features.board_pair_count;
    }
    features.hole_high_rank = std::max(rank_of(hole[0]), rank_of(hole[1]));
    features.hole_low_rank = std::min(rank_of(hole[0]), rank_of(hole[1]));
    features.hole_suited = suit_of(hole[0]) == suit_of(hole[1]) ? 1U : 0U;
    return features;
}

std::uint32_t assign_multiway_baseline_bucket(
    const MultiwayBucketFeatures& features,
    const MultiwayBucketBaselineProfile& profile,
    Street street) {
    const auto count = profile.bucket_count(street);
    const auto versioned_hash = feature_hash(features, street) ^
        (profile.feature_version * 0x9e3779b97f4a7c15ULL);
    return static_cast<std::uint32_t>(versioned_hash % count);
}

MultiwayBucketTable build_multiway_baseline_bucket_table(
    const MultiwayModelIdentity& identity,
    Street street,
    std::vector<std::uint8_t> canonical_board,
    const MultiwayBucketBaselineProfile& profile) {
    identity.validate();
    profile.validate();
    if (!is_multiway_canonical_board(street, canonical_board)) {
        throw std::invalid_argument("multiway bucket builder requires a canonical sorted board");
    }
    const auto count = profile.bucket_count(street);
    std::vector<std::uint32_t> assignments(MULTIWAY_HOLE_COMBINATION_COUNT, MULTIWAY_INVALID_BUCKET);
    for (CanonicalComboId id = 0U; id < MULTIWAY_HOLE_COMBINATION_COUNT; ++id) {
        const auto& hole = canonical_combos().cards(id);
        if (std::find(canonical_board.begin(), canonical_board.end(), hole[0]) != canonical_board.end() ||
            std::find(canonical_board.begin(), canonical_board.end(), hole[1]) != canonical_board.end()) {
            continue;
        }
        assignments[id] = assign_multiway_baseline_bucket(
            make_multiway_bucket_features(street, canonical_board, hole), profile, street);
    }
    return {identity, street, std::move(canonical_board), count, std::move(assignments)};
}

MultiwayBucketRegistry build_multiway_baseline_bucket_registry(
    const MultiwayModelIdentity& identity,
    const std::vector<MultiwayBucketBoardRequest>& boards,
    const MultiwayBucketBaselineProfile& profile) {
    profile.validate();
    if (boards.empty()) throw std::invalid_argument("multiway bucket builder requires boards");
    std::vector<MultiwayBucketTable> tables;
    tables.reserve(boards.size());
    for (const auto& board : boards) {
        tables.push_back(build_multiway_baseline_bucket_table(
            identity, board.street, board.canonical_board, profile));
    }
    return MultiwayBucketRegistry(std::move(tables));
}

void validate_multiway_bucket_coverage(
    const MultiwayBucketRegistry& registry,
    const std::vector<MultiwayBucketBoardRequest>& required_boards) {
    if (required_boards.empty()) throw std::invalid_argument("multiway bucket coverage requires boards");
    std::vector<MultiwayBucketBoardRequest> ordered = required_boards;
    std::sort(ordered.begin(), ordered.end(), request_less);
    for (std::size_t index = 0U; index < ordered.size(); ++index) {
        const auto& board = ordered[index];
        if (!is_multiway_canonical_board(board.street, board.canonical_board)) {
            throw std::invalid_argument("multiway bucket coverage requires canonical boards");
        }
        if (index != 0U && !request_less(ordered[index - 1U], board)) {
            throw std::invalid_argument("multiway bucket coverage has duplicate board requests");
        }
        const auto& table = registry.table(board.street, board.canonical_board);
        if (table.bucket_count() == 0U) throw std::logic_error("multiway bucket coverage found empty table");
    }
}

std::vector<std::uint8_t> serialize_multiway_bucket_registry(const MultiwayBucketRegistry& registry) {
    registry.identity().validate();
    const auto& tables = registry.tables();
    if (tables.size() > std::numeric_limits<std::uint32_t>::max()) {
        throw std::overflow_error("multiway bucket artifact table count overflows format");
    }
    constexpr std::size_t kHeaderBytes = 4U + 4U + kIdentityFieldCount * 8U + 4U;
    constexpr std::size_t kLargestTableBytes = 1U + 1U + 5U + 4U +
                                               MULTIWAY_HOLE_COMBINATION_COUNT * 4U;
    if (tables.size() > (std::numeric_limits<std::size_t>::max() - kHeaderBytes) / kLargestTableBytes) {
        throw std::overflow_error("multiway bucket artifact size overflows address space");
    }
    std::vector<std::uint8_t> output;
    output.reserve(kHeaderBytes + tables.size() * kLargestTableBytes);
    output.insert(output.end(), std::begin(kMagic), std::end(kMagic));
    append_u32(output, MULTIWAY_BUCKET_ARTIFACT_SCHEMA_VERSION);
    append_identity(output, registry.identity());
    append_u32(output, static_cast<std::uint32_t>(tables.size()));
    for (const auto& table : tables) {
        output.push_back(static_cast<std::uint8_t>(table.street()));
        output.push_back(static_cast<std::uint8_t>(table.canonical_board().size()));
        output.insert(output.end(), table.canonical_board().begin(), table.canonical_board().end());
        append_u32(output, table.bucket_count());
        for (const auto assignment : table.assignments()) append_u32(output, assignment);
    }
    return output;
}

MultiwayBucketRegistry deserialize_multiway_bucket_registry(const std::vector<std::uint8_t>& bytes) {
    constexpr std::size_t kMinimumSize = 4U + 4U + kIdentityFieldCount * 8U + 4U;
    if (bytes.size() < kMinimumSize || !std::equal(std::begin(kMagic), std::end(kMagic), bytes.begin())) {
        throw std::invalid_argument("multiway bucket artifact has invalid header");
    }
    std::size_t cursor = 4U;
    if (read_u32(bytes, cursor) != MULTIWAY_BUCKET_ARTIFACT_SCHEMA_VERSION) {
        throw std::invalid_argument("multiway bucket artifact schema is unsupported");
    }
    const auto identity = read_identity(bytes, cursor);
    const auto table_count = read_u32(bytes, cursor);
    if (table_count == 0U) throw std::invalid_argument("multiway bucket artifact has no tables");
    constexpr std::size_t kSmallestTableBytes = 2U + 3U + 4U + MULTIWAY_HOLE_COMBINATION_COUNT * 4U;
    if (static_cast<std::size_t>(table_count) > (bytes.size() - cursor) / kSmallestTableBytes) {
        throw std::invalid_argument("multiway bucket artifact table count is invalid");
    }
    std::vector<MultiwayBucketTable> tables;
    tables.reserve(table_count);
    for (std::uint32_t index = 0U; index < table_count; ++index) {
        if (bytes.size() - cursor < 2U) throw std::invalid_argument("multiway bucket artifact is truncated");
        const auto street = static_cast<Street>(bytes[cursor++]);
        const auto board_size = static_cast<std::size_t>(bytes[cursor++]);
        if (board_size != board_size_for(street) || bytes.size() - cursor < board_size) {
            throw std::invalid_argument("multiway bucket artifact has invalid board metadata");
        }
        std::vector<std::uint8_t> board(bytes.begin() + static_cast<std::ptrdiff_t>(cursor),
                                        bytes.begin() + static_cast<std::ptrdiff_t>(cursor + board_size));
        cursor += board_size;
        const auto bucket_count = read_u32(bytes, cursor);
        std::vector<std::uint32_t> assignments;
        assignments.reserve(MULTIWAY_HOLE_COMBINATION_COUNT);
        for (std::size_t assignment = 0U; assignment < MULTIWAY_HOLE_COMBINATION_COUNT; ++assignment) {
            assignments.push_back(read_u32(bytes, cursor));
        }
        if (!is_multiway_canonical_board(street, board)) {
            throw std::invalid_argument("multiway bucket artifact board is not canonical");
        }
        tables.emplace_back(identity, street, std::move(board), bucket_count, std::move(assignments));
    }
    if (cursor != bytes.size()) throw std::invalid_argument("multiway bucket artifact has trailing data");
    return MultiwayBucketRegistry(std::move(tables));
}

}  // namespace texas::solver::multiway
