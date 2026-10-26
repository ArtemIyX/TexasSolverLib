#pragma once

#include "logging/config.hpp"

#include <cstdint>
#include <iosfwd>
#include <string>
#include <string_view>
#include <functional>
#include <chrono>

namespace texas::logging::detail
{
	void configure_terminal(std::ostream* output, bool interactive, const ProgressConfig& config,
		const std::function<std::chrono::steady_clock::time_point()>& now) noexcept;
	void reset_terminal() noexcept;
	void write_console(std::string_view text, Level level, bool color) noexcept;
	std::uint64_t begin_progress(std::string_view label, std::uint64_t total) noexcept;
	void update_progress(std::uint64_t id, std::uint64_t completed) noexcept;
	void add_progress_field(std::uint64_t id, std::string_view name, std::string_view value) noexcept;
	void set_progress_rate(std::uint64_t id, double value) noexcept;
	void finish_progress(std::uint64_t id, std::string_view message) noexcept;
}
