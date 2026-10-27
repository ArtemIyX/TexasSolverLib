#include "logging/progress.hpp"
#include "terminal.hpp"

#include <string>

namespace texas::logging
{
	Progress Progress::start(std::string_view label, std::uint64_t total) noexcept
	{
		return Progress(detail::begin_progress(label, total));
	}
	Progress::~Progress()
	{
		finish();
	}
	Progress::Progress(Progress&& other) noexcept : id_(other.id_)
	{
		other.id_ = 0U;
	}
	Progress& Progress::operator=(Progress&& other) noexcept
	{
		if (this != &other)
		{
			finish();
			id_ = other.id_;
			other.id_ = 0U;
		}
		return *this;
	}
	Progress& Progress::update(std::uint64_t completed) noexcept
	{
		detail::update_progress(id_, completed);
		return *this;
	}
	Progress& Progress::field(std::string_view name, std::uint64_t value) noexcept
	{
		detail::add_progress_field(id_, name, std::to_string(value));
		return *this;
	}
	Progress& Progress::field(std::string_view name, std::string_view value) noexcept
	{
		detail::add_progress_field(id_, name, value);
		return *this;
	}
	Progress& Progress::rate(double value) noexcept
	{
		detail::set_progress_rate(id_, value);
		return *this;
	}
	void Progress::finish(std::string_view message) noexcept
	{
		if (id_ != 0U)
		{
			detail::finish_progress(id_, message);
			id_ = 0U;
		}
	}
} // namespace texas::logging
