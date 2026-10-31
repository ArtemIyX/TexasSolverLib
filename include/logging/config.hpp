#pragma once

/** @file
 * @brief Logging configuration, parsing, environment overrides and bootstrap results.
 */

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <unordered_map>

namespace texas::logging
{
	/** @brief Number of configurable logging categories. */
	inline constexpr std::size_t category_count = 8U;

	/** @brief Severity threshold for log messages. */
	enum class Level : std::uint8_t
	{
		Trace,	  ///< Fine-grained diagnostic events.
		Debug,	  ///< Diagnostic events useful during development.
		Info,	  ///< Routine operational events.
		Warning,  ///< Unexpected condition that allows continued operation.
		Error,	  ///< Operation failed.
		Critical, ///< Severe failure.
		Off		  ///< Disable messages at every level.
	};
	/** @brief Functional category associated with a log message. */
	enum class Category : std::uint8_t
	{
		Core,		 ///< Core application behavior.
		Config,		 ///< Configuration loading and validation.
		Workflow,	 ///< Workflow orchestration.
		Solver,		 ///< Solver work.
		Storage,	 ///< Persistent storage operations.
		Checkpoint,	 ///< Checkpoint creation and restore.
		Performance, ///< Performance measurements.
		Validation,	 ///< Validation results.
		Count		 ///< Sentinel equal to the number of categories.
	};
	/** @brief Console color selection policy. */
	enum class ColorMode : std::uint8_t
	{
		Automatic, ///< Enable color when output capabilities permit.
		Always,	   ///< Always enable color output.
		Never	   ///< Never emit color codes.
	};
	/** @brief Progress display policy. */
	enum class ProgressMode : std::uint8_t
	{
		Automatic, ///< Choose a display based on terminal capabilities.
		Live,	   ///< Refresh a live progress line.
		Lines,	   ///< Emit periodic progress lines.
		Off		   ///< Disable progress output.
	};

