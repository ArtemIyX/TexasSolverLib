#pragma once

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
	struct InitializeResult
	{
		bool ok = true;
		std::string message;
	};
	struct RuntimeDependencies
	{
		std::ostream* console_stream = nullptr;
		bool terminal_known = false;
		bool interactive_terminal = false;
		std::function<std::chrono::steady_clock::time_point()> now;
		bool fail_file_sink = false;
	};
	class LoggingSession
	{
	public:
		explicit LoggingSession(const Config& config) noexcept;
		~LoggingSession();
		LoggingSession(const LoggingSession&) = delete;
		LoggingSession& operator=(const LoggingSession&) = delete;
		bool ok() const noexcept { return ok_; }
		const std::string& message() const noexcept { return message_; }

	private:
		bool ok_ = false;
		std::string message_;
	};
	InitializeResult initialize(const Config& config, const RuntimeDependencies& dependencies = {}) noexcept;
	void flush() noexcept;
	void shutdown() noexcept;
	bool should_log(Category category, Level level) noexcept;
	void write_text(Category category, Level level, std::string_view file, int line, std::string_view text) noexcept;
	bool configure_training_audit(const std::filesystem::path& path) noexcept;
	void write_training_audit(std::string_view text) noexcept;

#if TEXASSOLVER_ENABLE_LOGGING
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
