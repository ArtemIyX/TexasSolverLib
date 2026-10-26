#include "logging/log.hpp"
#include "logging/progress.hpp"
#include "test_harness.hpp"

#include <chrono>
#include <algorithm>
#include <sstream>
#include <thread>
#include <vector>

namespace
{
    namespace logging = texas::logging;

    logging::RuntimeDependencies dependencies(std::ostream& stream, bool interactive,
        std::chrono::steady_clock::time_point& now)
    {
        logging::RuntimeDependencies value;
        value.console_stream = &stream;
        value.terminal_known = true;
        value.interactive_terminal = interactive;
        value.now = [&now] { return now; };
        return value;
    }
}

TEST_CASE(logging_progress_live_redraw_throttle_and_final_line)
{
    std::ostringstream output;
    auto now = std::chrono::steady_clock::time_point(std::chrono::seconds(1));
    logging::Config config;
    config.console.pattern = "%v";
    config.progress.mode = logging::ProgressMode::Live;
    config.progress.refresh_ms = 100U;
    config.progress.show_elapsed = false;
    EXPECT_TRUE(logging::initialize(config, dependencies(output, true, now)).ok);
    auto progress = logging::Progress::start("train", 10U);
    progress.field("workers", "12345").update(1U);
    const auto first_size = output.str().size();
    now += std::chrono::milliseconds(50);
    progress.update(2U);
    EXPECT_EQ(output.str().size(), first_size);
    progress.field("workers", "1");
    now += std::chrono::milliseconds(100);
    progress.update(3U);
    TEXAS_LOG_INFO(Core, "ordinary-record");
    progress.finish("done");
    logging::shutdown();
    const auto text = output.str();
    EXPECT_TRUE(text.find("ordinary-record") != std::string::npos);
    EXPECT_TRUE(text.find("done") != std::string::npos);
    EXPECT_TRUE(text.find("\r") != std::string::npos);
    EXPECT_TRUE(text.find("     ") != std::string::npos);
    EXPECT_TRUE(!text.empty() && text.back() == '\n');
}

TEST_CASE(logging_progress_lines_disabled_and_display_toggles)
{
    std::ostringstream output;
    auto now = std::chrono::steady_clock::time_point(std::chrono::seconds(1));
    logging::Config config;
    config.console.enabled = false;
    config.progress.mode = logging::ProgressMode::Automatic;
    config.progress.line_interval = 2U;
    config.progress.show_elapsed = false;
    config.progress.show_eta = false;
    config.progress.show_rate = false;
    EXPECT_TRUE(logging::initialize(config, dependencies(output, false, now)).ok);
    auto progress = logging::Progress::start("batch", 4U);
    progress.rate(2.0).update(1U);
    EXPECT_TRUE(output.str().empty());
    progress.update(2U);
    progress.finish();
    logging::shutdown();
    EXPECT_TRUE(output.str().find("batch: 2/4") != std::string::npos);
    EXPECT_TRUE(output.str().find("elapsed=") == std::string::npos);
    EXPECT_TRUE(output.str().find("rate=") == std::string::npos);
    EXPECT_TRUE(output.str().find("eta=") == std::string::npos);

    output.str({});
    config.progress.enabled = false;
    EXPECT_TRUE(logging::initialize(config, dependencies(output, false, now)).ok);
    logging::Progress::start("hidden", 1U).update(1U);
    logging::shutdown();
    EXPECT_TRUE(output.str().empty());
}

TEST_CASE(logging_progress_off_mode_emits_no_records)
{
    std::ostringstream output;
    auto now = std::chrono::steady_clock::time_point(std::chrono::seconds(1));
    logging::Config config;
    config.progress.mode = logging::ProgressMode::Off;
    EXPECT_TRUE(logging::initialize(config, dependencies(output, false, now)).ok);
    auto progress = logging::Progress::start("hidden", 1U);
    progress.update(1U).finish();
    logging::shutdown();
    EXPECT_TRUE(output.str().empty());
}

TEST_CASE(logging_progress_and_concurrent_records_are_complete)
{
    std::ostringstream output;
    auto now = std::chrono::steady_clock::time_point(std::chrono::seconds(1));
    logging::Config config;
    config.console.pattern = "%v";
    config.progress.mode = logging::ProgressMode::Lines;
    config.progress.line_interval = 100U;
    config.categories.fill(logging::Level::Trace);
    EXPECT_TRUE(logging::initialize(config, dependencies(output, false, now)).ok);
    auto progress = logging::Progress::start("work", 100U);
    std::vector<std::thread> threads;
    for (int index = 0; index < 4; ++index)
        threads.emplace_back([index] { TEXAS_LOG_INFO(Core, "record-{}", index); });
    for (auto& thread : threads)
        thread.join();
    progress.finish();
    logging::shutdown();
    const auto text = output.str();
    const auto count_records = [&text](const std::string& record) {
        std::size_t count = 0U;
        std::istringstream lines(text);
        std::string line;
        while (std::getline(lines, line))
        {
            if (!line.empty() && line.back() == '\r')
                line.pop_back();
            if (line == record)
                ++count;
        }
        return count;
    };
    for (int index = 0; index < 4; ++index)
    {
        const auto record = "record-" + std::to_string(index);
        EXPECT_EQ(count_records(record), 1U);
    }
    EXPECT_TRUE(text.find("work: 0/100") != std::string::npos);
}
