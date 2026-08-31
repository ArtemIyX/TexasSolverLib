#include "solver/multiway/abstraction/multiway_bucket_generation.hpp"

#include "solver/multiway/abstraction/multiway_bucket_catalog.hpp"
#include "core/fingerprint.hpp"
#include "core/poker.hpp"

#include <algorithm>
#include <array>
#include <condition_variable>
#include <chrono>
#include <exception>
#include <map>
#include <limits>
#include <mutex>
#include <stdexcept>
#include <thread>
#if defined(_WIN32)
#include <windows.h>
#endif

namespace texas::solver::multiway {
using core::CanonicalComboId;
namespace {

struct GenerationChunk {
    std::uint64_t begin_index = 0U;
    std::vector<MultiwayBucketTable> tables;
};

struct SharedGenerationState {
    std::mutex mutex;
    std::condition_variable condition;
    std::uint64_t next_job = 0U;
    std::uint32_t in_flight = 0U;
    std::map<std::uint64_t, GenerationChunk> ready;
    std::exception_ptr failure;
};

void append_u32(std::vector<std::uint8_t>& payload, std::uint32_t value) {
    for (std::size_t index = 0U; index < 4U; ++index) payload.push_back(static_cast<std::uint8_t>(value >> (index * 8U)));
}

void write_u32(std::uint8_t*& cursor, std::uint32_t value) noexcept {
    for (std::size_t index = 0U; index < 4U; ++index) *cursor++ = static_cast<std::uint8_t>(value >> (index * 8U));
}

struct ComboMetadata { std::array<std::uint8_t, 2U> cards; std::array<std::uint8_t, 2U> ranks; std::array<std::uint8_t, 2U> suits; };

const std::array<ComboMetadata, MULTIWAY_HOLE_COMBINATION_COUNT>& combo_metadata() {
    static const auto result = [] {
        std::array<ComboMetadata, MULTIWAY_HOLE_COMBINATION_COUNT> value{};
        for (CanonicalComboId id = 0U; id < MULTIWAY_HOLE_COMBINATION_COUNT; ++id) {
            const auto cards = core::canonical_combos().cards(id);
            value[id] = {cards, {core::rank_of(cards[0]), core::rank_of(cards[1])},
                         {core::suit_of(cards[0]), core::suit_of(cards[1])}};
        }
        return value;
    }();
    return result;
}

std::uint64_t board_hash_prefix(core::Street street, const MultiwayBucketFeatures& features) noexcept {
    auto hash = core::fingerprint::FNV1A_OFFSET;
    core::fingerprint::append_u64(hash, static_cast<std::uint8_t>(street));
    core::fingerprint::append_u64(hash, features.board_rank_mask);
    core::fingerprint::append_u64(hash, features.board_suit_mask);
    core::fingerprint::append_u64(hash, features.board_pair_count);
    core::fingerprint::append_u64(hash, features.board_size);
    return hash;
}

void add_u64(std::uint64_t& hash, std::uint64_t value) noexcept { core::fingerprint::append_u64(hash, value); }

void serialize_tables(const std::vector<MultiwayBucketTable>& tables, std::vector<std::uint8_t>& payload) {
    payload.reserve(tables.size() * (MULTIWAY_HOLE_COMBINATION_COUNT * 4U + 16U));
    for (const auto& table : tables) {
        if (table.assignments().size() != MULTIWAY_HOLE_COMBINATION_COUNT || table.canonical_board().size() > 5U ||
            !is_multiway_canonical_board(table.street(), table.canonical_board())) {
            throw std::invalid_argument("invalid bucket table");
        }
        payload.push_back(static_cast<std::uint8_t>(table.street()));
        payload.push_back(static_cast<std::uint8_t>(table.canonical_board().size()));
        for (const auto card : table.canonical_board()) payload.push_back(card);
        append_u32(payload, table.bucket_count());
        for (const auto assignment : table.assignments()) append_u32(payload, assignment);
    }
}

void build_direct_serialized_chunk(
    const MultiwayModelIdentity& identity, const MultiwayBucketBaselineProfile& profile,
    std::uint64_t begin_index, std::uint64_t end_index, std::vector<std::uint8_t>& payload) {
    identity.validate(); profile.validate();
    const auto flop = multiway_bucket_board_count(core::Street::Flop);
    const auto turn = multiway_bucket_board_count(core::Street::Turn);
    const auto total = flop + turn + multiway_bucket_board_count(core::Street::River);
    if (begin_index > end_index || end_index > total) throw std::out_of_range("bucket chunk range exceeds catalog");
    constexpr std::size_t max_record_bytes = 2U + 5U + 4U + MULTIWAY_HOLE_COMBINATION_COUNT * 4U;
    const auto table_count = end_index - begin_index;
    if (table_count > ((std::numeric_limits<std::size_t>::max)() - payload.size()) / max_record_bytes) {
        throw std::length_error("bucket payload size estimate overflows");
    }
    if (table_count == 0U) return;
    const auto original_size = payload.size();
    payload.resize(original_size + static_cast<std::size_t>(table_count) * max_record_bytes);
    auto* output = payload.data() + original_size;
    std::uint64_t offset = 0U;
    for (const auto street : {core::Street::Flop, core::Street::Turn, core::Street::River}) {
        const MultiwayBucketBoardCatalog catalog(street);
        const auto street_end = offset + catalog.size();
        if (end_index <= offset) break;
        const auto local_begin = begin_index > offset ? begin_index - offset : 0U;
        const auto local_end = end_index < street_end ? end_index - offset : catalog.size();
        if (local_begin < local_end) catalog.for_each_fixed_board(local_begin, local_end,
            [&](const std::array<std::uint8_t, 5U>& board) {
                const auto board_size = street == core::Street::Flop ? 3U : street == core::Street::Turn ? 4U : 5U;
                const auto bucket_count = street == core::Street::Flop ? profile.flop_bucket_count : street == core::Street::Turn ? profile.turn_bucket_count : profile.river_bucket_count;
                *output++ = static_cast<std::uint8_t>(street); *output++ = static_cast<std::uint8_t>(board_size);
                for (std::size_t index = 0U; index < board_size; ++index) *output++ = board[index];
                write_u32(output, bucket_count);
                std::array<std::uint8_t, 15U> rank_counts{};
                std::array<std::uint8_t, 4U> suit_counts{};
                std::uint64_t dead_mask = 0U;
                std::uint16_t rank_mask = 0U;
                std::uint8_t suit_mask = 0U;
                for (std::size_t index = 0U; index < board_size; ++index) {
                    const auto rank = core::rank_of(board[index]); const auto suit = core::suit_of(board[index]);
                    dead_mask |= 1ULL << board[index]; rank_mask |= static_cast<std::uint16_t>(1U << rank);
                    suit_mask |= static_cast<std::uint8_t>(1U << suit); ++rank_counts[rank]; ++suit_counts[suit];
                }
                std::uint8_t pair_count = 0U;
                for (const auto count : rank_counts) if (count >= 2U) ++pair_count;
                MultiwayBucketFeatures board_features;
                board_features.board_rank_mask = rank_mask; board_features.board_suit_mask = suit_mask;
                board_features.board_pair_count = pair_count; board_features.board_size = static_cast<std::uint8_t>(board_size);
                const auto board_prefix = board_hash_prefix(street, board_features);
                for (CanonicalComboId id = 0U; id < MULTIWAY_HOLE_COMBINATION_COUNT; ++id) {
                    const auto& combo = combo_metadata()[id];
                    if ((dead_mask & ((1ULL << combo.cards[0]) | (1ULL << combo.cards[1]))) != 0U) { write_u32(output, MULTIWAY_INVALID_BUCKET); continue; }
                    MultiwayBucketFeatures features = board_features;
                    features.hole_high_rank = (std::max)(combo.ranks[0], combo.ranks[1]); features.hole_low_rank = (std::min)(combo.ranks[0], combo.ranks[1]);
                    features.hole_suited = combo.suits[0] == combo.suits[1] ? 1U : 0U;
                    features.hole_pairs_board = combo.ranks[0] == combo.ranks[1] ? rank_counts[combo.ranks[0]] : static_cast<std::uint8_t>(rank_counts[combo.ranks[0]] + rank_counts[combo.ranks[1]]);
                    features.hole_suit_matches_board = static_cast<std::uint8_t>(suit_counts[combo.suits[0]] + suit_counts[combo.suits[1]]);
                    auto hash = board_prefix; add_u64(hash, features.hole_high_rank); add_u64(hash, features.hole_low_rank); add_u64(hash, features.hole_suited); add_u64(hash, features.hole_pairs_board); add_u64(hash, features.hole_suit_matches_board);
                    write_u32(output, static_cast<std::uint32_t>((hash ^ (profile.feature_version * 0x9e3779b97f4a7c15ULL)) % bucket_count));
                }
                if (static_cast<std::size_t>(output - payload.data()) > payload.size()) throw std::logic_error("bucket payload cursor overflow");
            });
        offset = street_end;
    }
    payload.resize(static_cast<std::size_t>(output - payload.data()));
}

std::uint64_t estimate_multiway_bucket_generation_process_memory_bytes_impl(
    std::uint32_t requested_threads, std::uint32_t detected_threads,
    std::uint32_t chunk_size, std::uint32_t queue_capacity) {
    if (chunk_size == 0U) throw std::invalid_argument("bucket generation chunk size is zero");
    const auto threads = resolve_multiway_bucket_thread_count(requested_threads, detected_threads);
    if (threads == 0U) throw std::invalid_argument("bucket generation thread count is zero");
    std::uint32_t slots = queue_capacity;
    if (slots == 0U) {
        if (threads > (std::numeric_limits<std::uint32_t>::max)() / 2U) {
            throw std::length_error("bucket generation queue size overflows");
        }
        slots = (std::max)(2U, threads * 2U);
    } else {
        slots = (std::max)(2U, slots);
    }
    constexpr std::uint64_t max_record_bytes = 2U + 5U + 4U + MULTIWAY_HOLE_COMBINATION_COUNT * 4U;
    constexpr std::uint64_t fixed_bytes = 1U << 20U;
    if (slots > ((std::numeric_limits<std::uint64_t>::max)() - fixed_bytes) / (chunk_size * max_record_bytes + 128U) ||
        threads > ((std::numeric_limits<std::uint64_t>::max)() - fixed_bytes) / (1U << 20U)) {
        throw std::length_error("bucket generation memory estimate overflows");
    }
    return fixed_bytes + static_cast<std::uint64_t>(slots) * (chunk_size * max_record_bytes + 128U) + static_cast<std::uint64_t>(threads) * (1U << 20U);
}

}  // namespace

void build_multiway_baseline_direct_serialized_chunk(
    const MultiwayModelIdentity& identity, const MultiwayBucketBaselineProfile& profile,
    std::uint64_t begin_index, std::uint64_t end_index, std::vector<std::uint8_t>& payload) {
    build_direct_serialized_chunk(identity, profile, begin_index, end_index, payload);
}

std::uint64_t estimate_multiway_bucket_generation_process_memory_bytes(
    std::uint32_t requested_threads, std::uint32_t detected_threads,
    std::uint32_t chunk_size, std::uint32_t queue_capacity) {
    return estimate_multiway_bucket_generation_process_memory_bytes_impl(
        requested_threads, detected_threads, chunk_size, queue_capacity);
}

std::uint32_t multiway_bucket_hardware_thread_count() noexcept {
    const auto detected = std::thread::hardware_concurrency();
    return detected == 0U ? 1U : detected;
}

std::uint32_t multiway_bucket_physical_core_count() noexcept {
#if defined(_WIN32)
    DWORD bytes = 0U;
    GetLogicalProcessorInformationEx(RelationProcessorCore, nullptr, &bytes);
    if (bytes == 0U) return multiway_bucket_hardware_thread_count();
    std::vector<std::uint8_t> buffer(bytes);
    if (!GetLogicalProcessorInformationEx(
            RelationProcessorCore,
            reinterpret_cast<PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX>(buffer.data()), &bytes)) {
        return multiway_bucket_hardware_thread_count();
    }
    std::uint32_t cores = 0U;
    DWORD offset = 0U;
    while (offset < bytes) {
        const auto* entry = reinterpret_cast<const SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX*>(buffer.data() + offset);
        if (entry->Relationship == RelationProcessorCore) ++cores;
        if (entry->Size == 0U) break;
        offset += entry->Size;
    }
    return cores == 0U ? multiway_bucket_hardware_thread_count() : cores;
#else
    return multiway_bucket_hardware_thread_count();
#endif
}

std::uint32_t resolve_multiway_bucket_thread_count(
    std::uint32_t requested_threads, std::uint32_t detected_threads) noexcept {
    if (requested_threads == 0U) return 0U;
    const auto available = detected_threads == 0U ? 1U : detected_threads;
    return (std::min)(requested_threads, available);
}

void build_multiway_baseline_bucket_chunk(
    const MultiwayModelIdentity& identity,
    const MultiwayBucketBaselineProfile& profile,
    std::uint64_t begin_index,
    std::uint64_t end_index,
    std::vector<MultiwayBucketTable>& tables) {
    if (begin_index > end_index) throw std::invalid_argument("bucket chunk range is invalid");
    const auto flop_count = multiway_bucket_board_count(core::Street::Flop);
    const auto turn_count = multiway_bucket_board_count(core::Street::Turn);
    const auto river_count = multiway_bucket_board_count(core::Street::River);
    const auto total_count = flop_count + turn_count + river_count;
    if (end_index > total_count) throw std::out_of_range("bucket chunk range exceeds catalog");

    std::uint64_t global_offset = 0U;
    for (const auto street : {core::Street::Flop, core::Street::Turn, core::Street::River}) {
        const MultiwayBucketBoardCatalog catalog(street);
        const auto street_end = global_offset + catalog.size();
        if (end_index <= global_offset) break;
        const auto local_begin = begin_index > global_offset ? begin_index - global_offset : 0U;
        const auto local_end = end_index < street_end ? end_index - global_offset : catalog.size();
        if (local_begin < local_end) {
            catalog.for_each_fixed_board(local_begin, local_end, [&](const std::array<std::uint8_t, 5U>& board) {
                tables.push_back(build_multiway_baseline_bucket_table_fixed_board(
                    identity, street, board, profile));
            });
        }
        global_offset = street_end;
    }
}

void generate_multiway_bucket_chunks(
    std::uint64_t begin_index,
    std::uint64_t end_index,
    MultiwayBucketGenerationOptions options,
    MultiwayBucketChunkBuilder builder,
    MultiwayBucketChunkPublisher publisher,
    MultiwayBucketProgressCallback progress_callback) {
    if (begin_index > end_index) throw std::invalid_argument("bucket generation range is invalid");
    if (options.chunk_size == 0U) throw std::invalid_argument("bucket generation chunk size is zero");
    if (!builder || !publisher) throw std::invalid_argument("bucket generation callbacks are incomplete");
    const auto thread_count = resolve_multiway_bucket_thread_count(
        options.requested_threads, multiway_bucket_hardware_thread_count());
    if (thread_count == 0U) throw std::invalid_argument("bucket generation thread count is zero");
    const auto queue_capacity = options.queue_capacity == 0U
        ? std::max<std::uint32_t>(2U, thread_count * 2U)
        : std::max<std::uint32_t>(2U, options.queue_capacity);

    if (thread_count == 1U) {
        for (auto next = begin_index; next < end_index;) {
            const auto chunk_end = next + std::min<std::uint64_t>(
                end_index - next, options.chunk_size);
            std::vector<MultiwayBucketTable> tables;
            tables.reserve(static_cast<std::size_t>(chunk_end - next));
            builder(next, chunk_end, tables);
            if (tables.size() != static_cast<std::size_t>(chunk_end - next)) {
                throw std::runtime_error("bucket chunk builder returned an invalid table count");
            }
            publisher(next, std::move(tables));
            if (options.stats) {
                ++options.stats->chunks_built;
                ++options.stats->chunks_published;
            }
            next = chunk_end;
            if (progress_callback) {
                progress_callback({next - begin_index, end_index - begin_index});
            }
        }
        return;
    }

    SharedGenerationState state;
    state.next_job = begin_index;
    std::vector<std::thread> workers;
    workers.reserve(thread_count);
    const auto set_failure = [&state](std::exception_ptr failure) {
        std::lock_guard lock(state.mutex);
        if (!state.failure) state.failure = std::move(failure);
        state.condition.notify_all();
    };
    const auto worker = [&]() {
        while (true) {
            std::uint64_t job_begin = 0U;
            std::uint64_t job_end = 0U;
            {
                std::unique_lock lock(state.mutex);
                const auto wait_start = std::chrono::steady_clock::now();
                state.condition.wait(lock, [&] {
                    return state.failure || state.next_job >= end_index ||
                        state.in_flight < queue_capacity;
                });
                if (options.stats) options.stats->worker_wait_nanoseconds += static_cast<std::uint64_t>(
                    std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - wait_start).count());
                if (state.failure || state.next_job >= end_index) return;
                job_begin = state.next_job;
                const auto remaining = end_index - job_begin;
                job_end = job_begin + std::min<std::uint64_t>(remaining, options.chunk_size);
                state.next_job = job_end;
                ++state.in_flight;
            }

            try {
                GenerationChunk chunk;
                chunk.begin_index = job_begin;
                chunk.tables.reserve(static_cast<std::size_t>(job_end - job_begin));
                builder(job_begin, job_end, chunk.tables);
                if (chunk.tables.size() != static_cast<std::size_t>(job_end - job_begin)) {
                    throw std::runtime_error("bucket chunk builder returned an invalid table count");
                }
                {
                    std::lock_guard lock(state.mutex);
                    if (!state.failure) state.ready.emplace(chunk.begin_index, std::move(chunk));
                    else --state.in_flight;
                    if (options.stats) {
                        ++options.stats->chunks_built;
                        options.stats->ready_queue_high_watermark = std::max<std::uint64_t>(
                            options.stats->ready_queue_high_watermark, state.ready.size());
                    }
                }
                state.condition.notify_all();
            } catch (...) {
                {
                    std::lock_guard lock(state.mutex);
                    --state.in_flight;
                }
                set_failure(std::current_exception());
                return;
            }
        }
    };
    try {
        for (std::uint32_t index = 0U; index < thread_count; ++index) {
            workers.emplace_back(worker);
        }
    } catch (...) {
        set_failure(std::current_exception());
        state.condition.notify_all();
        for (auto& thread : workers) thread.join();
        throw;
    }

