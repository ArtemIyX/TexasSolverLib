#pragma once

/** @file
 * Optional named timers and output for search profiling.
 */

#include <cstdint>
#include <string_view>

namespace texas::util::profiling
{

	/** Reports whether basic profiling is active. */
	bool enabled() noexcept;
	/** Reports whether detailed per-scope profiling is active. */
	bool detail_enabled() noexcept;
	/** Records elapsed seconds for a named profiling counter. */
	void mark(std::string_view name, double seconds) noexcept;
	/** Records elapsed seconds and call count for one profiling counter. */
	void mark_with_calls(std::string_view name, double seconds, std::uint64_t calls) noexcept;
	/** Prints accumulated profiling counters to the configured output. */
	void print_profiler_report();

	/** Adds elapsed scope time to a named profile counter on destruction. */
	class ScopedTimer
	{
	public:
		/** Starts timing name when the selected profiler is active. */
		explicit ScopedTimer(std::string_view name) noexcept;
		/** Records the elapsed time when the scope exits. */
		~ScopedTimer();

		ScopedTimer(const ScopedTimer&) = delete;
		ScopedTimer& operator=(const ScopedTimer&) = delete;

	private:
		std::string_view name_;
		std::uint64_t start_ns_ = 0;
	};

	/** Writes the accumulated profiler report to its configured destination. */
	void write_report();

} // namespace texas::util::profiling

#define TEXASSOLVER_PROFILE_SCOPE(name_literal) \
	TEXASSOLVER_PROFILE_SCOPE_IMPL(name_literal, __COUNTER__)

#define TEXASSOLVER_PROFILE_SCOPE_IMPL(name_literal, counter)                     \
	::texas::util::profiling::ScopedTimer TEXASSOLVER_PROFILE_SCOPE_NAME(counter) \
	{                                                                             \
		name_literal                                                              \
	}

#define TEXASSOLVER_PROFILE_SCOPE_NAME(counter) TEXASSOLVER_PROFILE_SCOPE_NAME_IMPL(counter)
#define TEXASSOLVER_PROFILE_SCOPE_NAME_IMPL(counter) _profile_scope_##counter
