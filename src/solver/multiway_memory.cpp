#include "solver/multiway_memory.hpp"

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef PSAPI_VERSION
#define PSAPI_VERSION 2
#endif
#include <windows.h>
#include <psapi.h>
#elif defined(__linux__)
#include <fstream>
#include <unistd.h>
#endif

#include <limits>
#include <stdexcept>
#include <thread>

namespace texas::solver::multiway {
namespace {

std::uint64_t checked_multiply(std::uint64_t left, std::uint64_t right) noexcept {
    if (left != 0U && right > std::numeric_limits<std::uint64_t>::max() / left) {
        return std::numeric_limits<std::uint64_t>::max();
    }
    return left * right;
}

std::uint64_t checked_add(std::uint64_t left, std::uint64_t right) noexcept {
    if (right > std::numeric_limits<std::uint64_t>::max() - left) {
        return std::numeric_limits<std::uint64_t>::max();
    }
    return left + right;
}

}  // namespace

std::uint64_t observed_multiway_process_memory_bytes() noexcept {
#if defined(_WIN32)
    PROCESS_MEMORY_COUNTERS_EX counters{};
    counters.cb = sizeof(counters);
    if (K32GetProcessMemoryInfo(
            GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&counters), sizeof(counters)) == 0) {
        return 0U;
    }
    return static_cast<std::uint64_t>(counters.WorkingSetSize);
#elif defined(__linux__)
    std::ifstream input("/proc/self/statm");
    std::uint64_t ignored_pages = 0;
    std::uint64_t resident_pages = 0;
    if (!(input >> ignored_pages >> resident_pages)) return 0U;
    const auto page_size = sysconf(_SC_PAGESIZE);
    if (page_size <= 0 || resident_pages >
            std::numeric_limits<std::uint64_t>::max() / static_cast<std::uint64_t>(page_size)) {
        return 0U;
    }
    return resident_pages * static_cast<std::uint64_t>(page_size);
#else
    return 0U;
#endif
}

void MultiwayMemoryBudget::validate() const {
    if (warning_bytes == 0U || operating_bytes == 0U || reject_bytes == 0U ||
        warning_bytes > operating_bytes || operating_bytes >= reject_bytes) {
        throw std::invalid_argument("multiway memory budget requires ordered non-zero limits");
    }
}

MultiwayMemoryPreflight preflight_multiway_memory(
    const MultiwaySolverLimits& limits,
    MultiwayMemoryBudget budget) {
    return preflight_multiway_memory(limits, budget, MultiwayMemoryInputs{});
}

