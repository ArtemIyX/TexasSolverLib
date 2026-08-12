#include "solver/multiway_future_bucket.hpp"

#include "games/hunl_eval.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace core {
namespace {

constexpr std::array<std::uint8_t, 4U> kMagic = {'M', 'F', 'B', '1'};

std::uint8_t rank(std::uint8_t compact) noexcept { return static_cast<std::uint8_t>(compact / 4U + 2U); }
std::uint8_t suit(std::uint8_t compact) noexcept { return static_cast<std::uint8_t>(compact % 4U); }

double squared_distance(const MultiwayFutureBucketFeatures& left,
                        const MultiwayFutureBucketFeatures& right) noexcept {
    double result = 0.0;
    for (std::size_t i = 0U; i < left.values.size(); ++i) {
        const auto delta = left.values[i] - right.values[i];
        result += delta * delta;
    }
    return result;
}

void append_u32(std::vector<std::uint8_t>& bytes, std::uint32_t value) {
    for (std::uint8_t shift = 0U; shift < 32U; shift += 8U) bytes.push_back(static_cast<std::uint8_t>(value >> shift));
}
void append_u64(std::vector<std::uint8_t>& bytes, std::uint64_t value) {
    for (std::uint8_t shift = 0U; shift < 64U; shift += 8U) bytes.push_back(static_cast<std::uint8_t>(value >> shift));
}
std::uint64_t read_u64(const std::vector<std::uint8_t>& bytes, std::size_t& cursor) {
    if (bytes.size() - cursor < 8U) throw std::invalid_argument("future bucket artifact is truncated");
    std::uint64_t value = 0U;
    for (std::uint8_t shift = 0U; shift < 64U; shift += 8U) value |= static_cast<std::uint64_t>(bytes[cursor++]) << shift;
    return value;
}
std::uint32_t read_u32(const std::vector<std::uint8_t>& bytes, std::size_t& cursor) {
    if (bytes.size() - cursor < 4U) throw std::invalid_argument("future bucket artifact is truncated");
    std::uint32_t value = 0U;
    for (std::uint8_t shift = 0U; shift < 32U; shift += 8U) value |= static_cast<std::uint32_t>(bytes[cursor++]) << shift;
    return value;
}

}  // namespace

void MultiwayFutureBucketProfile::validate() const {
    if (feature_version == 0U || clustering_version == 0U || seed == 0U || lloyd_iterations == 0U ||
        flop_bucket_count == 0U || turn_bucket_count == 0U || river_bucket_count == 0U) {
        throw std::invalid_argument("multiway future bucket profile is invalid");
    }
}

std::uint32_t MultiwayFutureBucketProfile::bucket_count(Street street) const {
    switch (street) {
        case Street::Flop: return flop_bucket_count;
        case Street::Turn: return turn_bucket_count;
        case Street::River: return river_bucket_count;
        default: throw std::invalid_argument("future buckets require a postflop street");
    }
}

