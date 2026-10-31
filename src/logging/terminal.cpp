#include "terminal.hpp"

#include <chrono>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <sstream>

namespace texas::logging::detail
{
	namespace
	{
#pragma pack(push, 8)
		struct TerminalState
		{
			std::mutex mutex;
			std::ostream* output = nullptr;
			bool interactive = false;
			bool enabled = false;
			ProgressMode mode = ProgressMode::Off;
			std::uint32_t refresh_ms = 100U;
			std::uint32_t line_interval = 1U;
			std::uint64_t next_id = 1U;
			std::uint64_t active_id = 0U;
			std::string label;
			std::string fields;
			std::uint64_t total = 0U;
			std::uint64_t completed = 0U;
			double rate = 0.0;
			std::size_t previous_size = 0U;
			std::chrono::steady_clock::time_point last_render{};
			std::chrono::steady_clock::time_point started_at{};
			std::function<std::chrono::steady_clock::time_point()> now;
			bool show_elapsed = true;
			bool show_eta = true;
			bool show_rate = true;
		};
#pragma pack(pop)
		static_assert(alignof(std::mutex) <= 8, "packed TerminalState requires naturally aligned mutexes");
		TerminalState& state()
		{
			static TerminalState value;
			return value;
		}
		std::string line(const TerminalState& value)
		{
			std::ostringstream output;
			output << value.label;
			if (value.total != 0U)
				output << ": " << value.completed << '/' << value.total;
			else
				output << ": " << value.completed;
			const auto current = value.now ? value.now() : std::chrono::steady_clock::now();
			const auto elapsed = std::chrono::duration<double>(current - value.started_at).count();
			if (value.show_elapsed && value.started_at.time_since_epoch().count() != 0)
				output << " elapsed=" << std::fixed << std::setprecision(1) << elapsed << "s";
			if (value.show_rate && value.rate > 0.0)
				output << " rate=" << std::fixed << std::setprecision(1) << value.rate << "/s";
			if (value.show_eta && value.total > value.completed && value.rate > 0.0)
				output << " eta=" << std::fixed << std::setprecision(1) << static_cast<double>(value.total - value.completed) / value.rate << "s";
			output << value.fields;
			return output.str();
		}
		void clear_live(TerminalState& value)
		{
			if (value.interactive && value.active_id != 0U && value.previous_size != 0U)
				*value.output << '\r' << std::string(value.previous_size, ' ') << '\r';
		}
		void render(TerminalState& value, bool final)
		{
			if (!value.enabled || value.mode == ProgressMode::Off || value.active_id == 0U || value.output == nullptr)
				return;
			const std::string text = line(value);
			if (value.mode == ProgressMode::Live && value.interactive)
			{
				*value.output << '\r' << text;
				if (text.size() < value.previous_size)
					*value.output << std::string(value.previous_size - text.size(), ' ') << '\r' << text;
				value.previous_size = text.size();
				if (final)
					*value.output << '\n';
			}
			else if (final || value.line_interval == 1U || value.completed % value.line_interval == 0U)
				*value.output << text << '\n';
			value.output->flush();
			value.last_render = value.now ? value.now() : std::chrono::steady_clock::now();
		}
	} // namespace
	void configure_terminal(std::ostream* output, bool interactive, const ProgressConfig& config,
		const std::function<std::chrono::steady_clock::time_point()>& now) noexcept
	{
		auto& value = state();
		std::lock_guard<std::mutex> lock(value.mutex);
		value.output = output;
		value.interactive = interactive;
		value.enabled = config.enabled;
		value.mode = config.mode == ProgressMode::Automatic ? (interactive ? ProgressMode::Live : ProgressMode::Lines) : config.mode;
		if (!interactive && value.mode == ProgressMode::Live)
			value.mode = ProgressMode::Lines;
		value.refresh_ms = config.refresh_ms;
		value.line_interval = config.line_interval;
		value.now = now;
		value.show_elapsed = config.show_elapsed;
		value.show_eta = config.show_eta;
		value.show_rate = config.show_rate;
	}
	void reset_terminal() noexcept
	{
		auto& value = state();
		std::lock_guard<std::mutex> lock(value.mutex);
		if (value.active_id != 0U)
		{
			clear_live(value);
			value.active_id = 0U;
		}
		value.output = nullptr;
	}
	void write_console(std::string_view text, Level level, bool color) noexcept
	{
		auto& value = state();
		std::lock_guard<std::mutex> lock(value.mutex);
		if (!value.output)
			return;
		clear_live(value);
		if (color)
		{
			const char* code = level >= Level::Error ? "\x1b[31m" : level == Level::Warning ? "\x1b[33m"
																							: "\x1b[0m";
			*value.output << code;
		}
		*value.output << text;
		if (text.empty() || text.back() != '\n')
			*value.output << '\n';
		if (color)
			*value.output << "\x1b[0m";
		if (value.active_id != 0U)
			render(value, false);
		value.output->flush();
	}
	std::uint64_t begin_progress(std::string_view label, std::uint64_t total) noexcept
	{
		auto& value = state();
		std::lock_guard<std::mutex> lock(value.mutex);
		if (!value.enabled || value.mode == ProgressMode::Off || !value.output || value.active_id != 0U)
			return 0U;
		value.active_id = value.next_id++;
		value.label = label;
		value.total = total;
		value.completed = 0U;
		value.fields.clear();
		value.rate = 0.0;
		value.previous_size = 0U;
		value.last_render = {};
		value.started_at = value.now ? value.now() : std::chrono::steady_clock::now();
		return value.active_id;
	}
	void update_progress(std::uint64_t id, std::uint64_t completed) noexcept
	{
		auto& value = state();
		std::lock_guard<std::mutex> lock(value.mutex);
		if (id == 0U || id != value.active_id)
			return;
		value.completed = completed;
		const auto now = value.now ? value.now() : std::chrono::steady_clock::now();
		if (value.last_render.time_since_epoch().count() != 0 && std::chrono::duration_cast<std::chrono::milliseconds>(now - value.last_render).count() < value.refresh_ms)
			return;
		render(value, false);
	}
	void add_progress_field(std::uint64_t id, std::string_view name, std::string_view field_value) noexcept
	{
		auto& value = state();
		std::lock_guard<std::mutex> lock(value.mutex);
		if (id == 0U || id != value.active_id)
			return;
		const std::string needle = " " + std::string(name) + "=";
		const auto begin = value.fields.find(needle);
		const std::string replacement = needle + std::string(field_value);
		if (begin == std::string::npos)
			value.fields += replacement;
		else
		{
			const auto end = value.fields.find(' ', begin + needle.size());
			value.fields.replace(begin, end == std::string::npos ? value.fields.size() - begin : end - begin, replacement);
		}
	}
	void set_progress_rate(std::uint64_t id, double rate) noexcept
	{
		auto& value = state();
		std::lock_guard<std::mutex> lock(value.mutex);
		if (id == value.active_id)
			value.rate = rate;
	}
	void finish_progress(std::uint64_t id, std::string_view message) noexcept
	{
		auto& value = state();
		std::lock_guard<std::mutex> lock(value.mutex);
		if (id == 0U || id != value.active_id)
			return;
		if (!message.empty())
		{
			value.label = message;
			value.fields.clear();
		}
		render(value, true);
		value.active_id = 0U;
		value.previous_size = 0U;
	}
} // namespace texas::logging::detail
