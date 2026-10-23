#include "solver/multiway/engine/multiway_allocation_probe.hpp"
#include "util/tracy.hpp"

#include <cstdlib>
#include <cstdint>
#include <limits>
#include <new>

#if defined(_WIN32)
	#include <malloc.h>
#endif

namespace texas::solver::multiway
{
	namespace
	{
		thread_local MultiwayAllocationProbe* active_probe = nullptr;
		thread_local bool tracy_memory_event_active = false;

		class TracyMemoryEventScope
		{
		public:
			explicit TracyMemoryEventScope(bool& active) noexcept
				: active_(active)
			{
				active_ = true;
			}

			~TracyMemoryEventScope()
			{
				active_ = false;
			}

			TracyMemoryEventScope(const TracyMemoryEventScope&) = delete;
			TracyMemoryEventScope& operator=(const TracyMemoryEventScope&) = delete;

		private:
			bool& active_;
		};
	}

	void MultiwayAllocationProbe::record(std::size_t bytes) noexcept
	{
		if (allocation_count_ != std::numeric_limits<std::uint64_t>::max())
			++allocation_count_;
		if (bytes > std::numeric_limits<std::uint64_t>::max() - allocated_bytes_)
			allocated_bytes_ = std::numeric_limits<std::uint64_t>::max();
		else
			allocated_bytes_ += static_cast<std::uint64_t>(bytes);
	}

	MultiwayAllocationProbeScope::MultiwayAllocationProbeScope(MultiwayAllocationProbe& probe) noexcept
		: previous_(active_probe)
	{
		active_probe = &probe;
	}

	MultiwayAllocationProbeScope::~MultiwayAllocationProbeScope()
	{
		active_probe = previous_;
	}

	void record_allocation(std::size_t bytes) noexcept
	{
		if (active_probe != nullptr)
			active_probe->record(bytes);
	}

	void record_tracy_allocation(void* memory, std::size_t bytes) noexcept
	{
		if (!texas::util::tracy_memory_events_enabled || memory == nullptr || tracy_memory_event_active)
			return;
		TracyMemoryEventScope scope(tracy_memory_event_active);
		texas::util::tracy_memory_alloc(memory, bytes);
	}

	void record_tracy_deallocation(void* memory) noexcept
	{
		if (!texas::util::tracy_memory_events_enabled || memory == nullptr || tracy_memory_event_active)
			return;
		TracyMemoryEventScope scope(tracy_memory_event_active);
		texas::util::tracy_memory_free(memory);
	}

	void record_successful_allocation(void* memory, std::size_t bytes) noexcept
	{
		record_allocation(bytes);
		record_tracy_allocation(memory, bytes);
	}

	void record_deallocation(void* memory) noexcept
	{
		record_tracy_deallocation(memory);
	}

	bool aligned_allocation_size(std::size_t bytes, std::size_t alignment, std::size_t& result) noexcept
	{
		if (alignment == 0U)
			return false;
		if (bytes == 0U)
		{
			result = alignment;
			return true;
		}
		const auto remainder = bytes % alignment;
		if (remainder == 0U)
		{
			result = bytes;
			return true;
		}
		const auto padding = alignment - remainder;
		if (bytes > std::numeric_limits<std::size_t>::max() - padding)
			return false;
		result = bytes + padding;
		return true;
	}

	void release_unaligned(void* memory) noexcept
	{
		record_deallocation(memory);
		std::free(memory);
	}

	void release_aligned(void* memory) noexcept
	{
		record_deallocation(memory);
#if defined(_WIN32)
		::_aligned_free(memory);
#else
		std::free(memory);
#endif
	}
} // namespace texas::solver::multiway

void* operator new(std::size_t bytes)
{
	if (void* memory = std::malloc(bytes == 0U ? 1U : bytes))
	{
		texas::solver::multiway::record_successful_allocation(memory, bytes);
		return memory;
	}
	throw std::bad_alloc();
}

void* operator new[](std::size_t bytes)
{
	return ::operator new(bytes);
}

void operator delete(void* memory) noexcept
{
	texas::solver::multiway::release_unaligned(memory);
}
void operator delete[](void* memory) noexcept
{
	texas::solver::multiway::release_unaligned(memory);
}
void operator delete(void* memory, std::size_t) noexcept
{
	texas::solver::multiway::release_unaligned(memory);
}
void operator delete[](void* memory, std::size_t) noexcept
{
	texas::solver::multiway::release_unaligned(memory);
}

void* operator new(std::size_t bytes, const std::nothrow_t&) noexcept
{
	try
	{
		return ::operator new(bytes);
	}
	catch (...)
	{
		return nullptr;
	}
}

void* operator new[](std::size_t bytes, const std::nothrow_t&) noexcept
{
	return ::operator new(bytes, std::nothrow);
}

void operator delete(void* memory, const std::nothrow_t&) noexcept
{
	texas::solver::multiway::release_unaligned(memory);
}
void operator delete[](void* memory, const std::nothrow_t&) noexcept
{
	texas::solver::multiway::release_unaligned(memory);
}

void* operator new(std::size_t bytes, std::align_val_t alignment)
{
	const auto alignment_value = static_cast<std::size_t>(alignment);
	std::size_t aligned_bytes = 0U;
	if (!texas::solver::multiway::aligned_allocation_size(bytes, alignment_value, aligned_bytes))
		throw std::bad_alloc();
#if defined(_WIN32)
	void* memory = ::_aligned_malloc(aligned_bytes, alignment_value);
#else
	void* memory = std::aligned_alloc(alignment_value, aligned_bytes);
#endif
	if (memory == nullptr)
		throw std::bad_alloc();
	texas::solver::multiway::record_successful_allocation(memory, bytes);
	return memory;
}

void* operator new[](std::size_t bytes, std::align_val_t alignment)
{
	return ::operator new(bytes, alignment);
}

void operator delete(void* memory, std::align_val_t) noexcept
{
	texas::solver::multiway::release_aligned(memory);
}
void operator delete[](void* memory, std::align_val_t alignment) noexcept
{
	::operator delete(memory, alignment);
}
void operator delete(void* memory, std::size_t, std::align_val_t alignment) noexcept
{
	::operator delete(memory, alignment);
}
void operator delete[](void* memory, std::size_t, std::align_val_t alignment) noexcept
{
	::operator delete(memory, alignment);
}

void* operator new(std::size_t bytes, std::align_val_t alignment, const std::nothrow_t&) noexcept
{
	try
	{
		return ::operator new(bytes, alignment);
	}
	catch (...)
	{
		return nullptr;
	}
}

void* operator new[](std::size_t bytes, std::align_val_t alignment, const std::nothrow_t&) noexcept
{
	return ::operator new(bytes, alignment, std::nothrow);
}

void operator delete(void* memory, std::align_val_t, const std::nothrow_t&) noexcept
{
	texas::solver::multiway::release_aligned(memory);
}

void operator delete[](void* memory, std::align_val_t alignment, const std::nothrow_t&) noexcept
{
	::operator delete(memory, alignment);
}
