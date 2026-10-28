#include "logging/config.hpp"
#include "test_harness.hpp"

#include <atomic>
#include <filesystem>
#include <fstream>

namespace
{
	namespace logging = texas::logging;

	std::filesystem::path temporary_directory()
	{
		static std::atomic<unsigned> counter{ 0U };
		const auto path = std::filesystem::temp_directory_path() / ("texassolver_logging_config_" + std::to_string(counter.fetch_add(1U)));
		std::filesystem::create_directories(path);
		return path;
	}

	std::filesystem::path write_toml(const std::filesystem::path& directory, const std::string& text)
	{
		const auto path = directory / "logging.toml";
		std::ofstream output(path);
		output << text;
		return path;
	}
} // namespace

TEST_CASE(logging_config_defaults_and_complete_toml)
{
	logging::Config defaults;
	EXPECT_EQ(defaults.default_level, logging::Level::Info);
	EXPECT_EQ(defaults.categories[static_cast<std::size_t>(logging::Category::Performance)], logging::Level::Off);

	const auto directory = temporary_directory();
	const auto result = logging::load_config(write_toml(directory, R"(schema_version = 1
[logging]
enabled = true
default_level = "debug"
flush_level = "error"
[logging.console]
enabled = true
stream = "stderr"
level = "warning"
color = "never"
pattern = "%v"
[logging.file]
enabled = true
level = "trace"
path = "logs/run.log"
required = true
append = false
rotate_size_mb = 2
rotate_files = 3
pattern = "%v"
[logging.categories]
Workflow = "info"
[progress]
enabled = true
mode = "lines"
refresh_ms = 0
line_interval = 2
show_elapsed = false
show_eta = false
show_rate = false
)"));
	EXPECT_TRUE(result.ok);
	EXPECT_EQ(result.config.default_level, logging::Level::Debug);
	EXPECT_EQ(result.config.console.level, logging::Level::Warning);
	EXPECT_TRUE(result.config.console.stderr_stream);
	EXPECT_EQ(result.config.file.path, directory / "logs/run.log");
	EXPECT_EQ(result.config.categories[static_cast<std::size_t>(logging::Category::Workflow)], logging::Level::Info);
	EXPECT_EQ(result.config.progress.mode, logging::ProgressMode::Lines);
}

TEST_CASE(logging_config_rejects_invalid_inputs)
{
	const auto directory = temporary_directory();
	const auto expect_invalid = [&](const std::string& text) {
		EXPECT_TRUE(!logging::load_config(write_toml(directory, text)).ok);
	};
	expect_invalid("schema_version = 2\n");
	expect_invalid("schema_version = 1\nunknown = true\n");
	expect_invalid("schema_version = 1\n[logging]\ndefault_level = \"loud\"\n");
	expect_invalid("schema_version = 1\n[logging.categories]\nOther = \"info\"\n");
	expect_invalid("schema_version = 1\n[progress]\nmode = \"fast\"\n");
	expect_invalid("schema_version = 1\nschema_version = 1\n");
	expect_invalid("schema_version = 1\n[logging.file]\nrotate_size_mb = 4294967296\n");
	EXPECT_TRUE(!logging::load_config(directory / "missing.toml").ok);
}

TEST_CASE(logging_config_precedence_preserves_category_override)
{
	const auto directory = temporary_directory();
	auto loaded = logging::load_config(write_toml(directory, R"(schema_version = 1
[logging]
default_level = "debug"
[logging.categories]
Workflow = "info"
)"));
	EXPECT_TRUE(loaded.ok);
	logging::EnvironmentMap environment{
		{ "TEXASSOLVER_LOG_LEVEL", "warning" },
		{ "TEXASSOLVER_LOG_FILE", "env.log" },
		{ "TEXASSOLVER_LOG_COLOR", "always" }
	};
	logging::Overrides overrides;
	overrides.level = "error";
	overrides.file = "cli.log";
	overrides.color = "never";
	const auto merged = logging::merge_config(loaded.config, environment, overrides);
	EXPECT_TRUE(merged.ok);
	EXPECT_EQ(merged.config.default_level, logging::Level::Error);
	EXPECT_EQ(merged.config.categories[static_cast<std::size_t>(logging::Category::Core)], logging::Level::Error);
	EXPECT_EQ(merged.config.categories[static_cast<std::size_t>(logging::Category::Workflow)], logging::Level::Info);
	EXPECT_EQ(merged.config.file.path, std::filesystem::path("cli.log"));
	EXPECT_EQ(merged.config.console.color, logging::ColorMode::Never);
}

TEST_CASE(logging_config_preserves_compiled_category_defaults_without_level_override)
{
	const auto merged = logging::merge_config(logging::Config{}, {}, {});
	EXPECT_TRUE(merged.ok);
	EXPECT_EQ(merged.config.categories[static_cast<std::size_t>(logging::Category::Solver)], logging::Level::Warning);
	EXPECT_EQ(merged.config.categories[static_cast<std::size_t>(logging::Category::Storage)], logging::Level::Debug);
	EXPECT_EQ(merged.config.categories[static_cast<std::size_t>(logging::Category::Performance)], logging::Level::Off);
}

TEST_CASE(logging_config_cli_options_and_runtime_controls)
{
	char option0[] = "workflow";
	char option1[] = "--log-level";
	char option2[] = "warning";
	char option3[] = "--no-console-log";
	char option4[] = "--quiet";
	char* arguments[] = { option0, option1, option2, option3, option4 };
	logging::BootstrapError error;
	const auto overrides = logging::scan_logging_options(5, arguments, error);
	EXPECT_TRUE(error.reason.empty());
	EXPECT_EQ(overrides.level, std::string("warning"));
	EXPECT_TRUE(overrides.no_console);
	EXPECT_TRUE(overrides.quiet);
	const auto merged = logging::merge_config(logging::Config{}, {}, overrides);
	EXPECT_TRUE(merged.ok);
	EXPECT_TRUE(!merged.config.console.enabled);
	EXPECT_TRUE(!merged.config.progress.enabled);

	logging::EnvironmentMap environment{ { "TEXASSOLVER_LOG_CONFIG", "environment.toml" }, { "NO_COLOR", "1" } };
	logging::Overrides config_override;
	EXPECT_EQ(logging::select_config_path(environment, config_override), std::filesystem::path("environment.toml"));
	config_override.config_path = "command-line.toml";
	EXPECT_EQ(logging::select_config_path(environment, config_override), std::filesystem::path("command-line.toml"));
	const auto no_color = logging::merge_config(logging::Config{}, environment, {});
	EXPECT_TRUE(no_color.ok);
	EXPECT_EQ(no_color.config.console.color, logging::ColorMode::Never);

	logging::Overrides no_log;
	no_log.no_log = true;
	const auto disabled = logging::merge_config(logging::Config{}, {}, no_log);
	EXPECT_TRUE(disabled.ok);
	EXPECT_TRUE(!disabled.config.enabled);
	EXPECT_TRUE(!disabled.config.file.enabled);
}
