#pragma once

#include <cstddef>
#include <cstdint>

namespace texas::solver::multiway
{

	struct MultiwayAllocationProbeSnapshot
	{
		std::uint64_t allocation_count = 0U;
		std::uint64_t allocated_bytes = 0U;
	};

	class MultiwayAllocationProbe
	{
	public:
		MultiwayAllocationProbe() noexcept = default;

		void reset() noexcept
		{
			allocation_count_ = 0U;
			allocated_bytes_ = 0U;
		}

		[[nodiscard]] MultiwayAllocationProbeSnapshot snapshot() const noexcept
		{
			return { allocation_count_, allocated_bytes_ };
		}

		void record(std::size_t bytes) noexcept;

	private:
		std::uint64_t allocation_count_ = 0U;
		std::uint64_t allocated_bytes_ = 0U;
	};

	class MultiwayAllocationProbeScope
	{
	public:
		explicit MultiwayAllocationProbeScope(MultiwayAllocationProbe& probe) noexcept;
		~MultiwayAllocationProbeScope();

		MultiwayAllocationProbeScope(const MultiwayAllocationProbeScope&) = delete;
		MultiwayAllocationProbeScope& operator=(const MultiwayAllocationProbeScope&) = delete;

	private:
		MultiwayAllocationProbe* previous_ = nullptr;
	};

} // namespace texas::solver::multiway