MultiwayMemoryPreflight preflight_multiway_memory(
    const MultiwaySolverLimits& limits,
    MultiwayMemoryBudget budget,
    const MultiwayMemoryInputs& inputs) {
    budget.validate();
    if (limits.worker_count != 0U && limits.max_worker_delta_entries >
        std::numeric_limits<std::size_t>::max() / static_cast<std::size_t>(limits.worker_count)) {
        MultiwayMemoryPreflight result;
        result.status = MultiwayMemoryStatus::Rejected;
        result.estimate.total_bytes = std::numeric_limits<std::uint64_t>::max();
        result.message = "worker-delta capacity overflows addressable memory";
        return result;
    }
    limits.validate();

    MultiwayMemoryPreflight result;
    // Public descriptors contain vectors. Reserve a conservative 1 KiB per
    // admitted state before its dynamically sized edge/history payload.
    result.estimate.public_state_bytes = checked_multiply(limits.max_public_states, 1024U);
    result.estimate.sparse_row_bytes = checked_multiply(
        limits.max_sparse_rows, static_cast<std::uint64_t>(sizeof(MultiwaySparseRowMetadata)));
    result.estimate.sparse_value_bytes = checked_multiply(
        limits.max_sparse_values, 2U * static_cast<std::uint64_t>(sizeof(double)));
    result.estimate.blueprint_index_bytes = inputs.blueprint_index_bytes;
    result.estimate.range_row_bytes = checked_add(
        checked_multiply(inputs.range_row_count, sizeof(std::vector<MultiwayWeightedHole>)),
        checked_multiply(
            checked_multiply(inputs.range_entry_count, sizeof(MultiwayWeightedHole)), 2U));
    result.estimate.future_bucket_cache_bytes = inputs.future_bucket_cache_bytes;
    result.estimate.off_tree_menu_bytes = checked_multiply(
        inputs.off_tree_menu_entries, sizeof(MultiwayActionDescriptor));
    result.estimate.continuation_scratch_bytes = checked_multiply(
        limits.worker_count, inputs.continuation_scratch_bytes_per_worker);
    result.estimate.worker_delta_bytes = checked_add(
        checked_multiply(
            checked_multiply(limits.worker_count, limits.max_worker_delta_entries),
            static_cast<std::uint64_t>(sizeof(MultiwayWorkerDelta))),
        checked_multiply(
            limits.worker_count,
            static_cast<std::uint64_t>(
                sizeof(MultiwayPrivateWorkerScratch) +
                sizeof(MultiwayWorkerDeltaStream) + sizeof(std::thread))));
    const auto aggregate_delta_entries = checked_multiply(
        limits.worker_count, limits.max_worker_delta_entries);
    result.estimate.merge_scratch_bytes = checked_multiply(
        aggregate_delta_entries,
        static_cast<std::uint64_t>(sizeof(MultiwayWorkerDelta) + 3U * sizeof(std::uint64_t)));
    result.estimate.export_bytes = checked_multiply(
        inputs.export_action_capacity,
        2U * static_cast<std::uint64_t>(sizeof(MultiwayRootActionProbability)));
    result.estimate.continuation_cache_bytes = inputs.continuation_cache_bytes;

    const auto root_stage = checked_add(
        checked_add(result.estimate.public_state_bytes, result.estimate.blueprint_index_bytes),
        checked_add(
            checked_add(result.estimate.range_row_bytes, result.estimate.future_bucket_cache_bytes),
            checked_add(result.estimate.off_tree_menu_bytes, result.estimate.export_bytes)));
    const auto row_stage = checked_add(
        result.estimate.sparse_row_bytes, result.estimate.sparse_value_bytes);
    const auto delta_stage = checked_add(
        checked_add(result.estimate.worker_delta_bytes, result.estimate.merge_scratch_bytes),
        result.estimate.continuation_scratch_bytes);
    result.estimate.mandatory_bytes = checked_add(checked_add(root_stage, row_stage), delta_stage);
    result.estimate.total_bytes = checked_add(
        result.estimate.mandatory_bytes, result.estimate.continuation_cache_bytes);

    if (root_stage > budget.operating_bytes || root_stage >= budget.reject_bytes) {
        result.status = MultiwayMemoryStatus::Rejected;
        result.message = "root admission exceeds memory limit";
        return result;
    }
    result.highest_admitted_stage = MultiwayMemoryAdmissionStage::Root;
    const auto through_rows = checked_add(root_stage, row_stage);
    if (through_rows > budget.operating_bytes || through_rows >= budget.reject_bytes) {
        result.status = MultiwayMemoryStatus::Rejected;
        result.estimate.admitted_bytes = root_stage;
        result.message = "sparse-row admission exceeds memory limit";
        return result;
    }
    result.highest_admitted_stage = MultiwayMemoryAdmissionStage::SparseRows;
    if (result.estimate.mandatory_bytes > budget.operating_bytes ||
        result.estimate.mandatory_bytes >= budget.reject_bytes) {
        result.status = MultiwayMemoryStatus::Rejected;
        result.estimate.admitted_bytes = through_rows;
        result.message = "worker-delta admission exceeds memory limit";
        return result;
    }
    result.highest_admitted_stage = MultiwayMemoryAdmissionStage::WorkerDeltas;
    result.estimate.admitted_bytes = result.estimate.mandatory_bytes;

    if (result.estimate.continuation_cache_bytes != 0U &&
        (result.estimate.total_bytes > budget.operating_bytes ||
         result.estimate.total_bytes >= budget.reject_bytes)) {
        result.status = MultiwayMemoryStatus::Warning;
        result.degraded = true;
        result.message = "optional continuation cache was not admitted";
        return result;
    }
    result.optional_continuation_cache_admitted =
        result.estimate.continuation_cache_bytes != 0U;
    if (result.optional_continuation_cache_admitted) {
        result.highest_admitted_stage = MultiwayMemoryAdmissionStage::OptionalContinuationCache;
        result.estimate.admitted_bytes = result.estimate.total_bytes;
    }

    if (result.estimate.admitted_bytes >= budget.warning_bytes) {
        result.status = MultiwayMemoryStatus::Warning;
        result.message = "estimated memory exceeds warning limit";
    }
    return result;
}

}  // namespace texas::solver::multiway
