#include "logging/log.hpp"
#include "test_harness.hpp"

#include <atomic>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace
{
	namespace logging = texas::logging;

	std::filesystem::path temporary_file(const char* name)
	{
		static std::atomic<unsigned> counter{ 0U };
		const auto directory = std::filesystem::temp_directory_path() / ("texassolver_logging_" + std::to_string(counter.fetch_add(1U)));
		std::filesystem::remove_all(directory);
		std::filesystem::create_directories(directory);
		return directory / name;
	}

	std::string read_file(const std::filesystem::path& path)
	{
		std::ifstream input(path, std::ios::binary);
		return { std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>() };
	}

	std::filesystem::path rotated_file(const std::filesystem::path& path, unsigned index)
	{
#if defined(_WIN32)
		return path.parent_path() / (path.stem().string() + "." + std::to_string(index) + path.extension().string());
#else
		return path.string() + "." + std::to_string(index);
#endif
	}

	logging::RuntimeDependencies dependencies(std::ostream& stream, bool interactive = false)
	{
		logging::RuntimeDependencies value;
		value.console_stream = &stream;
		value.terminal_known = true;
		value.interactive_terminal = interactive;
		return value;
	}
} // namespace

TEST_CASE(logging_fans_out_and_filters_by_sink_and_category)
{
	std::ostringstream console;
	logging::Config config;
	config.console.pattern = "%v";
	config.console.level = logging::Level::Info;
	config.file.enabled = true;
	config.file.path = temporary_file("run.log");
	config.file.pattern = "%v";
	config.file.level = logging::Level::Debug;
	config.categories.fill(logging::Level::Debug);
	EXPECT_TRUE(logging::initialize(config, dependencies(console)).ok);
	TEXAS_LOG_DEBUG(Core, "debug-record");
	TEXAS_LOG_INFO(Core, "info-record");
	logging::flush();
	logging::shutdown();
	EXPECT_TRUE(console.str().find("debug-record") == std::string::npos);
	EXPECT_TRUE(console.str().find("info-record") != std::string::npos);
	const auto file = read_file(config.file.path);
	EXPECT_TRUE(file.find("debug-record") != std::string::npos);
	EXPECT_TRUE(file.find("info-record") != std::string::npos);
}

TEST_CASE(logging_appends_reinitializes_and_disables_at_runtime)
{
	std::ostringstream console;
	logging::Config config;
	config.console.pattern = "%v";
	config.file.enabled = true;
	config.file.path = temporary_file("append.log");
	config.file.pattern = "%v";
	config.categories.fill(logging::Level::Trace);
	EXPECT_TRUE(logging::initialize(config, dependencies(console)).ok);
	TEXAS_LOG_INFO(Core, "first");
	EXPECT_TRUE(logging::initialize(config, dependencies(console)).ok);
	TEXAS_LOG_INFO(Core, "second");
	logging::shutdown();
	const auto file = read_file(config.file.path);
	EXPECT_TRUE(file.find("first") != std::string::npos);
	EXPECT_TRUE(file.find("second") != std::string::npos);

	config.enabled = false;
	config.file.path = temporary_file("disabled.log");
	EXPECT_TRUE(logging::initialize(config, dependencies(console)).ok);
	TEXAS_LOG_ERROR(Core, "suppressed");
	logging::shutdown();
	EXPECT_TRUE(!std::filesystem::exists(config.file.path));
}

TEST_CASE(logging_runtime_disabled_does_not_evaluate_arguments)
{
	std::ostringstream console;
	logging::Config config;
	config.enabled = false;
	int side_effects = 0;
	EXPECT_TRUE(logging::initialize(config, dependencies(console)).ok);
	TEXAS_LOG_INFO(Core, "{}", ++side_effects);
	logging::shutdown();
	EXPECT_EQ(side_effects, 0);
}

