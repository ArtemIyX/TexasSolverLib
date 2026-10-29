#pragma once

/** @file
 * Compile-time Tracy instrumentation aliases with no-op disabled fallbacks.
 */

#include <cstddef>

#if defined(TEXASSOLVER_TRACY_ENABLED) && TEXASSOLVER_TRACY_ENABLED

	#include <cstdint>
	#include <tracy/Tracy.hpp>

#endif

namespace texas::util
{

#if defined(TEXASSOLVER_TRACY_ENABLED) && TEXASSOLVER_TRACY_ENABLED

	/** Version string for the enabled Tracy instrumentation build. */
	inline constexpr const char* TEXASSOLVER_TRACY_VERSION = "0.14.1";

	/** Scoped zone marker, compiled away when Tracy is disabled. */
	#define TEXASSOLVER_TRACY_ZONE_SCOPED(name) ZoneScopedN(name)
	/** Named zone marker controlled by active. */
	#define TEXASSOLVER_TRACY_ZONE_NAMED(variable, name, active) ZoneNamedN(variable, name, active)
	/** Sets the current Tracy thread name. */
	#define TEXASSOLVER_TRACY_SET_THREAD_NAME(name) tracy::SetThreadName(name)
	/** Records an integer-valued Tracy plot sample. */
	#define TEXASSOLVER_TRACY_PLOT(name, value) TracyPlot(name, static_cast<std::int64_t>(value))
	/** Records a sized Tracy message. */
	#define TEXASSOLVER_TRACY_MESSAGE(name, size) TracyMessage(name, size)
	/** Marks a named Tracy frame. */
	#define TEXASSOLVER_TRACY_FRAME_MARK(name) FrameMarkNamed(name)

	#if defined(TEXASSOLVER_TRACY_MEMORY) && TEXASSOLVER_TRACY_MEMORY
	inline constexpr bool tracy_memory_events_enabled = true;

	/** Records an allocation when Tracy memory events are enabled. */
	inline void tracy_memory_alloc(const void* pointer, std::size_t bytes) noexcept
	{
		TracyAlloc(pointer, bytes);
	}

	/** Records a non-null deallocation when Tracy memory events are enabled. */
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

	/** Empty version string when Tracy instrumentation is disabled. */
	inline constexpr const char* TEXASSOLVER_TRACY_VERSION = "";

	/** No-op placeholder for a scoped zone marker. */
	#define TEXASSOLVER_TRACY_ZONE_SCOPED(name) (void)0
	/** No-op placeholder for a named zone marker. */
	#define TEXASSOLVER_TRACY_ZONE_NAMED(variable, name, active) (void)0
	/** No-op placeholder for setting a thread name. */
	#define TEXASSOLVER_TRACY_SET_THREAD_NAME(name) (void)0
	/** No-op placeholder for a plot sample. */
	#define TEXASSOLVER_TRACY_PLOT(name, value) (void)0
	/** No-op placeholder for a Tracy message. */
	#define TEXASSOLVER_TRACY_MESSAGE(name, size) (void)0
	/** No-op placeholder for a frame marker. */
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
