#pragma once

/** @file
 * @brief Sampled HUNL solver timing, traversal, memory and export profile data.
 */

#include <cstddef>
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

	/** @brief Mutable sampled-solver counters with snapshot and text export. */
	class HUNLSampledProfile
	{
	public:
		/** @brief Clears counters and accumulated durations. */
		void reset() noexcept;
		/** @brief Adds traversal, visited-node and update counts. */
		void record_traversal(
			std::uint64_t traversals,
			std::uint64_t nodes_visited,
			std::uint64_t infosets_updated) noexcept;
		/** @brief Records current sparse row and value counts. */
		void record_sparse_storage(std::uint64_t sparse_rows, std::uint64_t sparse_values) noexcept;
		/** @brief Records memory-budget estimates and warning/rejection decisions. */
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
		/** @brief Records observed retained and peak memory. */
		void record_observed_memory(std::uint64_t retained_bytes, std::uint64_t peak_bytes) noexcept;
		/** @brief Adds elapsed traversal seconds. */
		void add_traverse_seconds(double seconds) noexcept;
		/** @brief Adds elapsed merge seconds. */
		void add_merge_seconds(double seconds) noexcept;
		/** @brief Adds elapsed terminal-evaluation seconds. */
		void add_terminal_seconds(double seconds) noexcept;
		/** @brief Adds elapsed export seconds. */
		void add_export_seconds(double seconds) noexcept;
		/** @brief Returns a const snapshot of the accumulated profile. */
		[[nodiscard]] const HUNLSampledProfileSnapshot& snapshot() const noexcept;
		/** @brief Formats a concise profile summary into caller storage. */
		[[nodiscard]] std::size_t format_summary(char* buffer, std::size_t buffer_size) const noexcept;

	private:
		HUNLSampledProfileSnapshot snapshot_;
	};

} // namespace texas::solver::hunl
