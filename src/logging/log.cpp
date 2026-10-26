#include "logging/log.hpp"
#include "terminal.hpp"

#include <spdlog/logger.h>
#include <spdlog/sinks/base_sink.h>
#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/sinks/rotating_file_sink.h>

#include <array>
#include <atomic>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <mutex>
#include <vector>

#if defined(_WIN32)
#include <io.h>
#include <windows.h>
#else
#include <unistd.h>
#endif

namespace texas::logging
{
	namespace
	{
		spdlog::level::level_enum to_spdlog(Level value) noexcept
		{
			static constexpr spdlog::level::level_enum levels[] = { spdlog::level::trace, spdlog::level::debug, spdlog::level::info, spdlog::level::warn, spdlog::level::err, spdlog::level::critical, spdlog::level::off };
			return levels[static_cast<std::size_t>(value) < 7U ? static_cast<std::size_t>(value) : 6U];
		}
		bool interactive_stdout() noexcept
		{
#if defined(_WIN32)
			const auto handle=GetStdHandle(STD_OUTPUT_HANDLE); DWORD mode=0U;
			if (handle==INVALID_HANDLE_VALUE || !GetConsoleMode(handle,&mode)) return false;
			SetConsoleMode(handle,mode|ENABLE_VIRTUAL_TERMINAL_PROCESSING);
			return _isatty(_fileno(stdout)) != 0;
#else
			return isatty(fileno(stdout)) != 0;
#endif
		}
		void fallback_error(const char* text) noexcept;
		class ConsoleSink final : public spdlog::sinks::base_sink<std::mutex>
		{
		public:
			explicit ConsoleSink(bool color) : color_(color) {}
		private:
			void sink_it_(const spdlog::details::log_msg& message) override
			{
				spdlog::memory_buf_t formatted;
				this->formatter_->format(message, formatted);
				detail::write_console(std::string_view(formatted.data(), formatted.size()), static_cast<Level>(message.level), color_);
			}
			void flush_() override {}
			bool color_ = false;
		};
		class OptionalFileSink final : public spdlog::sinks::base_sink<std::mutex>
		{
		public:
			explicit OptionalFileSink(spdlog::sink_ptr sink) : sink_(std::move(sink)) {}
			void set_inner_pattern(const std::string& pattern) { sink_->set_pattern(pattern); }
		private:
			void sink_it_(const spdlog::details::log_msg& message) override
			{
				if (failed_.load(std::memory_order_acquire)) return;
				try { sink_->log(message); }
				catch (...) { failed_.store(true, std::memory_order_release); fallback_error("TexasLogging: optional file sink disabled"); }
			}
			void flush_() override { if (!failed_.load(std::memory_order_acquire)) { try { sink_->flush(); } catch (...) { failed_.store(true, std::memory_order_release); } } }
			spdlog::sink_ptr sink_;
			std::atomic<bool> failed_{false};
		};
		struct LoggingState
		{
			Config config{};
			std::array<std::shared_ptr<spdlog::logger>, category_count> loggers{};
			std::shared_ptr<spdlog::sinks::sink> console_sink;
			std::shared_ptr<spdlog::sinks::sink> file_sink;
			mutable std::mutex audit_mutex;
			mutable std::ofstream audit_file;
			std::atomic<bool> enabled{false};
		};
		std::mutex lifecycle_mutex;
		std::unique_ptr<LoggingState> owner;
		LoggingState* active = nullptr;
		void fallback_error(const char* text) noexcept { std::fputs(text, stderr); std::fputc('\n', stderr); }
	}

