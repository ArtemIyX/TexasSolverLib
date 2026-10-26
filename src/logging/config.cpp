#include "logging/config.hpp"

#include <toml++/toml.h>

#include <algorithm>
#include <array>
#include <cstdlib>
#include <limits>
#include <system_error>

namespace texas::logging
{
	namespace
	{
		constexpr std::array<const char*, category_count> category_names = {
			"Core", "Config", "Workflow", "Solver", "Storage", "Checkpoint", "Performance", "Validation" };
		bool accepts(const toml::table& table, std::initializer_list<const char*> names, BootstrapError& error,
			const std::filesystem::path& file, std::string_view prefix)
		{
			for (const auto& [key, node] : table)
			{
				(void)node;
				if (std::find(names.begin(), names.end(), std::string(key.str())) == names.end())
				{
					error = { file, std::string(prefix) + std::string(key.str()), "unknown key" };
					return false;
				}
			}
			return true;
		}
		bool level_field(const toml::table& table, const char* key, Level& value, BootstrapError& error,
			const std::filesystem::path& file, const char* path)
		{
			if (const auto* node = table.get_as<std::string>(key))
			{
				if (parse_level(node->get(), value)) return true;
				error = { file, path, "invalid level" }; return false;
			}
			if (table.contains(key)) { error = { file, path, "expected string" }; return false; }
			return true;
		}
		bool bool_field(const toml::table& table, const char* key, bool& value, BootstrapError& error,
			const std::filesystem::path& file, const char* path)
		{
			if (const auto* node = table.get_as<bool>(key)) { value = node->get(); return true; }
			if (table.contains(key)) { error = { file, path, "expected boolean" }; return false; }
			return true;
		}
		bool string_field(const toml::table& table, const char* key, std::string& value, BootstrapError& error,
			const std::filesystem::path& file, const char* path)
		{
			if (const auto* node = table.get_as<std::string>(key)) { value = node->get(); return true; }
			if (table.contains(key)) { error = { file, path, "expected string" }; return false; }
			return true;
		}
		bool uint_field(const toml::table& table, const char* key, std::uint32_t& value, BootstrapError& error,
			const std::filesystem::path& file, const char* path, bool allow_zero = false)
		{
			if (const auto* node = table.get_as<std::int64_t>(key))
			{
				const auto raw = node->get();
				if (raw >= (allow_zero ? 0 : 1) && static_cast<std::uint64_t>(raw) <= std::numeric_limits<std::uint32_t>::max()) { value = static_cast<std::uint32_t>(raw); return true; }
				error = { file, path, "integer is out of range" }; return false;
			}
			if (table.contains(key)) { error = { file, path, "expected positive integer" }; return false; }
			return true;
		}
		bool apply_level(Level& target, const std::string& text, BootstrapError& error, const char* key)
		{
			if (text.empty()) return true;
			if (parse_level(text, target)) return true;
			error = { {}, key, "invalid level" }; return false;
		}
	}