TEST_CASE(logging_training_audit_is_plain_append_only_and_flushed)
{
	std::ostringstream console;
	const auto path = temporary_file("audit.log");
	logging::Config config;
	config.console.enabled = false;
	EXPECT_TRUE(logging::initialize(config, dependencies(console)).ok);
	EXPECT_TRUE(logging::configure_training_audit(path));
	logging::write_training_audit("first {plain} message");
	EXPECT_EQ(read_file(path), std::string("first {plain} message\n"));
	logging::shutdown();

	EXPECT_TRUE(logging::initialize(config, dependencies(console)).ok);
	EXPECT_TRUE(logging::configure_training_audit(path));
	logging::write_training_audit("second message");
	logging::shutdown();
	EXPECT_EQ(read_file(path), std::string("first {plain} message\nsecond message\n"));
}

TEST_CASE(logging_file_failures_and_file_output_have_no_ansi)
{
	std::ostringstream console;
	logging::Config config;
	config.console.pattern = "%v";
	config.console.color = logging::ColorMode::Always;
	config.file.enabled = true;
	config.file.path = temporary_file("color.log");
	config.file.pattern = "%v";
	config.categories.fill(logging::Level::Trace);
	EXPECT_TRUE(logging::initialize(config, dependencies(console, true)).ok);
	TEXAS_LOG_WARNING(Core, "colored-warning");
	logging::shutdown();
	EXPECT_TRUE(console.str().find("\x1b[") != std::string::npos);
	EXPECT_TRUE(read_file(config.file.path).find("\x1b[") == std::string::npos);

	auto failure = dependencies(console);
	failure.fail_file_sink = true;
	config.file.required = false;
	EXPECT_TRUE(logging::initialize(config, failure).ok);
	logging::shutdown();
	config.file.required = true;
	EXPECT_TRUE(!logging::initialize(config, failure).ok);
	logging::shutdown();
}

TEST_CASE(logging_no_color_and_redirected_always_never_emit_ansi)
{
	std::ostringstream console;
	logging::Config config;
	config.console.pattern = "%v";
	config.categories.fill(logging::Level::Trace);
	const auto no_color = logging::merge_config(config, { { "NO_COLOR", "1" } }, {});
	EXPECT_TRUE(no_color.ok);
	EXPECT_TRUE(logging::initialize(no_color.config, dependencies(console, true)).ok);
	TEXAS_LOG_WARNING(Core, "automatic-no-color");
	logging::shutdown();
	EXPECT_TRUE(console.str().find("\x1b[") == std::string::npos);

	console.str({});
	config.console.color = logging::ColorMode::Always;
	EXPECT_TRUE(logging::initialize(config, dependencies(console, false)).ok);
	TEXAS_LOG_WARNING(Core, "redirected-always");
	logging::shutdown();
	EXPECT_TRUE(console.str().find("\x1b[") == std::string::npos);
}

TEST_CASE(logging_rotates_and_honors_category_off)
{
	std::ostringstream console;
	logging::Config config;
	config.console.enabled = false;
	config.file.enabled = true;
	config.file.path = temporary_file("rotate.log");
	config.file.pattern = "%v";
	config.file.rotate_size_mb = 1U;
	config.file.rotate_files = 2U;
	config.categories.fill(logging::Level::Trace);
	config.categories[static_cast<std::size_t>(logging::Category::Performance)] = logging::Level::Off;
	EXPECT_TRUE(logging::initialize(config, dependencies(console)).ok);
	TEXAS_LOG_INFO(Performance, "not-written");
	const std::string payload(1024U * 1024U, 'x');
	TEXAS_LOG_INFO(Core, "{}", payload);
	TEXAS_LOG_INFO(Core, "{}", payload);
	TEXAS_LOG_INFO(Core, "{}", payload);
	logging::shutdown();
	EXPECT_TRUE(read_file(config.file.path).find("not-written") == std::string::npos);
	EXPECT_TRUE(std::filesystem::exists(rotated_file(config.file.path, 1U)));
	EXPECT_TRUE(std::filesystem::exists(rotated_file(config.file.path, 2U)));
	EXPECT_TRUE(!std::filesystem::exists(rotated_file(config.file.path, 3U)));
}