MultiwayFutureBucketFeatures make_multiway_future_bucket_features(
    Street street, const std::vector<std::uint8_t>& board,
    const std::array<std::uint8_t, 2>& hole, std::uint64_t feature_version) {
    if (feature_version == 0U || !is_multiway_canonical_board(street, board) ||
        hole[0] >= 52U || hole[1] >= 52U || hole[0] == hole[1] ||
        std::find(board.begin(), board.end(), hole[0]) != board.end() ||
        std::find(board.begin(), board.end(), hole[1]) != board.end()) {
        throw std::invalid_argument("multiway future bucket features require live canonical cards");
    }
    MultiwayFutureBucketFeatures result;
    result.feature_version = feature_version;
    std::array<std::uint8_t, 4U> suit_counts = {};
    std::array<std::uint8_t, 15U> rank_counts = {};
    for (const auto card : board) { ++suit_counts[suit(card)]; ++rank_counts[rank(card)]; }
    const auto high = std::max(rank(hole[0]), rank(hole[1]));
    const auto low = std::min(rank(hole[0]), rank(hole[1]));
    result.values[0] = static_cast<double>(high) / 14.0;
    result.values[1] = static_cast<double>(low) / 14.0;
    result.values[2] = hole[0] / 4U == hole[1] / 4U ? 1.0 : 0.0;
    result.values[3] = suit(hole[0]) == suit(hole[1]) ? 1.0 : 0.0;
    result.values[4] = static_cast<double>(rank_counts[rank(hole[0])] + rank_counts[rank(hole[1])]) / board.size();
    result.values[5] = static_cast<double>(suit_counts[suit(hole[0])] + suit_counts[suit(hole[1])]) / board.size();
    result.values[6] = static_cast<double>(std::max({suit_counts[0], suit_counts[1], suit_counts[2], suit_counts[3]})) / board.size();
    result.values[7] = static_cast<double>(std::count_if(rank_counts.begin(), rank_counts.end(), [](auto count) { return count >= 2U; })) / 2.0;
    result.values[8] = static_cast<double>(high - low) / 12.0;
    if (board.size() >= 3U) {
        std::vector<std::uint8_t> cards;
        cards.reserve(board.size() + 2U);
        for (const auto card : board) cards.push_back(static_cast<std::uint8_t>(card + HUNL_CARD_FIRST));
        cards.push_back(static_cast<std::uint8_t>(hole[0] + HUNL_CARD_FIRST));
        cards.push_back(static_cast<std::uint8_t>(hole[1] + HUNL_CARD_FIRST));
        result.values[9] = static_cast<double>(evaluate_n(cards).value >> 56U) / 8.0;
    }
    return result;
}

MultiwayFutureBucketArtifact::MultiwayFutureBucketArtifact(
    MultiwayFutureBucketProfile profile, MultiwayBucketRegistry registry)
    : profile_(profile), registry_(std::move(registry)) {
    profile_.validate();
    for (const auto& table : registry_.tables()) {
        if (table.bucket_count() != profile_.bucket_count(table.street())) {
            throw std::invalid_argument("future bucket artifact profile does not match table count");
        }
    }
}

std::uint32_t MultiwayFutureBucketArtifact::lookup_hunl(
    Street street, const std::vector<std::uint8_t>& board,
    const std::array<std::uint8_t, 2>& hole) const {
    return registry_.lookup_hunl(street, board, hole);
}

