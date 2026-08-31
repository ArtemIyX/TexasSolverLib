#include "solver/multiway/abstraction/multiway_bucket_generation.hpp"

#include "solver/multiway/abstraction/multiway_bucket_catalog.hpp"

#include <algorithm>
#include <array>
#include <condition_variable>
#include <exception>
#include <map>
#include <mutex>
#include <stdexcept>
#include <thread>

namespace texas::solver::multiway {
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

}  // namespace

std::uint32_t multiway_bucket_hardware_thread_count() noexcept {
    const auto detected = std::thread::hardware_concurrency();
    return detected == 0U ? 1U : detected;
}

std::uint32_t resolve_multiway_bucket_thread_count(
    std::uint32_t requested_threads, std::uint32_t detected_threads) noexcept {
    if (requested_threads == 0U) return 0U;
    const auto available = detected_threads == 0U ? 1U : detected_threads;
    return std::min(requested_threads, available);
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
            catalog.for_each(local_begin, local_end, [&](const MultiwayBucketBoardRequest& request) {
                std::array<std::uint8_t, 5U> board{};
                std::copy(request.canonical_board.begin(), request.canonical_board.end(), board.begin());
                tables.push_back(build_multiway_baseline_bucket_table_fixed_board(
                    identity, request.street, board, profile));
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
                state.condition.wait(lock, [&] {
                    return state.failure || state.next_job >= end_index ||
                        state.in_flight < queue_capacity;
                });
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
                state.condition.wait(lock, [&] {
                    return state.failure || state.ready.find(next_publish) != state.ready.end();
                });
                if (state.failure) std::rethrow_exception(state.failure);
                auto found = state.ready.find(next_publish);
                chunk = std::move(found->second);
                state.ready.erase(found);
                --state.in_flight;
                state.condition.notify_all();
            }
            publisher(chunk.begin_index, std::move(chunk.tables));
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

}  // namespace texas::solver::multiway
