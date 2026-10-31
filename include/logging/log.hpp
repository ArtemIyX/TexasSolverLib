#pragma once

/** @file
 * @brief Logging initialization, category-filtered writes and convenience macros.
 */

#include "logging/config.hpp"

#include <string>
#include <string_view>
#include <utility>
#include <functional>
#include <chrono>
#include <iosfwd>

#ifndef TEXASSOLVER_ENABLE_LOGGING
	#define TEXASSOLVER_ENABLE_LOGGING 1
#endif

#if TEXASSOLVER_ENABLE_LOGGING
	#include <spdlog/common.h>
	#include <spdlog/fmt/fmt.h>
#endif

namespace texas::logging
{
	/** @brief Result of initializing the logging runtime. */
#pragma pack(push, 8)
	struct InitializeResult
	{
		bool ok = true;		 ///< Whether initialization succeeded.
		std::string message; ///< Failure detail when initialization fails.
	};
#pragma pack(pop)
	/** @brief Runtime resources and injectable behavior used by logging initialization. */
#pragma pack(push, 8)
	struct RuntimeDependencies
	{
		std::ostream* console_stream = nullptr;						///< Console destination, or null for the default stream.
		bool terminal_known = false;								///< Whether terminal capabilities are known.
		bool interactive_terminal = false;							///< Whether the output is interactive.
		std::function<std::chrono::steady_clock::time_point()> now; ///< Clock source used by progress display.
		bool fail_file_sink = false;								///< Test hook that forces file-sink creation to fail.
	};
#pragma pack(pop)
	/** @brief RAII lifetime for a configured logging runtime. */
	class LoggingSession
	{
	public:
		/** @brief Initializes logging for this session. */
		explicit LoggingSession(const Config& config) noexcept;
		/** @brief Flushes and shuts down the session's logging runtime. */
		~LoggingSession();
		LoggingSession(const LoggingSession&) = delete;
		LoggingSession& operator=(const LoggingSession&) = delete;
		/** @brief Reports whether initialization succeeded. */
		bool ok() const noexcept { return ok_; }
		/** @brief Returns initialization failure detail, or an empty string on success. */
		const std::string& message() const noexcept { return message_; }

	private:
		bool ok_ = false;
		std::string message_;
	};
	/** @brief Initializes the global logging runtime from config and dependencies. */
	InitializeResult initialize(const Config& config, const RuntimeDependencies& dependencies = {}) noexcept;
	/** @brief Flushes active logging sinks. */
	void flush() noexcept;
	/** @brief Shuts down the global logging runtime. */
	void shutdown() noexcept;
	/** @brief Reports whether a category and level pass current filters. */
	bool should_log(Category category, Level level) noexcept;
	/** @brief Writes unformatted text with source location metadata. */
	void write_text(Category category, Level level, std::string_view file, int line, std::string_view text) noexcept;
	/** @brief Configures the dedicated training-audit output path. */
	bool configure_training_audit(const std::filesystem::path& path) noexcept;
	/** @brief Writes one training-audit record to the configured destination. */
	void write_training_audit(std::string_view text) noexcept;

#if TEXASSOLVER_ENABLE_LOGGING
	/** @brief Formats and writes a log message; formatting failures are suppressed. */
	template <class... Args>
	void write(Category category, Level level, const char* file, int line,
		spdlog::format_string_t<Args...> format, Args&&... args) noexcept
	{
		try
		{
			write_text(category, level, file, line, fmt::format(format, std::forward<Args>(args)...));
		}
		catch (...)
		{
		}
	}
#endif
} // namespace texas::logging

#if TEXASSOLVER_ENABLE_LOGGING
	/** @brief Checks severity/category filters, then emits a formatted message. */
	#define TEXAS_LOG_IMPL(level, category, ...)                                                                                                \
		do                                                                                                                                      \
		{                                                                                                                                       \
			if (::texas::logging::should_log(::texas::logging::Category::category, ::texas::logging::Level::level))                             \
				::texas::logging::write(::texas::logging::Category::category, ::texas::logging::Level::level, __FILE__, __LINE__, __VA_ARGS__); \
		}                                                                                                                                       \
		while (false)
#else
	#define TEXAS_LOG_IMPL(level, category, ...) ((void)0)
#endif
#define TEXAS_LOG_TRACE(category, ...) TEXAS_LOG_IMPL(Trace, category, __VA_ARGS__)
#define TEXAS_LOG_DEBUG(category, ...) TEXAS_LOG_IMPL(Debug, category, __VA_ARGS__)
#define TEXAS_LOG_INFO(category, ...) TEXAS_LOG_IMPL(Info, category, __VA_ARGS__)
#define TEXAS_LOG_WARNING(category, ...) TEXAS_LOG_IMPL(Warning, category, __VA_ARGS__)
#define TEXAS_LOG_ERROR(category, ...) TEXAS_LOG_IMPL(Error, category, __VA_ARGS__)
#define TEXAS_LOG_CRITICAL(category, ...) TEXAS_LOG_IMPL(Critical, category, __VA_ARGS__)