    std::uint64_t next_publish = begin_index;
    try {
        while (next_publish < end_index) {
            GenerationChunk chunk;
            {
                std::unique_lock lock(state.mutex);
                const auto wait_start = std::chrono::steady_clock::now();
                state.condition.wait(lock, [&] {
                    return state.failure || state.ready.find(next_publish) != state.ready.end();
                });
                if (options.stats) options.stats->ordered_publish_wait_nanoseconds += static_cast<std::uint64_t>(
                    std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - wait_start).count());
                if (state.failure) std::rethrow_exception(state.failure);
                auto found = state.ready.find(next_publish);
                chunk = std::move(found->second);
                state.ready.erase(found);
                --state.in_flight;
                state.condition.notify_all();
            }
            publisher(chunk.begin_index, std::move(chunk.tables));
            if (options.stats) {
                std::lock_guard lock(state.mutex);
                ++options.stats->chunks_published;
            }
            next_publish += options.chunk_size;
            if (next_publish > end_index) next_publish = end_index;
            if (progress_callback) {
                progress_callback({next_publish - begin_index, end_index - begin_index});
            }
        }
    } catch (...) {
        set_failure(std::current_exception());
    }

    state.condition.notify_all();
    for (auto& thread : workers) thread.join();
    if (state.failure) std::rethrow_exception(state.failure);
}