	InitializeResult initialize(const Config& config, const RuntimeDependencies& dependencies) noexcept
	{
		std::lock_guard<std::mutex> lock(lifecycle_mutex);
		if (active != nullptr) { flush(); detail::reset_terminal(); active=nullptr; owner.reset(); }
		try
		{
			auto state=std::make_unique<LoggingState>(); state->config=config;
			const bool interactive=dependencies.terminal_known ? dependencies.interactive_terminal : interactive_stdout();
			const auto output=dependencies.console_stream != nullptr ? dependencies.console_stream : (config.console.stderr_stream ? &std::cerr : &std::cout);
			const bool automatic_color=config.console.color == ColorMode::Automatic;
			const bool color=interactive && (config.console.color == ColorMode::Always || automatic_color);
			detail::configure_terminal(output, interactive, config.progress, dependencies.now);
			if (config.enabled && config.console.enabled)
			{
				state->console_sink=std::make_shared<ConsoleSink>(color);
				state->console_sink->set_level(to_spdlog(config.console.level));
				state->console_sink->set_pattern(config.console.pattern);
			}
			if (config.enabled && config.file.enabled)
			{
				try
				{
					if (dependencies.fail_file_sink) throw std::runtime_error("injected file sink failure");
					if (!config.file.path.parent_path().empty()) std::filesystem::create_directories(config.file.path.parent_path());
					auto raw_sink=std::make_shared<spdlog::sinks::rotating_file_sink_mt>(config.file.path.string(), static_cast<std::size_t>(config.file.rotate_size_mb)*1024U*1024U, config.file.rotate_files, !config.file.append);
					raw_sink->set_level(to_spdlog(config.file.level)); raw_sink->set_pattern(config.file.pattern);
					if (config.file.required) state->file_sink=raw_sink;
					else state->file_sink=std::make_shared<OptionalFileSink>(raw_sink);
					state->file_sink->set_level(to_spdlog(config.file.level));
				}
				catch (const std::exception& error)
				{
					if (config.file.required)
					{
						detail::reset_terminal();
						return { false, std::string("required log file unavailable: ")+error.what() };
					}
					fallback_error("TexasLogging: optional file sink disabled");
				}
			}
			for (std::size_t index=0; index<category_count; ++index)
			{
				std::vector<spdlog::sink_ptr> sinks;
				if (state->console_sink) sinks.push_back(state->console_sink);
				if (state->file_sink) sinks.push_back(state->file_sink);
				auto logger=std::make_shared<spdlog::logger>(category_name(static_cast<Category>(index)),sinks.begin(),sinks.end());
				logger->set_level(spdlog::level::trace);
				logger->flush_on(to_spdlog(config.flush_level));
				logger->set_error_handler([](const std::string&) { fallback_error("TexasLogging: sink write failure"); });
				state->loggers[index]=std::move(logger);
			}
			state->enabled.store(config.enabled && (state->console_sink || state->file_sink), std::memory_order_release);
			active=state.get(); owner=std::move(state);
			return { true, {} };
		}
		catch (const std::exception& error) { detail::reset_terminal(); return { false, error.what() }; }
		catch (...) { detail::reset_terminal(); return { false, "unknown logging initialization failure" }; }
	}
	void flush() noexcept
	{
		const auto* state=active; if (!state) return;
		try
		{
			for (const auto& logger:state->loggers)
				if (logger) logger->flush();
			std::lock_guard<std::mutex> audit_lock(state->audit_mutex);
			if (state->audit_file.is_open()) state->audit_file.flush();
		}
		catch (...) {}
	}
	void shutdown() noexcept
	{
		std::lock_guard<std::mutex> lock(lifecycle_mutex);
		if (!active) return;
		flush(); detail::reset_terminal(); active=nullptr; owner.reset();
	}
	bool should_log(Category category, Level level) noexcept
	{
		const auto* state=active; const auto index=static_cast<std::size_t>(category);
		return state != nullptr && state->enabled.load(std::memory_order_acquire) && index < category_count && level >= state->config.categories[index] && level != Level::Off;
	}
	void write_text(Category category, Level level, std::string_view file, int line, std::string_view text) noexcept
	{
		const auto* state=active; const auto index=static_cast<std::size_t>(category);
		if (!state || index >= category_count || !should_log(category,level)) return;
		try { state->loggers[index]->log(spdlog::source_loc{file.data(),line,""},to_spdlog(level), "{}", std::string(text)); } catch (...) { fallback_error("TexasLogging: record dropped"); }
	}
	bool configure_training_audit(const std::filesystem::path& path) noexcept
	{
		std::lock_guard<std::mutex> lifecycle_lock(lifecycle_mutex);
		const auto* state=active; if (!state || !state->config.enabled) return true;
		try
		{
			if (!path.parent_path().empty()) std::filesystem::create_directories(path.parent_path());
			std::lock_guard<std::mutex> audit_lock(state->audit_mutex);
			state->audit_file.close();
			state->audit_file.clear();
			state->audit_file.open(path, std::ios::out | std::ios::app | std::ios::binary);
			return state->audit_file.is_open();
		}
		catch (...) { fallback_error("TexasLogging: training audit sink unavailable"); return false; }
	}
	void write_training_audit(std::string_view text) noexcept
	{
		std::lock_guard<std::mutex> lifecycle_lock(lifecycle_mutex);
		const auto* state=active;
		if (!state) return;
		try
		{
			std::lock_guard<std::mutex> audit_lock(state->audit_mutex);
			if (!state->audit_file.is_open()) return;
			state->audit_file.write(text.data(), static_cast<std::streamsize>(text.size()));
			state->audit_file.put('\n');
			state->audit_file.flush();
			if (!state->audit_file) fallback_error("TexasLogging: training audit write failure");
		}
		catch (...) { fallback_error("TexasLogging: training audit write failure"); }
	}
	LoggingSession::LoggingSession(const Config& config) noexcept { const auto result=initialize(config); ok_=result.ok; message_=result.message; }
	LoggingSession::~LoggingSession() { shutdown(); }
}
