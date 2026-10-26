#include "logging/log.hpp"

namespace texas::logging
{
	InitializeResult initialize(const Config&, const RuntimeDependencies&) noexcept { return {}; }
	void flush() noexcept {}
	void shutdown() noexcept {}
	bool should_log(Category, Level) noexcept { return false; }
	void write_text(Category, Level, std::string_view, int, std::string_view) noexcept {}
	bool configure_training_audit(const std::filesystem::path&) noexcept { return true; }
	void write_training_audit(std::string_view) noexcept {}
	LoggingSession::LoggingSession(const Config& config) noexcept { const auto result=initialize(config); ok_=result.ok; message_=result.message; }
	LoggingSession::~LoggingSession() { shutdown(); }
}
