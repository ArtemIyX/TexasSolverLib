#include "logging/config.hpp"

#include <cstdlib>

namespace texas::logging
{
	Config::Config() { categories.fill(default_level); }
	const char* level_name(Level value) noexcept { static constexpr const char* names[]={"trace","debug","info","warning","error","critical","off"}; return static_cast<std::size_t>(value)<7U?names[static_cast<std::size_t>(value)]:"off"; }
	const char* category_name(Category value) noexcept { static constexpr const char* names[]={"Core","Config","Workflow","Solver","Storage","Checkpoint","Performance","Validation"}; return static_cast<std::size_t>(value)<category_count?names[static_cast<std::size_t>(value)]:"Unknown"; }
	const char* color_mode_name(ColorMode value) noexcept { static constexpr const char* names[]={"auto","always","never"}; return static_cast<std::size_t>(value)<3U?names[static_cast<std::size_t>(value)]:"auto"; }
	const char* progress_mode_name(ProgressMode value) noexcept { static constexpr const char* names[]={"auto","live","lines","off"}; return static_cast<std::size_t>(value)<4U?names[static_cast<std::size_t>(value)]:"auto"; }
	bool parse_level(std::string_view text, Level& result) noexcept { for (std::size_t i=0;i<7U;++i) if(text==level_name(static_cast<Level>(i))){result=static_cast<Level>(i);return true;} return false; }
	bool parse_color_mode(std::string_view text, ColorMode& result) noexcept { for (std::size_t i=0;i<3U;++i) if(text==color_mode_name(static_cast<ColorMode>(i))){result=static_cast<ColorMode>(i);return true;} return false; }
	bool parse_progress_mode(std::string_view text, ProgressMode& result) noexcept { for(std::size_t i=0;i<4U;++i)if(text==progress_mode_name(static_cast<ProgressMode>(i))){result=static_cast<ProgressMode>(i);return true;}return false; }
	bool parse_category(std::string_view text, Category& result) noexcept { for(std::size_t i=0;i<category_count;++i)if(text==category_name(static_cast<Category>(i))){result=static_cast<Category>(i);return true;}return false; }
	ConfigLoadResult load_config(const std::filesystem::path&) noexcept { return {}; }
	ConfigLoadResult merge_config(Config config, const EnvironmentMap&, const Overrides&) noexcept { ConfigLoadResult result; result.config=std::move(config); result.config.enabled=false; result.config.console.enabled=false; result.config.file.enabled=false; result.config.progress.enabled=false; return result; }
	std::filesystem::path select_config_path(const EnvironmentMap&, const Overrides& overrides) noexcept { return overrides.config_path; }
	Overrides scan_logging_options(int argc,char** argv,BootstrapError& error) noexcept { Overrides result; for(int i=1;i<argc;++i){const std::string_view option(argv[i]);auto next=[&](std::string& out){if(i+1>=argc){error={{},std::string(option),"missing value"};return false;}out=argv[++i];return true;};if(option=="--log-config"){std::string value;if(!next(value))return result;result.config_path=value;}else if(option=="--log-level"){if(!next(result.level))return result;}else if(option=="--log-file"){std::string value;if(!next(value))return result;result.file=value;}else if(option=="--log-color"){if(!next(result.color))return result;}else if(option=="--no-console-log")result.no_console=true;else if(option=="--no-file-log")result.no_file=true;else if(option=="--no-log")result.no_log=true;else if(option=="--quiet")result.quiet=true;} return result; }
	EnvironmentMap process_environment() noexcept { return {}; }
}
