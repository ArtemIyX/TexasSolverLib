#include "solver/multiway/abstraction/multiway_bucket_artifact.hpp"
#include "solver/multiway/abstraction/multiway_bucket_catalog.hpp"
#include "solver/multiway/abstraction/multiway_bucket_generation.hpp"
#include "core/fingerprint.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <exception>
#include <fstream>
#include <limits>
#include <memory>
#include <mutex>
#include <chrono>
#include <thread>
#include <vector>

namespace texas::solver::multiway {
namespace {

constexpr std::size_t kReaderBytes = 1024U * 1024U;
constexpr std::uint32_t kLeafTables = 4096U;
constexpr std::uint64_t kHeaderBytes = 116U;

struct Range {
    std::uint64_t begin = 0U;
    std::uint32_t count = 0U;
    std::uint64_t bytes = 0U;
    core::Street street = core::Street::Flop;
    std::uint64_t street_index = 0U;
};

struct Reader {
    std::ifstream input;
    std::vector<std::uint8_t> buffer;
    std::uint64_t begin = 0U;
    std::uint64_t end = 0U;
    std::uint64_t position = 0U;
    std::size_t available = 0U;
    std::size_t offset = 0U;
    std::uint64_t* hash = nullptr;

    Reader(const std::filesystem::path& path, std::uint64_t first, std::uint64_t last, std::uint64_t* hash_value = nullptr)
        : buffer(kReaderBytes), begin(first), end(last), position(first), hash(hash_value) {
        input.open(path, std::ios::binary);
        if (!input) throw std::runtime_error("multiway bucket artifact cannot be opened");
        input.seekg(static_cast<std::streamoff>(first));
    }
    std::uint8_t get() {
        if (position >= end) throw std::runtime_error("bucket artifact is truncated");
        if (offset == available) {
            const auto want = static_cast<std::streamsize>(
                std::min<std::uint64_t>(buffer.size(), end - position));
            input.read(reinterpret_cast<char*>(buffer.data()), want);
            if (input.gcount() != want) throw std::runtime_error("bucket artifact is truncated");
            available = static_cast<std::size_t>(want);
            offset = 0U;
        }
        ++position;
        const auto value = buffer[offset++];
        if (hash != nullptr) core::fingerprint::append_u8(*hash, value);
        return value;
    }
    std::uint32_t get_u32() {
        std::uint32_t value = 0U;
        for (unsigned i = 0U; i < 4U; ++i) value |= std::uint32_t(get()) << (i * 8U);
        return value;
    }
};

std::uint64_t get_u64(std::ifstream& input) {
    std::uint64_t value = 0U;
    for (unsigned i = 0U; i < 8U; ++i) {
        const auto byte = input.get();
        if (byte == EOF) throw std::runtime_error("bucket artifact is truncated");
        value |= std::uint64_t(static_cast<std::uint8_t>(byte)) << (i * 8U);
    }
    return value;
}

std::uint32_t get_u32(std::ifstream& input) {
    std::uint32_t value = 0U;
    for (unsigned i = 0U; i < 4U; ++i) {
        const auto byte = input.get();
        if (byte == EOF) throw std::runtime_error("bucket artifact is truncated");
        value |= std::uint32_t(static_cast<std::uint8_t>(byte)) << (i * 8U);
    }
    return value;
}

std::uint32_t record_bytes(core::Street street) {
    switch (street) {
        case core::Street::Flop: return 5313U;
        case core::Street::Turn: return 5314U;
        case core::Street::River: return 5315U;
        default: throw std::invalid_argument("bucket artifact contains a non-postflop table");
    }
}

std::uint64_t street_start(core::Street street) {
    const auto flop = multiway_bucket_board_count(core::Street::Flop);
    const auto turn = multiway_bucket_board_count(core::Street::Turn);
    if (street == core::Street::Flop) return 0U;
    if (street == core::Street::Turn) return flop;
    return flop + turn;
}

struct Header {
    MultiwayModelIdentity identity{};
    std::uint32_t table_count = 0U;
};

Header read_header(const std::filesystem::path& path, const MultiwayModelIdentity& expected) {
    std::ifstream input(path, std::ios::binary);
    if (!input) throw std::runtime_error("multiway bucket artifact cannot be opened");
    std::array<char, 4U> magic{};
    input.read(magic.data(), 4);
    if (!input || magic != std::array<char, 4U>{'M', 'W', 'B', 'K'})
        throw std::invalid_argument("invalid bucket artifact header");
    if (get_u32(input) != MULTIWAY_BUCKET_ARTIFACT_SCHEMA_VERSION)
        throw std::invalid_argument("unsupported bucket artifact schema");
    Header header;
    visit_multiway_model_identity_fields(header.identity, [&](std::uint64_t& field) { field = get_u64(input); });
    header.identity.validate();
    expected.validate();
    if (header.identity != expected) throw std::invalid_argument("bucket artifact identity mismatch");
    const auto count = get_u32(input);
    if (count == 0U || count > std::numeric_limits<std::uint32_t>::max())
        throw std::invalid_argument("bucket artifact has invalid table count");
    header.table_count = static_cast<std::uint32_t>(count);
    return header;
}

bool complete_layout(const Header& header, std::uint64_t file_bytes) {
    const auto total = multiway_bucket_board_count(core::Street::Flop) +
        multiway_bucket_board_count(core::Street::Turn) +
        multiway_bucket_board_count(core::Street::River);
    if (header.table_count != total) return false;
    std::uint64_t expected = kHeaderBytes;
    for (const auto street : {core::Street::Flop, core::Street::Turn, core::Street::River}) {
        const auto bytes = multiway_bucket_board_count(street) * record_bytes(street);
        if (expected > std::numeric_limits<std::uint64_t>::max() - bytes) return false;
        expected += bytes;
    }
    return expected == file_bytes;
}

struct LeafResult {
    std::uint64_t tables = 0U;
    std::uint64_t live = 0U;
    std::uint64_t bytes = 0U;
    std::uint64_t hash = core::fingerprint::FNV1A_OFFSET;
    std::exception_ptr error{};
    std::uint64_t error_table = std::numeric_limits<std::uint64_t>::max();
};

void append_u64(std::uint64_t& hash, std::uint64_t value) noexcept {
    core::fingerprint::append_u64(hash, value);
}

void inspect_leaf(const std::filesystem::path& path, const Range& range, LeafResult& result) {
    result.bytes = range.bytes;
    Reader reader(path, range.begin, range.begin + range.bytes, &result.hash);
    std::array<std::uint8_t, 5U> board{};
    const auto catalog = MultiwayBucketBoardCatalog(range.street);
    for (std::uint32_t table = 0U; table < range.count; ++table) {
        const auto table_index = range.street_index + table;
        const auto street = static_cast<core::Street>(reader.get());
        const auto board_size = reader.get();
        if (street != range.street || board_size != (range.street == core::Street::Flop ? 3U :
            range.street == core::Street::Turn ? 4U : 5U))
            throw std::invalid_argument("bucket board metadata does not match layout");
        for (std::uint8_t i = 0U; i < board_size; ++i) board[i] = reader.get();
        std::array<std::uint8_t, 5U> expected{};
        catalog.for_each_fixed_board(table_index, table_index + 1U,
            [&](const std::array<std::uint8_t, 5U>& value) { expected = value; });
        for (std::uint8_t i = 0U; i < board_size; ++i) {
            if (board[i] != expected[i]) throw std::invalid_argument("bucket board ordering is invalid");
        }
        const auto buckets = reader.get_u32();
        if (buckets == 0U) throw std::invalid_argument("bucket count is zero");
        for (std::uint32_t i = 0U; i < MULTIWAY_HOLE_COMBINATION_COUNT; ++i) {
            const auto assignment = reader.get_u32();
            if (assignment != MULTIWAY_INVALID_BUCKET) {
                ++result.live;
                if (assignment >= buckets) throw std::invalid_argument("bucket assignment is out of range");
            }
        }
        ++result.tables;
    }
    if (reader.position != reader.end) throw std::invalid_argument("bucket table range was not exhausted");
}

MultiwayBucketArtifactInspection legacy(const std::filesystem::path& path,
    const MultiwayModelIdentity& expected, MultiwayBucketInspectionProgressCallback callback,
    std::uint64_t interval) {
    if (callback && interval == 0U) throw std::invalid_argument("bucket inspection progress interval is zero");
    const auto header = read_header(path, expected);
    const auto size = std::filesystem::file_size(path);
    MultiwayBucketArtifactInspection result;
    result.identity = header.identity;
    result.payload_hash = core::fingerprint::FNV1A_OFFSET;
    result.hash_mode = MultiwayBucketInspectionHashMode::Legacy;
    Reader reader(path, kHeaderBytes, size, &result.payload_hash);
    for (std::uint32_t table = 0U; table < header.table_count; ++table) {
        const auto street = reader.get();
        const auto board_size = reader.get();
        if (board_size == 0U || board_size > 5U) throw std::runtime_error("invalid bucket board metadata");
        std::array<std::uint8_t, 5U> board{};
        for (std::uint8_t i = 0U; i < board_size; ++i) board[i] = reader.get();
        const auto buckets = reader.get_u32();
        if (!is_multiway_canonical_board(static_cast<core::Street>(street),
            std::vector<std::uint8_t>(board.begin(), board.begin() + board_size)))
            throw std::invalid_argument("bucket board is not canonical");
        if (street == static_cast<std::uint8_t>(core::Street::Flop)) ++result.flop_tables;
        else if (street == static_cast<std::uint8_t>(core::Street::Turn)) ++result.turn_tables;
        else if (street == static_cast<std::uint8_t>(core::Street::River)) ++result.river_tables;
        else throw std::invalid_argument("bucket artifact contains a non-postflop table");
        for (std::uint32_t i = 0U; i < MULTIWAY_HOLE_COMBINATION_COUNT; ++i) {
            const auto assignment = reader.get_u32();
            if (assignment != MULTIWAY_INVALID_BUCKET) {
                ++result.live_assignments;
                if (assignment >= buckets) throw std::invalid_argument("bucket assignment is out of range");
            }
        }
        if (callback && ((table + 1U) % interval == 0U || table + 1U == header.table_count))
            callback(table + 1U, header.table_count);
    }
    if (reader.position != reader.end) throw std::invalid_argument("bucket artifact has trailing data");
    result.payload_hash = core::fingerprint::finish(result.payload_hash);
    result.parallel_payload_hash = result.payload_hash;
    return result;
}

} // namespace

// Compatibility parser retained as the source-of-truth byte-stream path.
void compatibility_hash_u32(std::uint64_t& hash, std::uint32_t value) noexcept {
    for (unsigned i = 0U; i < 4U; ++i)
        core::fingerprint::append_u8(hash, static_cast<std::uint8_t>(value >> (i * 8U)));
}
MultiwayBucketArtifactInspection legacy_compat(const std::filesystem::path& path,
    const MultiwayModelIdentity& expected, MultiwayBucketInspectionProgressCallback callback,
    std::uint64_t interval) {
    if (callback && interval == 0U) throw std::invalid_argument("bucket inspection progress interval is zero");
    std::ifstream input(path, std::ios::binary);
    if (!input) throw std::runtime_error("multiway bucket artifact cannot be opened");
    std::array<char, 4U> magic{}; input.read(magic.data(), 4);
    if (!input || magic != std::array<char, 4U>{'M','W','B','K'}) throw std::invalid_argument("invalid bucket artifact header");
    if (get_u32(input) != MULTIWAY_BUCKET_ARTIFACT_SCHEMA_VERSION) throw std::invalid_argument("unsupported bucket artifact schema");
    MultiwayModelIdentity identity;
    visit_multiway_model_identity_fields(identity, [&](std::uint64_t& field) { field = get_u64(input); });
    identity.validate(); expected.validate(); if (identity != expected) throw std::invalid_argument("bucket artifact identity mismatch");
    const auto count = get_u32(input); if (!count) throw std::invalid_argument("bucket artifact has no tables");
    MultiwayBucketArtifactInspection result; result.identity = identity; result.payload_hash = core::fingerprint::FNV1A_OFFSET;
    std::vector<std::uint8_t> board;
    board.reserve(5U);
    for (std::uint32_t table = 0U; table < count; ++table) {
        const auto street = input.get(); const auto board_size = input.get();
        if (!input || board_size == 0 || board_size > 5) throw std::runtime_error("invalid bucket board metadata");
        board.resize(static_cast<std::size_t>(board_size));
        input.read(reinterpret_cast<char*>(board.data()), board_size);
        if (!input || !is_multiway_canonical_board(static_cast<core::Street>(street), board)) throw std::invalid_argument("bucket board is not canonical");
        const auto buckets = get_u32(input); if (!buckets) throw std::invalid_argument("bucket count is zero");
        if (street == static_cast<int>(core::Street::Flop)) ++result.flop_tables;
        else if (street == static_cast<int>(core::Street::Turn)) ++result.turn_tables;
        else if (street == static_cast<int>(core::Street::River)) ++result.river_tables;
        else throw std::invalid_argument("bucket artifact contains a non-postflop table");
        core::fingerprint::append_u8(result.payload_hash, static_cast<std::uint8_t>(street));
        core::fingerprint::append_u8(result.payload_hash, static_cast<std::uint8_t>(board_size));
        for (const auto card : board) core::fingerprint::append_u8(result.payload_hash, card);
        compatibility_hash_u32(result.payload_hash, buckets);
        for (std::size_t assignment = 0U; assignment < MULTIWAY_HOLE_COMBINATION_COUNT; ++assignment) {
            const auto value = get_u32(input); if (value != MULTIWAY_INVALID_BUCKET) { ++result.live_assignments; if (value >= buckets) throw std::invalid_argument("bucket assignment is out of range"); } compatibility_hash_u32(result.payload_hash, value);
        }
        if (callback && ((table + 1U) % interval == 0U || table + 1U == count)) callback(table + 1U, count);
    }
    if (input.peek() != EOF) throw std::invalid_argument("bucket artifact has trailing data");
    result.payload_hash = core::fingerprint::finish(result.payload_hash); result.parallel_payload_hash = result.payload_hash; return result;
}

MultiwayBucketArtifactInspection inspect_multiway_bucket_artifact(
    const std::filesystem::path& path, const MultiwayModelIdentity& expected,
    MultiwayBucketInspectionProgressCallback callback, std::uint64_t interval) {
    return legacy(path, expected, std::move(callback), interval);
}

MultiwayBucketArtifactInspection inspect_multiway_bucket_artifact(
    const std::filesystem::path& path, const MultiwayModelIdentity& expected,
    const MultiwayBucketInspectionOptions& options,
    MultiwayBucketInspectionProgressCallback callback, std::uint64_t interval) {
    if (options.hash_mode == MultiwayBucketInspectionHashMode::Legacy || options.requested_threads == 1U)
        return legacy(path, expected, std::move(callback), interval);
    const auto header = read_header(path, expected);
    if (!complete_layout(header, std::filesystem::file_size(path)))
        return legacy(path, expected, std::move(callback), interval);
    if (interval == 0U && callback) throw std::invalid_argument("bucket inspection progress interval is zero");
    std::vector<Range> ranges;
    for (const auto street : {core::Street::Flop, core::Street::Turn, core::Street::River}) {
        const auto count = multiway_bucket_board_count(street);
        const auto bytes = record_bytes(street);
        for (std::uint64_t first = 0U; first < count; first += kLeafTables) {
            const auto n = static_cast<std::uint32_t>(std::min<std::uint64_t>(kLeafTables, count - first));
            const auto before = street == core::Street::Flop ? 0U : street == core::Street::Turn
                ? multiway_bucket_board_count(core::Street::Flop) * record_bytes(core::Street::Flop)
                : multiway_bucket_board_count(core::Street::Flop) * record_bytes(core::Street::Flop) +
                    multiway_bucket_board_count(core::Street::Turn) * record_bytes(core::Street::Turn);
            ranges.push_back({kHeaderBytes + before + first * bytes, n,
                std::uint64_t(n) * bytes, street, first});
        }
    }
    std::vector<LeafResult> results(ranges.size());
    const auto workers = std::max(1U, std::min(
        options.requested_threads ? options.requested_threads : multiway_bucket_physical_core_count(),
        static_cast<std::uint32_t>(ranges.size())));
    std::atomic<std::size_t> next{0U};
    std::atomic<std::size_t> completed_workers{0U};
    auto finished = std::make_unique<std::atomic_bool[]>(ranges.size());
    for (std::size_t i = 0U; i < ranges.size(); ++i) finished[i].store(false);
    std::vector<std::thread> threads;
    for (std::uint32_t worker = 0U; worker < workers; ++worker) {
        threads.emplace_back([&] {
            for (;;) {
                const auto index = next.fetch_add(1U);
                if (index >= ranges.size()) break;
                try { inspect_leaf(path, ranges[index], results[index]); }
                catch (...) { results[index].error = std::current_exception(); results[index].error_table = ranges[index].street_index; }
                finished[index].store(true, std::memory_order_release);
                completed_workers.fetch_add(1U, std::memory_order_release);
            }
        });
    }
    std::uint64_t frontier = 0U;
    std::uint64_t next_progress = interval;
    std::exception_ptr callback_error;
    while (completed_workers.load(std::memory_order_acquire) < ranges.size()) {
        while (frontier < ranges.size() && finished[frontier].load(std::memory_order_acquire)) {
            ++frontier;
            const auto completed_tables = frontier == ranges.size() ? header.table_count :
                std::min<std::uint64_t>(header.table_count, frontier * kLeafTables);
            if (callback && !callback_error && completed_tables >= next_progress) {
                try { callback(completed_tables, header.table_count); }
                catch (...) { callback_error = std::current_exception(); }
                while (next_progress <= completed_tables) next_progress += interval;
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    for (auto& thread : threads) thread.join();
    if (callback_error) std::rethrow_exception(callback_error);
    MultiwayBucketArtifactInspection result;
    result.identity = header.identity;
    result.effective_threads = workers;
    result.hash_mode = options.hash_mode;
    for (const auto& leaf : results) {
        if (leaf.error) std::rethrow_exception(leaf.error);
        result.live_assignments += leaf.live;
    }
    if (callback && !callback_error && frontier == ranges.size() && next_progress > header.table_count) callback(header.table_count, header.table_count);
    result.parallel_payload_hash = core::fingerprint::FNV1A_OFFSET;
    append_u64(result.parallel_payload_hash, 0x666E7631ULL); // fnv1a64-tree-v1
    append_u64(result.parallel_payload_hash, std::filesystem::file_size(path) - kHeaderBytes);
    for (std::size_t i = 0U; i < results.size(); ++i) {
        append_u64(result.parallel_payload_hash, i);
        append_u64(result.parallel_payload_hash, results[i].bytes);
        append_u64(result.parallel_payload_hash, results[i].hash);
    }
    result.parallel_payload_hash = core::fingerprint::finish(result.parallel_payload_hash);
    result.payload_hash = result.parallel_payload_hash;
    result.flop_tables = multiway_bucket_board_count(core::Street::Flop);
    result.turn_tables = multiway_bucket_board_count(core::Street::Turn);
    result.river_tables = multiway_bucket_board_count(core::Street::River);
    if (options.hash_mode == MultiwayBucketInspectionHashMode::Both)
        result.payload_hash = legacy(path, expected, {}, 1U).payload_hash;
    return result;
}
} // namespace texas::solver::multiway
