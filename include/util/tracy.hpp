#pragma once

#if defined(TEXASSOLVER_TRACY_ENABLED) && TEXASSOLVER_TRACY_ENABLED

#include <tracy/Tracy.hpp>

#endif

namespace texas::util
{

#if defined(TEXASSOLVER_TRACY_ENABLED) && TEXASSOLVER_TRACY_ENABLED

	inline constexpr const char* TEXASSOLVER_TRACY_VERSION = "0.14.2";

#define TEXASSOLVER_TRACY_ZONE_SCOPED(name) ZoneScopedN(name)
#define TEXASSOLVER_TRACY_ZONE_NAMED(variable, name, active) ZoneNamedN(variable, name, active)
#define TEXASSOLVER_TRACY_SET_THREAD_NAME(name) tracy::SetThreadName(name)
#define TEXASSOLVER_TRACY_PLOT(name, value) TracyPlot(name, value)
#define TEXASSOLVER_TRACY_MESSAGE(name, size) TracyMessage(name, size)
#define TEXASSOLVER_TRACY_FRAME_MARK(name) FrameMarkNamed(name)

#else

	inline constexpr const char* TEXASSOLVER_TRACY_VERSION = "";

#define TEXASSOLVER_TRACY_ZONE_SCOPED(name) (void)0
#define TEXASSOLVER_TRACY_ZONE_NAMED(variable, name, active) (void)0
#define TEXASSOLVER_TRACY_SET_THREAD_NAME(name) (void)0
#define TEXASSOLVER_TRACY_PLOT(name, value) (void)0
#define TEXASSOLVER_TRACY_MESSAGE(name, size) (void)0
#define TEXASSOLVER_TRACY_FRAME_MARK(name) (void)0

#endif

} // namespace texas::util
