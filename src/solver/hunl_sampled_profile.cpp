#include "solver/hunl_sampled_profile.hpp"

#include <cstdio>

namespace core {

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
        "t_traverse=%.6f t_merge=%.6f t_terminal=%.6f t_export=%.6f",
        static_cast<unsigned long long>(snapshot_.traversals),
        static_cast<unsigned long long>(snapshot_.nodes_visited),
        static_cast<unsigned long long>(snapshot_.infosets_updated),
        static_cast<unsigned long long>(snapshot_.sparse_rows),
        static_cast<unsigned long long>(snapshot_.sparse_values),
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

}  // namespace core