MultiwayFutureBucketArtifact build_multiway_future_bucket_artifact(
    const MultiwayModelIdentity& identity, const std::vector<MultiwayBucketBoardRequest>& boards,
    const MultiwayFutureBucketProfile& profile) {
    identity.validate(); profile.validate();
    if (boards.empty()) throw std::invalid_argument("future bucket producer requires boards");
    std::vector<MultiwayBucketTable> tables;
    tables.reserve(boards.size());
    for (const auto& request : boards) {
        if (!is_multiway_canonical_board(request.street, request.canonical_board)) {
            throw std::invalid_argument("future bucket producer requires canonical boards");
        }
        const auto count = profile.bucket_count(request.street);
        std::vector<MultiwayFutureBucketFeatures> features;
        std::vector<std::size_t> indices;
        features.reserve(MULTIWAY_HOLE_COMBINATION_COUNT); indices.reserve(MULTIWAY_HOLE_COMBINATION_COUNT);
        std::vector<std::uint32_t> assignments(MULTIWAY_HOLE_COMBINATION_COUNT, MULTIWAY_INVALID_BUCKET);
        for (std::uint8_t a = 0U; a < 52U; ++a) for (std::uint8_t b = static_cast<std::uint8_t>(a + 1U); b < 52U; ++b) {
            const std::array<std::uint8_t, 2> hole = {a, b};
            if (std::find(request.canonical_board.begin(), request.canonical_board.end(), a) != request.canonical_board.end() ||
                std::find(request.canonical_board.begin(), request.canonical_board.end(), b) != request.canonical_board.end()) continue;
            indices.push_back(MultiwayBucketTable::hole_index(hole));
            features.push_back(make_multiway_future_bucket_features(request.street, request.canonical_board, hole, profile.feature_version));
        }
        if (count > features.size()) throw std::invalid_argument("future bucket count exceeds live combinations");
        std::vector<MultiwayFutureBucketFeatures> centers;
        centers.reserve(count);
        for (std::uint32_t cluster = 0U; cluster < count; ++cluster) {
            centers.push_back(features[(static_cast<std::size_t>(cluster) * features.size() + profile.seed) % features.size()]);
        }
        std::vector<std::uint32_t> labels(features.size(), 0U);
        for (std::uint32_t iteration = 0U; iteration < profile.lloyd_iterations; ++iteration) {
            std::vector<MultiwayFutureBucketFeatures> sums(count);
            std::vector<std::uint32_t> sizes(count, 0U);
            for (std::size_t i = 0U; i < features.size(); ++i) {
                auto best = 0U; auto distance = squared_distance(features[i], centers[0]);
                for (std::uint32_t cluster = 1U; cluster < count; ++cluster) {
                    const auto candidate = squared_distance(features[i], centers[cluster]);
                    if (candidate < distance) { distance = candidate; best = cluster; }
                }
                labels[i] = best; ++sizes[best];
                for (std::size_t value = 0U; value < sums[best].values.size(); ++value) sums[best].values[value] += features[i].values[value];
            }
            for (std::uint32_t cluster = 0U; cluster < count; ++cluster) if (sizes[cluster] != 0U) {
                for (std::size_t value = 0U; value < centers[cluster].values.size(); ++value) centers[cluster].values[value] = sums[cluster].values[value] / sizes[cluster];
            }
        }
        for (std::size_t i = 0U; i < indices.size(); ++i) assignments[indices[i]] = labels[i];
        tables.emplace_back(identity, request.street, request.canonical_board, count, std::move(assignments));
    }
    return {profile, MultiwayBucketRegistry(std::move(tables))};
}

std::vector<std::uint8_t> serialize_multiway_future_bucket_artifact(const MultiwayFutureBucketArtifact& artifact) {
    auto registry = serialize_multiway_bucket_registry(artifact.registry());
    std::vector<std::uint8_t> output;
    output.reserve(4U + 4U + 3U * 8U + 4U * 4U + registry.size());
    output.insert(output.end(), kMagic.begin(), kMagic.end()); append_u32(output, MULTIWAY_FUTURE_BUCKET_ARTIFACT_SCHEMA_VERSION);
    const auto& p = artifact.profile(); append_u64(output, p.feature_version); append_u64(output, p.clustering_version); append_u64(output, p.seed);
    append_u32(output, p.lloyd_iterations); append_u32(output, p.flop_bucket_count); append_u32(output, p.turn_bucket_count); append_u32(output, p.river_bucket_count);
    output.insert(output.end(), registry.begin(), registry.end()); return output;
}

MultiwayFutureBucketArtifact deserialize_multiway_future_bucket_artifact(const std::vector<std::uint8_t>& bytes) {
    if (bytes.size() < 48U || !std::equal(kMagic.begin(), kMagic.end(), bytes.begin())) throw std::invalid_argument("future bucket artifact has invalid header");
    std::size_t cursor = 4U;
    if (read_u32(bytes, cursor) != MULTIWAY_FUTURE_BUCKET_ARTIFACT_SCHEMA_VERSION) throw std::invalid_argument("future bucket artifact schema is unsupported");
    MultiwayFutureBucketProfile profile; profile.feature_version = read_u64(bytes, cursor); profile.clustering_version = read_u64(bytes, cursor); profile.seed = read_u64(bytes, cursor);
    profile.lloyd_iterations = read_u32(bytes, cursor); profile.flop_bucket_count = read_u32(bytes, cursor); profile.turn_bucket_count = read_u32(bytes, cursor); profile.river_bucket_count = read_u32(bytes, cursor);
    std::vector<std::uint8_t> registry(bytes.begin() + static_cast<std::ptrdiff_t>(cursor), bytes.end());
    return {profile, deserialize_multiway_bucket_registry(registry)};
}

}  // namespace core
