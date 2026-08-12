#include "solver/hunl_sampled_profile.hpp"

#include <cstdio>
#include <algorithm>

namespace texas::solver::hunl {

void HUNLSampledProfile::reset() noexcept {
    snapshot_ = {};
}

void HUNLSampledProfile::record_traversal(
    std::uint64_t traversals,
    std::uint64_t nodes_visited,
    std::uint64_t infosets_updated) noexcept {
    snapshot_.traversals += traversals;
    snapshot_.nodes_visited += nodes_visited;
    snapshot_.infosets_updated += infosets_updated;
}

void HUNLSampledProfile::record_sparse_storage(
    std::uint64_t sparse_rows,
    std::uint64_t sparse_values) noexcept {
    snapshot_.sparse_rows = sparse_rows;
    snapshot_.sparse_values = sparse_values;
}

void HUNLSampledProfile::record_memory_budget(
    std::uint64_t public_states_cached,
    std::uint64_t infoset_rows_allocated,
    std::uint64_t sparse_values_allocated,
    std::uint64_t terminal_cache_bytes,
    std::uint64_t worker_delta_bytes,
    std::uint64_t export_bytes,
    std::uint64_t total_memory_bytes,
    bool warning,
    bool rejected) noexcept {
    snapshot_.public_states_cached = public_states_cached;
    snapshot_.infoset_rows_allocated = infoset_rows_allocated;
    snapshot_.sparse_values_allocated = sparse_values_allocated;
    snapshot_.terminal_cache_bytes = terminal_cache_bytes;
    snapshot_.worker_delta_bytes = worker_delta_bytes;
    snapshot_.export_bytes = export_bytes;
    snapshot_.total_memory_bytes = total_memory_bytes;
    snapshot_.memory_warning = warning;
    snapshot_.memory_rejected = rejected;
}

void HUNLSampledProfile::record_observed_memory(
    std::uint64_t retained_bytes, std::uint64_t peak_bytes) noexcept {
    snapshot_.observed_retained_bytes = retained_bytes;
    snapshot_.observed_peak_bytes = std::max(snapshot_.observed_peak_bytes, peak_bytes);
}

void HUNLSampledProfile::add_traverse_seconds(double seconds) noexcept {
    snapshot_.time_traverse_seconds += seconds;
}

void HUNLSampledProfile::add_merge_seconds(double seconds) noexcept {
    snapshot_.time_merge_seconds += seconds;
}

void HUNLSampledProfile::add_terminal_seconds(double seconds) noexcept {
    snapshot_.time_terminal_seconds += seconds;
}

void HUNLSampledProfile::add_export_seconds(double seconds) noexcept {
    snapshot_.time_export_seconds += seconds;
}

const HUNLSampledProfileSnapshot& HUNLSampledProfile::snapshot() const noexcept {
    return snapshot_;
}

std::size_t HUNLSampledProfile::format_summary(char* buffer, std::size_t buffer_size) const noexcept {
    if (buffer == nullptr || buffer_size == 0) {
        return 0;
    }

    const auto written = std::snprintf(
        buffer,
        buffer_size,
        "traversals=%llu nodes=%llu infosets=%llu sparse_rows=%llu sparse_values=%llu "
        "mem_total=%llu mem_export=%llu t_traverse=%.6f t_merge=%.6f t_terminal=%.6f t_export=%.6f",
        static_cast<unsigned long long>(snapshot_.traversals),
        static_cast<unsigned long long>(snapshot_.nodes_visited),
        static_cast<unsigned long long>(snapshot_.infosets_updated),
        static_cast<unsigned long long>(snapshot_.sparse_rows),
        static_cast<unsigned long long>(snapshot_.sparse_values),
        static_cast<unsigned long long>(snapshot_.total_memory_bytes),
        static_cast<unsigned long long>(snapshot_.export_bytes),
        snapshot_.time_traverse_seconds,
        snapshot_.time_merge_seconds,
        snapshot_.time_terminal_seconds,
        snapshot_.time_export_seconds);

    if (written < 0) {
        buffer[0] = '\0';
        return 0;
    }

    const auto count = static_cast<std::size_t>(written);
    return count < buffer_size ? count : buffer_size - 1;
}

}  // namespace texas::solver::hunl
