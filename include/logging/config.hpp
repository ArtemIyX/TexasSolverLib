#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <unordered_map>

namespace texas::logging
{
	inline constexpr std::size_t category_count = 8U;

	enum class Level : std::uint8_t { Trace, Debug, Info, Warning, Error, Critical, Off };
	enum class Category : std::uint8_t { Core, Config, Workflow, Solver, Storage, Checkpoint, Performance, Validation, Count };
	enum class ColorMode : std::uint8_t { Automatic, Always, Never };
	enum class ProgressMode : std::uint8_t { Automatic, Live, Lines, Off };

	struct ConsoleConfig
	{
		bool enabled = true;
		bool stderr_stream = false;
		Level level = Level::Info;
		ColorMode color = ColorMode::Automatic;
		std::string pattern = "%v";
	};
	struct FileConfig
	{
		bool enabled = false;
		bool required = false;
		bool append = true;
		Level level = Level::Debug;
		std::filesystem::path path = "logs/texassolver.log";
		std::uint32_t rotate_size_mb = 20U;
		std::uint32_t rotate_files = 5U;
		std::string pattern = "[%Y-%m-%d %H:%M:%S.%e] [%l] [%n] [%t] %v";
	};
	struct ProgressConfig
	{
		bool enabled = true;
		ProgressMode mode = ProgressMode::Automatic;
		std::uint32_t refresh_ms = 100U;
		std::uint32_t line_interval = 1U;
		bool show_elapsed = true;
		bool show_eta = true;
		bool show_rate = true;
	};
	struct Config
	{
		bool enabled = true;
		Level default_level = Level::Info;
		bool default_level_overridden = false;
		Level flush_level = Level::Warning;
		ConsoleConfig console{};
		FileConfig file{};
		ProgressConfig progress{};
		std::array<Level, category_count> categories{};
		std::array<bool, category_count> category_overridden{};
		Config();
	};
	struct BootstrapError
	{
		std::filesystem::path file;
		std::string key;
		std::string reason;
	};
	struct ConfigLoadResult
	{
		Config config{};
		BootstrapError error{};
		bool ok = true;
	};
	struct Overrides
	{
		std::filesystem::path config_path;
		std::string level;
		std::filesystem::path file;
		std::string color;
		bool no_console = false;
		bool no_file = false;
		bool no_log = false;
		bool quiet = false;
	};
	using EnvironmentMap = std::unordered_map<std::string, std::string>;

	const char* level_name(Level value) noexcept;
	const char* category_name(Category value) noexcept;
	const char* color_mode_name(ColorMode value) noexcept;
	const char* progress_mode_name(ProgressMode value) noexcept;
	bool parse_level(std::string_view text, Level& result) noexcept;
	bool parse_color_mode(std::string_view text, ColorMode& result) noexcept;
	bool parse_progress_mode(std::string_view text, ProgressMode& result) noexcept;
	bool parse_category(std::string_view text, Category& result) noexcept;
	ConfigLoadResult load_config(const std::filesystem::path& path) noexcept;
	ConfigLoadResult merge_config(Config config, const EnvironmentMap& environment, const Overrides& overrides) noexcept;
	std::filesystem::path select_config_path(const EnvironmentMap& environment, const Overrides& overrides) noexcept;
	Overrides scan_logging_options(int argc, char** argv, BootstrapError& error) noexcept;
	EnvironmentMap process_environment() noexcept;
}
