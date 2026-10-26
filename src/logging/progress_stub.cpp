#include "logging/progress.hpp"

namespace texas::logging
{
	Progress Progress::start(std::string_view, std::uint64_t) noexcept { return {}; }
	Progress::~Progress() = default;
	Progress::Progress(Progress&& other) noexcept : id_(other.id_) { other.id_=0U; }
	Progress& Progress::operator=(Progress&& other) noexcept { id_=other.id_; other.id_=0U; return *this; }
	Progress& Progress::update(std::uint64_t) noexcept { return *this; }
	Progress& Progress::field(std::string_view, std::uint64_t) noexcept { return *this; }
	Progress& Progress::field(std::string_view, std::string_view) noexcept { return *this; }
	Progress& Progress::rate(double) noexcept { return *this; }
	void Progress::finish(std::string_view) noexcept {}
}
