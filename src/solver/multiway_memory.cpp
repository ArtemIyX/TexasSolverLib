#include "solver/multiway_memory.hpp"

#include <limits>
#include <stdexcept>

namespace core {
namespace {

std::uint64_t checked_multiply(std::uint64_t left, std::uint64_t right) {
    if (left != 0U && right > std::numeric_limits<std::uint64_t>::max() / left) {
        throw std::overflow_error("multiway memory estimate overflows uint64");
    }
    return left * right;
}

std::uint64_t checked_add(std::uint64_t left, std::uint64_t right) {
    if (right > std::numeric_limits<std::uint64_t>::max() - left) {
        throw std::overflow_error("multiway memory estimate overflows uint64");
    }
    return left + right;
}

}  // namespace

void MultiwayMemoryBudget::validate() const {
    if (warning_bytes == 0U || reject_bytes == 0U || warning_bytes > reject_bytes) {
        throw std::invalid_argument("multiway memory budget requires ordered non-zero limits");
    }
}

MultiwayMemoryPreflight preflight_multiway_memory(
    const MultiwaySolverLimits& limits,
    MultiwayMemoryBudget budget) {
    limits.validate();
    budget.validate();

    MultiwayMemoryPreflight result;
    // Public descriptors contain vectors. Reserve a conservative 1 KiB per
    // admitted state before its dynamically sized edge/history payload.
    result.estimate.public_state_bytes = checked_multiply(limits.max_public_states, 1024U);
    result.estimate.sparse_row_bytes = checked_multiply(
        limits.max_sparse_rows, static_cast<std::uint64_t>(sizeof(MultiwaySparseRowMetadata)));
    result.estimate.sparse_value_bytes = checked_multiply(
        limits.max_sparse_values, 2U * static_cast<std::uint64_t>(sizeof(double)));
    result.estimate.worker_delta_bytes = checked_multiply(
        checked_multiply(limits.worker_count, limits.max_worker_delta_entries),
        static_cast<std::uint64_t>(sizeof(MultiwayWorkerDelta)));
    result.estimate.total_bytes = checked_add(
        checked_add(result.estimate.public_state_bytes, result.estimate.sparse_row_bytes),
        checked_add(result.estimate.sparse_value_bytes, result.estimate.worker_delta_bytes));

    if (result.estimate.total_bytes > budget.reject_bytes) {
        result.status = MultiwayMemoryStatus::Rejected;
        result.message = "estimated memory exceeds hard limit";
    } else if (result.estimate.total_bytes >= budget.warning_bytes) {
        result.status = MultiwayMemoryStatus::Warning;
        result.message = "estimated memory exceeds warning limit";
    }
    return result;
}

}  // namespace core
