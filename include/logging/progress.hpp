#pragma once

/** @file
 * @brief Move-only RAII progress reporting with numeric and text fields.
 */

#include <cstdint>
#include <string>
#include <string_view>

namespace texas::logging
{
	/** @brief Move-only progress scope that reports updates through logging. */
	class Progress
	{
	public:
		/** @brief Starts a progress display.
		 * @param label Display label.
		 * @param total Total work units, or zero when unknown.
		 */
		static Progress start(std::string_view label, std::uint64_t total = 0U) noexcept;
		/** @brief Creates an inactive progress object. */
		Progress() noexcept = default;
		/** @brief Finishes an active progress display when this object owns it. */
		~Progress();
		/** @brief Transfers progress-display ownership from other. */
		Progress(Progress&& other) noexcept;
		/** @brief Finishes the current display and takes ownership from other. */
		Progress& operator=(Progress&& other) noexcept;
		Progress(const Progress&) = delete;
		Progress& operator=(const Progress&) = delete;
		/** @brief Updates completed work units and returns this object. */
		Progress& update(std::uint64_t completed) noexcept;
		/** @brief Adds or updates a numeric display field. */
		Progress& field(std::string_view name, std::uint64_t value) noexcept;
		/** @brief Adds or updates a text display field. */
		Progress& field(std::string_view name, std::string_view value) noexcept;
		/** @brief Supplies a work rate in units per second. */
		Progress& rate(double per_second) noexcept;
		/** @brief Ends this display with an optional final message. */
		void finish(std::string_view message = {}) noexcept;

	private:
		explicit Progress(std::uint64_t id) noexcept : id_(id) {}
		std::uint64_t id_ = 0U;
	};
} // namespace texas::logging
