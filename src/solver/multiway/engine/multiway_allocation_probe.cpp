#include "solver/multiway/engine/multiway_allocation_probe.hpp"

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
} // namespace texas::solver::multiway

void* operator new(std::size_t bytes)
{
	if (void* memory = std::malloc(bytes == 0U ? 1U : bytes))
	{
		texas::solver::multiway::record_allocation(bytes);
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
	std::free(memory);
}
void operator delete[](void* memory) noexcept
{
	std::free(memory);
}
void operator delete(void* memory, std::size_t) noexcept
{
	std::free(memory);
}
void operator delete[](void* memory, std::size_t) noexcept
{
	std::free(memory);
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
	std::free(memory);
}
void operator delete[](void* memory, const std::nothrow_t&) noexcept
{
	std::free(memory);
}

void* operator new(std::size_t bytes, std::align_val_t alignment)
{
	const auto alignment_value = static_cast<std::size_t>(alignment);
	const auto aligned_bytes = bytes == 0U ? alignment_value : ((bytes + alignment_value - 1U) / alignment_value) * alignment_value;
#if defined(_WIN32)
	void* memory = ::_aligned_malloc(aligned_bytes, alignment_value);
#else
	void* memory = std::aligned_alloc(alignment_value, aligned_bytes);
#endif
	if (memory == nullptr)
		throw std::bad_alloc();
	texas::solver::multiway::record_allocation(bytes);
	return memory;
}

void* operator new[](std::size_t bytes, std::align_val_t alignment)
{
	return ::operator new(bytes, alignment);
}

void operator delete(void* memory, std::align_val_t) noexcept
{
#if defined(_WIN32)
	::_aligned_free(memory);
#else
	std::free(memory);
#endif
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
#if defined(_WIN32)
	::_aligned_free(memory);
#else
	std::free(memory);
#endif
}

void operator delete[](void* memory, std::align_val_t alignment, const std::nothrow_t&) noexcept
{
	::operator delete(memory, alignment);
}