	/** @brief Console sink settings. */
#pragma pack(push, 8)
	struct ConsoleConfig
	{
		bool enabled = true;					///< Whether console logging is enabled.
		bool stderr_stream = false;				///< Write to stderr instead of stdout.
		Level level = Level::Info;				///< Minimum console message severity.
		ColorMode color = ColorMode::Automatic; ///< Console color policy.
		std::string pattern = "%v";				///< spdlog pattern for console messages.
	};
#pragma pack(pop)
	/** @brief Rotating file sink settings. */
#pragma pack(push, 8)
	struct FileConfig
	{
		bool enabled = false;											  ///< Whether file logging is enabled.
		bool required = false;											  ///< Treat sink creation failure as initialization failure.
		bool append = true;												  ///< Append to the active log file when opening it.
		Level level = Level::Debug;										  ///< Minimum file message severity.
		std::filesystem::path path = "logs/texassolver.log";			  ///< Active log-file path.
		std::uint32_t rotate_size_mb = 20U;								  ///< Maximum size before rotation, in MiB.
		std::uint32_t rotate_files = 5U;								  ///< Number of rotated files to retain.
		std::string pattern = "[%Y-%m-%d %H:%M:%S.%e] [%l] [%n] [%t] %v"; ///< spdlog pattern for file messages.
	};
#pragma pack(pop)
	/** @brief Progress display and refresh settings. */
#pragma pack(push, 8)
	struct ProgressConfig
	{
		bool enabled = true;						 ///< Whether progress display is enabled.
		ProgressMode mode = ProgressMode::Automatic; ///< Display mode.
		std::uint32_t refresh_ms = 100U;			 ///< Minimum live-display refresh interval in milliseconds.
		std::uint32_t line_interval = 1U;			 ///< Progress units between line-mode updates.
		bool show_elapsed = true;					 ///< Include elapsed time in progress output.
		bool show_eta = true;						 ///< Include estimated completion time when available.
		bool show_rate = true;						 ///< Include the configured or measured rate.
	};
#pragma pack(pop)
	/** @brief Effective logging settings after file and override processing. */
#pragma pack(push, 8)
	struct Config
	{
		bool enabled = true;									///< Global logging switch.
		Level default_level = Level::Info;						///< Default minimum severity.
		bool default_level_overridden = false;					///< Whether an override set default_level.
		Level flush_level = Level::Warning;						///< Severity at which messages trigger a flush.
		ConsoleConfig console{};								///< Console sink settings.
		FileConfig file{};										///< File sink settings.
		ProgressConfig progress{};								///< Progress display settings.
		std::array<Level, category_count> categories{};			///< Minimum severity by category.
		std::array<bool, category_count> category_overridden{}; ///< Whether each category has an explicit level.
		/** @brief Initializes category thresholds from the default level. */
		Config();
	};
#pragma pack(pop)
	/** @brief One configuration bootstrap error and its source location. */
#pragma pack(push, 8)
	struct BootstrapError
	{
		std::filesystem::path file; ///< File associated with the error, when known.
		std::string key;			///< Configuration key associated with the error, when known.
		std::string reason;			///< Human-readable cause.
	};
#pragma pack(pop)
	/** @brief Effective config or the first bootstrap error. */
#pragma pack(push, 8)
	struct ConfigLoadResult
	{
		Config config{};		///< Parsed or defaulted configuration.
		BootstrapError error{}; ///< Error details when ok is false.
		bool ok = true;			///< Whether loading and merging succeeded.
	};
#pragma pack(pop)
	/** @brief Command-line overrides for logging settings. */
#pragma pack(push, 8)
	struct Overrides
	{
		std::filesystem::path config_path; ///< Explicit configuration file path.
		std::string level;				   ///< Default severity override text.
		std::filesystem::path file;		   ///< File sink path override.
		std::string color;				   ///< Console color-mode override text.
		bool no_console = false;		   ///< Disable console output.
		bool no_file = false;			   ///< Disable file output.
		bool no_log = false;			   ///< Disable logging globally.
		bool quiet = false;				   ///< Select quiet-mode defaults.
	};
#pragma pack(pop)
	/** @brief Environment-variable names mapped to their values. */
	using EnvironmentMap = std::unordered_map<std::string, std::string>;

	/** @brief Returns the stable text name for a severity level. */
	const char* level_name(Level value) noexcept;
	/** @brief Returns the stable text name for a logging category. */
	const char* category_name(Category value) noexcept;
	/** @brief Returns the stable text name for a color mode. */
	const char* color_mode_name(ColorMode value) noexcept;
	/** @brief Returns the stable text name for a progress mode. */
	const char* progress_mode_name(ProgressMode value) noexcept;
	/** @brief Parses a severity name into result; false leaves result unchanged. */
	bool parse_level(std::string_view text, Level& result) noexcept;
	/** @brief Parses a color-mode name into result. */
	bool parse_color_mode(std::string_view text, ColorMode& result) noexcept;
	/** @brief Parses a progress-mode name into result. */
	bool parse_progress_mode(std::string_view text, ProgressMode& result) noexcept;
	/** @brief Parses a category name into result. */
	bool parse_category(std::string_view text, Category& result) noexcept;
	/** @brief Loads logging settings from a file without throwing. */
	ConfigLoadResult load_config(const std::filesystem::path& path) noexcept;
	/** @brief Applies environment variables and command-line overrides to config. */
	ConfigLoadResult merge_config(Config config, const EnvironmentMap& environment, const Overrides& overrides) noexcept;
	/** @brief Selects a configuration path from environment and command-line overrides. */
	std::filesystem::path select_config_path(const EnvironmentMap& environment, const Overrides& overrides) noexcept;
	/** @brief Scans process arguments for logging options and records malformed input in error. */
	Overrides scan_logging_options(int argc, char** argv, BootstrapError& error) noexcept;
	/** @brief Captures the current process environment as a map. */
	EnvironmentMap process_environment() noexcept;
} // namespace texas::logging
