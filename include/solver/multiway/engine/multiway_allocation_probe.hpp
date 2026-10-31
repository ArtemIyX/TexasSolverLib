#pragma once

/** @file
 * Scoped allocation counters for worker and coordinator profiling.
 */

#include <cstddef>
#include <cstdint>

namespace texas::solver::multiway
{

	/** Allocation count and byte totals captured from a probe. */
#pragma pack(push, 8)
	struct MultiwayAllocationProbeSnapshot
	{
		std::uint64_t allocation_count = 0U;
		std::uint64_t allocated_bytes = 0U;
	};
#pragma pack(pop)

	/** Accumulates allocation events while installed as the active probe. */
	class MultiwayAllocationProbe
	{
	public:
		MultiwayAllocationProbe() noexcept = default;

		/** Clears both allocation counters. */
		void reset() noexcept
		{
			allocation_count_ = 0U;
			allocated_bytes_ = 0U;
		}

		/** Returns a value snapshot of the counters. */
		[[nodiscard]] MultiwayAllocationProbeSnapshot snapshot() const noexcept
		{
			return { allocation_count_, allocated_bytes_ };
		}

		/** Adds one allocation event to the counters. */
		void record(std::size_t bytes) noexcept;

	private:
		std::uint64_t allocation_count_ = 0U;
		std::uint64_t allocated_bytes_ = 0U;
	};

	/** Installs a probe for a lexical scope and restores the previous probe. */
	class MultiwayAllocationProbeScope
	{
	public:
		/** Installs probe as the current thread's allocation target. */
		explicit MultiwayAllocationProbeScope(MultiwayAllocationProbe& probe) noexcept;
		/** Restores the probe that was current before this scope. */
		~MultiwayAllocationProbeScope();

		MultiwayAllocationProbeScope(const MultiwayAllocationProbeScope&) = delete;
		MultiwayAllocationProbeScope& operator=(const MultiwayAllocationProbeScope&) = delete;

	private:
		MultiwayAllocationProbe* previous_ = nullptr;
	};

} // namespace texas::solver::multiway
