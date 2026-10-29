#pragma once

/** @file
 * @brief Internal terminal-output and progress-rendering interface for logging.
 */

#include "logging/config.hpp"

#include <cstdint>
#include <iosfwd>
#include <string>
#include <string_view>
#include <functional>
#include <chrono>

namespace texas::logging::detail
{
	/** @brief Configures terminal output and progress rendering dependencies. */
	void configure_terminal(std::ostream* output, bool interactive, const ProgressConfig& config,
		const std::function<std::chrono::steady_clock::time_point()>& now) noexcept;
	/** @brief Resets terminal rendering state. */
	void reset_terminal() noexcept;
	/** @brief Writes a console message with the configured severity and color behavior. */
	void write_console(std::string_view text, Level level, bool color) noexcept;
	/** @brief Starts a progress record and returns its identifier. */
	std::uint64_t begin_progress(std::string_view label, std::uint64_t total) noexcept;
	/** @brief Updates completed units for a progress record. */
	void update_progress(std::uint64_t id, std::uint64_t completed) noexcept;
	/** @brief Adds or replaces a text field on a progress record. */
	void add_progress_field(std::uint64_t id, std::string_view name, std::string_view value) noexcept;
	/** @brief Sets the displayed work rate for a progress record. */
	void set_progress_rate(std::uint64_t id, double value) noexcept;
	/** @brief Finishes and removes a progress record. */
	void finish_progress(std::uint64_t id, std::string_view message) noexcept;
} // namespace texas::logging::detail
