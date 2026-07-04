#pragma once

#include <cstddef>
#include <cstdint>

namespace core {

struct HUNLSampledProfileSnapshot {
    double time_traverse_seconds = 0.0;
    double time_merge_seconds = 0.0;
    double time_terminal_seconds = 0.0;
    double time_export_seconds = 0.0;
    std::uint64_t traversals = 0;
    std::uint64_t nodes_visited = 0;
    std::uint64_t infosets_updated = 0;
    std::uint64_t sparse_rows = 0;
    std::uint64_t sparse_values = 0;
    std::uint64_t public_states_cached = 0;
    std::uint64_t infoset_rows_allocated = 0;
    std::uint64_t sparse_values_allocated = 0;
    std::uint64_t terminal_cache_bytes = 0;
    std::uint64_t worker_delta_bytes = 0;
    std::uint64_t export_bytes = 0;
    std::uint64_t total_memory_bytes = 0;
    bool memory_warning = false;
    bool memory_rejected = false;
};

class HUNLSampledProfile {
public:
    void reset() noexcept;
    void record_traversal(
        std::uint64_t traversals,
        std::uint64_t nodes_visited,
        std::uint64_t infosets_updated) noexcept;
    void record_sparse_storage(std::uint64_t sparse_rows, std::uint64_t sparse_values) noexcept;
    void record_memory_budget(
        std::uint64_t public_states_cached,
        std::uint64_t infoset_rows_allocated,
        std::uint64_t sparse_values_allocated,
        std::uint64_t terminal_cache_bytes,
        std::uint64_t worker_delta_bytes,
        std::uint64_t export_bytes,
        std::uint64_t total_memory_bytes,
        bool warning,
        bool rejected) noexcept;
    void add_traverse_seconds(double seconds) noexcept;
    void add_merge_seconds(double seconds) noexcept;
    void add_terminal_seconds(double seconds) noexcept;
    void add_export_seconds(double seconds) noexcept;
    [[nodiscard]] const HUNLSampledProfileSnapshot& snapshot() const noexcept;
    [[nodiscard]] std::size_t format_summary(char* buffer, std::size_t buffer_size) const noexcept;

private:
    HUNLSampledProfileSnapshot snapshot_;
};

}  // namespace core