	Config::Config()
	{
		categories.fill(default_level);
		category_overridden.fill(false);
		categories[static_cast<std::size_t>(Category::Solver)] = Level::Warning;
		categories[static_cast<std::size_t>(Category::Storage)] = Level::Debug;
		categories[static_cast<std::size_t>(Category::Performance)] = Level::Off;
		categories[static_cast<std::size_t>(Category::Validation)] = Level::Warning;
	}
	const char* level_name(Level value) noexcept { static constexpr const char* names[] = { "trace", "debug", "info", "warning", "error", "critical", "off" }; return static_cast<std::size_t>(value) < 7U ? names[static_cast<std::size_t>(value)] : "off"; }
	const char* category_name(Category value) noexcept { const auto index = static_cast<std::size_t>(value); return index < category_names.size() ? category_names[index] : "Unknown"; }
	const char* color_mode_name(ColorMode value) noexcept { static constexpr const char* names[] = { "auto", "always", "never" }; return static_cast<std::size_t>(value) < 3U ? names[static_cast<std::size_t>(value)] : "auto"; }
	const char* progress_mode_name(ProgressMode value) noexcept { static constexpr const char* names[] = { "auto", "live", "lines", "off" }; return static_cast<std::size_t>(value) < 4U ? names[static_cast<std::size_t>(value)] : "auto"; }
	bool parse_level(std::string_view text, Level& result) noexcept { for (std::size_t i = 0; i < 7U; ++i) if (text == level_name(static_cast<Level>(i))) { result = static_cast<Level>(i); return true; } return false; }
	bool parse_color_mode(std::string_view text, ColorMode& result) noexcept { for (std::size_t i = 0; i < 3U; ++i) if (text == color_mode_name(static_cast<ColorMode>(i))) { result = static_cast<ColorMode>(i); return true; } return false; }
	bool parse_progress_mode(std::string_view text, ProgressMode& result) noexcept { for (std::size_t i = 0; i < 4U; ++i) if (text == progress_mode_name(static_cast<ProgressMode>(i))) { result = static_cast<ProgressMode>(i); return true; } return false; }
	bool parse_category(std::string_view text, Category& result) noexcept { for (std::size_t i = 0; i < category_count; ++i) if (text == category_names[i]) { result = static_cast<Category>(i); return true; } return false; }

