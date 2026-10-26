#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace texas::logging
{
	class Progress
	{
	public:
		static Progress start(std::string_view label, std::uint64_t total = 0U) noexcept;
		Progress() noexcept = default;
		~Progress();
		Progress(Progress&& other) noexcept;
		Progress& operator=(Progress&& other) noexcept;
		Progress(const Progress&) = delete;
		Progress& operator=(const Progress&) = delete;
		Progress& update(std::uint64_t completed) noexcept;
		Progress& field(std::string_view name, std::uint64_t value) noexcept;
		Progress& field(std::string_view name, std::string_view value) noexcept;
		Progress& rate(double per_second) noexcept;
		void finish(std::string_view message = {}) noexcept;
	private:
		explicit Progress(std::uint64_t id) noexcept : id_(id) {}
		std::uint64_t id_ = 0U;
	};
}
