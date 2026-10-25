#pragma once

#include <cstddef>

#if defined(TEXASSOLVER_TRACY_ENABLED) && TEXASSOLVER_TRACY_ENABLED

	#include <cstdint>
	#include <tracy/Tracy.hpp>

#endif

namespace texas::util
{

#if defined(TEXASSOLVER_TRACY_ENABLED) && TEXASSOLVER_TRACY_ENABLED

	inline constexpr const char* TEXASSOLVER_TRACY_VERSION = "0.14.1";

	#define TEXASSOLVER_TRACY_ZONE_SCOPED(name) ZoneScopedN(name)
	#define TEXASSOLVER_TRACY_ZONE_NAMED(variable, name, active) ZoneNamedN(variable, name, active)
	#define TEXASSOLVER_TRACY_SET_THREAD_NAME(name) tracy::SetThreadName(name)
	#define TEXASSOLVER_TRACY_PLOT(name, value) TracyPlot(name, static_cast<std::int64_t>(value))
	#define TEXASSOLVER_TRACY_MESSAGE(name, size) TracyMessage(name, size)
	#define TEXASSOLVER_TRACY_FRAME_MARK(name) FrameMarkNamed(name)

	#if defined(TEXASSOLVER_TRACY_MEMORY) && TEXASSOLVER_TRACY_MEMORY
	inline constexpr bool tracy_memory_events_enabled = true;

	inline void tracy_memory_alloc(const void* pointer, std::size_t bytes) noexcept
	{
		TracyAlloc(pointer, bytes);
	}

	inline void tracy_memory_free(const void* pointer) noexcept
	{
		if (pointer != nullptr)
			TracyFree(pointer);
	}
	#else
	inline constexpr bool tracy_memory_events_enabled = false;

	inline void tracy_memory_alloc(const void*, std::size_t) noexcept
	{
	}

	inline void tracy_memory_free(const void*) noexcept
	{
	}
	#endif

#else

	inline constexpr const char* TEXASSOLVER_TRACY_VERSION = "";

	#define TEXASSOLVER_TRACY_ZONE_SCOPED(name) (void)0
	#define TEXASSOLVER_TRACY_ZONE_NAMED(variable, name, active) (void)0
	#define TEXASSOLVER_TRACY_SET_THREAD_NAME(name) (void)0
	#define TEXASSOLVER_TRACY_PLOT(name, value) (void)0
	#define TEXASSOLVER_TRACY_MESSAGE(name, size) (void)0
	#define TEXASSOLVER_TRACY_FRAME_MARK(name) (void)0

	inline constexpr bool tracy_memory_events_enabled = false;

	inline void tracy_memory_alloc(const void*, std::size_t) noexcept
	{
	}

	inline void tracy_memory_free(const void*) noexcept
	{
	}

#endif

} // namespace texas::util