	ConfigLoadResult load_config(const std::filesystem::path& path) noexcept
	{
		ConfigLoadResult result;
		try
		{
			if (path.empty()) return result;
			const toml::table root = toml::parse_file(path.string());
			if (!accepts(root, { "schema_version", "logging", "progress" }, result.error, path, "")) { result.ok = false; return result; }
			const auto* schema = root.get_as<std::int64_t>("schema_version");
			if (schema == nullptr || schema->get() != 1) { result.ok = false; result.error = { path, "schema_version", "unsupported schema version" }; return result; }
			if (const auto* logging = root["logging"].as_table())
			{
				if (!accepts(*logging, { "enabled", "default_level", "flush_level", "console", "file", "categories" }, result.error, path, "logging.")) { result.ok = false; return result; }
				if (!bool_field(*logging, "enabled", result.config.enabled, result.error, path, "logging.enabled") ||
					!level_field(*logging, "default_level", result.config.default_level, result.error, path, "logging.default_level") ||
					!level_field(*logging, "flush_level", result.config.flush_level, result.error, path, "logging.flush_level")) { result.ok = false; return result; }
				result.config.default_level_overridden = logging->contains("default_level");
				if (result.config.default_level_overridden)
					for (auto& category : result.config.categories) category = result.config.default_level;
				if (const auto* console = (*logging)["console"].as_table())
				{
					if (!accepts(*console, { "enabled", "stream", "level", "color", "pattern" }, result.error, path, "logging.console.")) { result.ok = false; return result; }
					std::string stream; std::string color;
					if (!bool_field(*console, "enabled", result.config.console.enabled, result.error, path, "logging.console.enabled") || !level_field(*console, "level", result.config.console.level, result.error, path, "logging.console.level") || !string_field(*console, "pattern", result.config.console.pattern, result.error, path, "logging.console.pattern") || !string_field(*console, "stream", stream, result.error, path, "logging.console.stream") || !string_field(*console, "color", color, result.error, path, "logging.console.color")) { result.ok = false; return result; }
					if (!stream.empty() && stream != "stdout" && stream != "stderr") { result.ok=false; result.error={path,"logging.console.stream","must be stdout or stderr"}; return result; }
					result.config.console.stderr_stream = stream == "stderr";
					if (!color.empty() && !parse_color_mode(color, result.config.console.color)) { result.ok=false; result.error={path,"logging.console.color","invalid color mode"}; return result; }
				}
				else if (logging->contains("console")) { result.ok=false; result.error={path,"logging.console","expected table"}; return result; }
				if (const auto* file = (*logging)["file"].as_table())
				{
					if (!accepts(*file, { "enabled", "level", "path", "required", "append", "rotate_size_mb", "rotate_files", "pattern" }, result.error, path, "logging.file.")) { result.ok=false; return result; }
					std::string file_path;
					if (!bool_field(*file,"enabled",result.config.file.enabled,result.error,path,"logging.file.enabled") || !bool_field(*file,"required",result.config.file.required,result.error,path,"logging.file.required") || !bool_field(*file,"append",result.config.file.append,result.error,path,"logging.file.append") || !level_field(*file,"level",result.config.file.level,result.error,path,"logging.file.level") || !uint_field(*file,"rotate_size_mb",result.config.file.rotate_size_mb,result.error,path,"logging.file.rotate_size_mb") || !uint_field(*file,"rotate_files",result.config.file.rotate_files,result.error,path,"logging.file.rotate_files",true) || !string_field(*file,"path",file_path,result.error,path,"logging.file.path") || !string_field(*file,"pattern",result.config.file.pattern,result.error,path,"logging.file.pattern")) { result.ok=false; return result; }
					if (!file_path.empty()) { result.config.file.path=file_path; if (result.config.file.path.is_relative()) result.config.file.path=path.parent_path()/result.config.file.path; }
				}
				else if (logging->contains("file")) { result.ok=false; result.error={path,"logging.file","expected table"}; return result; }
				if (const auto* categories = (*logging)["categories"].as_table())
				{
					for (const auto& [key, node] : *categories) { Category category; const auto* level = node.as_string(); if (!parse_category(key.str(), category) || level == nullptr || !parse_level(level->get(), result.config.categories[static_cast<std::size_t>(category)])) { result.ok=false; result.error={path,"logging.categories."+std::string(key.str()),"invalid category or level"}; return result; } else result.config.category_overridden[static_cast<std::size_t>(category)]=true; }
				}
				else if (logging->contains("categories")) { result.ok=false; result.error={path,"logging.categories","expected table"}; return result; }
			}
			else if (root.contains("logging")) { result.ok=false; result.error={path,"logging","expected table"}; return result; }
			if (const auto* progress = root["progress"].as_table())
			{
				if (!accepts(*progress, { "enabled", "mode", "refresh_ms", "line_interval", "show_elapsed", "show_eta", "show_rate" }, result.error, path, "progress.")) { result.ok=false; return result; }
				std::string mode;
				if (!bool_field(*progress,"enabled",result.config.progress.enabled,result.error,path,"progress.enabled") || !string_field(*progress,"mode",mode,result.error,path,"progress.mode") || !uint_field(*progress,"refresh_ms",result.config.progress.refresh_ms,result.error,path,"progress.refresh_ms",true) || !uint_field(*progress,"line_interval",result.config.progress.line_interval,result.error,path,"progress.line_interval") || !bool_field(*progress,"show_elapsed",result.config.progress.show_elapsed,result.error,path,"progress.show_elapsed") || !bool_field(*progress,"show_eta",result.config.progress.show_eta,result.error,path,"progress.show_eta") || !bool_field(*progress,"show_rate",result.config.progress.show_rate,result.error,path,"progress.show_rate")) { result.ok=false; return result; }
				if (!mode.empty() && !parse_progress_mode(mode,result.config.progress.mode)) { result.ok=false; result.error={path,"progress.mode","invalid progress mode"}; return result; }
			}
			else if (root.contains("progress")) { result.ok=false; result.error={path,"progress","expected table"}; return result; }
		}
		catch (const std::exception& error) { result.ok=false; result.error={path,"",error.what()}; }
		return result;
	}

