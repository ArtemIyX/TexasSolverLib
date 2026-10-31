#pragma once

/** @file
 * @brief Copyable timing, traversal, memory and export profile data.
 */

#include <cstdint>

namespace texas::solver::hunl
{
	/** @brief Copyable counters and timing values captured from a sampled profile. */
	struct HUNLSampledProfileSnapshot
	{
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
		std::uint64_t observed_retained_bytes = 0;
		std::uint64_t observed_peak_bytes = 0;
		bool memory_warning = false;
		bool memory_rejected = false;
	};
} // namespace texas::solver::hunl