void generate_multiway_bucket_serialized_chunks(
    std::uint64_t begin_index, std::uint64_t end_index,
    MultiwayBucketGenerationOptions options, MultiwayBucketChunkBuilder builder,
    MultiwayBucketSerializedChunkPublisher publisher,
    MultiwayBucketProgressCallback progress_callback) {
    if (begin_index > end_index || options.chunk_size == 0U || !builder || !publisher) {
        throw std::invalid_argument("invalid serialized bucket generation request");
    }
    const auto threads = resolve_multiway_bucket_thread_count(
        options.requested_threads, multiway_bucket_hardware_thread_count());
    if (threads == 0U) throw std::invalid_argument("bucket generation thread count is zero");
    const auto capacity = options.queue_capacity == 0U
        ? std::max<std::uint32_t>(2U, threads * 2U) : std::max<std::uint32_t>(2U, options.queue_capacity);
    struct Chunk { std::uint64_t begin = 0U; std::uint64_t count = 0U; std::vector<std::uint8_t> payload; };
    std::vector<Chunk> slots(capacity);
    std::vector<std::size_t> free_slots;
    free_slots.reserve(capacity);
    for (std::size_t index = 0U; index < capacity; ++index) free_slots.push_back(index);
    std::mutex mutex;
    std::condition_variable condition;
    std::map<std::uint64_t, std::size_t> ready;
    std::exception_ptr failure;
    std::uint64_t next_job = begin_index;
    std::uint32_t in_flight = 0U;
    const auto fail = [&](std::exception_ptr error) {
        std::lock_guard lock(mutex);
        if (!failure) failure = std::move(error);
        condition.notify_all();
    };
    const auto worker = [&]() {
        while (true) {
            std::uint64_t first = 0U, last = 0U;
            std::size_t slot_index = 0U;
            {
                std::unique_lock lock(mutex);
                const auto wait_start = std::chrono::steady_clock::now();
                condition.wait(lock, [&] { return failure || next_job >= end_index || !free_slots.empty(); });
                if (options.stats) options.stats->worker_wait_nanoseconds += static_cast<std::uint64_t>(
                    std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - wait_start).count());
                if (failure || next_job >= end_index) return;
                first = next_job; last = first + std::min<std::uint64_t>(end_index - first, options.chunk_size);
                next_job = last; ++in_flight;
                slot_index = free_slots.back(); free_slots.pop_back();
            }
            try {
                const auto active_start = std::chrono::steady_clock::now();
                auto& chunk = slots[slot_index];
                chunk.begin = first; chunk.count = last - first; chunk.payload.clear();
                if (options.direct_serialized_builder) {
                    options.direct_serialized_builder(first, last, chunk.payload);
                } else {
                    std::vector<MultiwayBucketTable> tables;
                    tables.reserve(static_cast<std::size_t>(last - first));
                    builder(first, last, tables);
                    if (tables.size() != static_cast<std::size_t>(last - first)) throw std::runtime_error("bucket chunk builder returned an invalid table count");
                    serialize_tables(tables, chunk.payload);
                }
                if (options.stats) {
                    std::lock_guard lock(mutex);
                    options.stats->worker_active_nanoseconds += static_cast<std::uint64_t>(
                        std::chrono::duration_cast<std::chrono::nanoseconds>(
                            std::chrono::steady_clock::now() - active_start).count());
                }
                {
                    std::lock_guard lock(mutex);
                    if (!failure) ready.emplace(first, slot_index); else { --in_flight; free_slots.push_back(slot_index); }
                }
                if (options.stats) {
                    std::lock_guard lock(mutex);
                    ++options.stats->chunks_built;
                    options.stats->ready_queue_high_watermark = std::max<std::uint64_t>(options.stats->ready_queue_high_watermark, ready.size());
                }
                condition.notify_all();
            } catch (...) {
                { std::lock_guard lock(mutex); --in_flight; free_slots.push_back(slot_index); }
                fail(std::current_exception()); return;
            }
        }
    };
    std::vector<std::thread> workers;
    workers.reserve(threads);
    for (std::uint32_t index = 0U; index < threads; ++index) workers.emplace_back(worker);
    std::uint64_t next_publish = begin_index;
    try {
        while (next_publish < end_index) {
            std::size_t slot_index = 0U;
            {
                std::unique_lock lock(mutex);
                const auto wait_start = std::chrono::steady_clock::now();
                condition.wait(lock, [&] { return failure || ready.find(next_publish) != ready.end(); });
                if (options.stats) options.stats->ordered_publish_wait_nanoseconds += static_cast<std::uint64_t>(
                    std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - wait_start).count());
                if (failure) std::rethrow_exception(failure);
                auto found = ready.find(next_publish);
                slot_index = found->second; ready.erase(found); --in_flight; condition.notify_all();
            }
            auto& chunk = slots[slot_index];
            publisher(chunk.begin, chunk.count, chunk.payload);
            chunk.payload.clear();
            { std::lock_guard lock(mutex); free_slots.push_back(slot_index); condition.notify_all(); }
            if (options.stats) { std::lock_guard lock(mutex); ++options.stats->chunks_published; }
            next_publish += chunk.count;
            if (progress_callback) progress_callback({next_publish - begin_index, end_index - begin_index});
        }
    } catch (...) { fail(std::current_exception()); }
    condition.notify_all();
    for (auto& thread : workers) thread.join();
    if (failure) std::rethrow_exception(failure);
}

}  // namespace texas::solver::multiway