	ConfigLoadResult merge_config(Config config, const EnvironmentMap& environment, const Overrides& overrides) noexcept
	{
		ConfigLoadResult result; result.config = std::move(config);
		auto read = [&](const char* name) -> const std::string* { const auto it=environment.find(name); return it==environment.end()?nullptr:&it->second; };
		if (const auto* value=read("TEXASSOLVER_LOG_LEVEL")) { if (!apply_level(result.config.default_level,*value,result.error,"TEXASSOLVER_LOG_LEVEL")) { result.ok=false; return result; } result.config.default_level_overridden=true; }
		if (const auto* value=read("TEXASSOLVER_LOG_FILE")) { result.config.file.path=*value; result.config.file.enabled=true; }
		if (const auto* value=read("TEXASSOLVER_LOG_COLOR")) if (!parse_color_mode(*value,result.config.console.color)) { result.ok=false; result.error={{},"TEXASSOLVER_LOG_COLOR","invalid color mode"}; return result; }
		if (read("NO_COLOR") != nullptr && result.config.console.color == ColorMode::Automatic) result.config.console.color=ColorMode::Never;
		if (read("CI") != nullptr && result.config.progress.mode == ProgressMode::Automatic) result.config.progress.mode=ProgressMode::Lines;
		if (!apply_level(result.config.default_level,overrides.level,result.error,"--log-level")) { result.ok=false; return result; }
		if (!overrides.level.empty()) result.config.default_level_overridden=true;
		if (!overrides.file.empty()) { result.config.file.path=overrides.file; result.config.file.enabled=true; }
		if (!overrides.color.empty() && !parse_color_mode(overrides.color,result.config.console.color)) { result.ok=false; result.error={{},"--log-color","invalid color mode"}; return result; }
		if (overrides.no_console || overrides.quiet) result.config.console.enabled=false;
		if (overrides.quiet) result.config.progress.enabled=false;
		if (overrides.no_file) result.config.file.enabled=false;
		if (overrides.no_log) { result.config.enabled=false; result.config.console.enabled=false; result.config.file.enabled=false; }
		if (result.config.default_level_overridden)
			for (std::size_t index=0; index<category_count; ++index) if (!result.config.category_overridden[index]) result.config.categories[index] = result.config.default_level;
		return result;
	}
	std::filesystem::path select_config_path(const EnvironmentMap& environment, const Overrides& overrides) noexcept
	{
		if (!overrides.config_path.empty()) return overrides.config_path;
		const auto found = environment.find("TEXASSOLVER_LOG_CONFIG");
		return found == environment.end() ? std::filesystem::path{} : std::filesystem::path(found->second);
	}
	Overrides scan_logging_options(int argc, char** argv, BootstrapError& error) noexcept
	{
		Overrides result;
		for (int index=1; index<argc; ++index)
		{
			const std::string_view argument(argv[index]);
			auto value = [&](std::string& output) { if (index+1>=argc) { error={{},std::string(argument),"missing value"}; return false; } output=argv[++index]; return true; };
			if (argument=="--log-config") { std::string text; if (!value(text)) return result; result.config_path=text; }
			else if (argument=="--log-level") { if (!value(result.level)) return result; }
			else if (argument=="--log-file") { std::string text; if (!value(text)) return result; result.file=text; }
			else if (argument=="--log-color") { if (!value(result.color)) return result; }
			else if (argument=="--no-console-log") result.no_console=true;
			else if (argument=="--no-file-log") result.no_file=true;
			else if (argument=="--no-log") result.no_log=true;
			else if (argument=="--quiet") result.quiet=true;
		}
		return result;
	}
	EnvironmentMap process_environment() noexcept
	{
		EnvironmentMap result;
		for (const char* key : { "TEXASSOLVER_LOG_CONFIG", "TEXASSOLVER_LOG_LEVEL", "TEXASSOLVER_LOG_FILE", "TEXASSOLVER_LOG_COLOR", "NO_COLOR", "CI" }) if (const auto* value=std::getenv(key)) result.emplace(key,value);
		return result;
	}
}
