#include "core/atomic_publish.hpp"

#include <stdexcept>
#include <string>

namespace texas::core
{
	namespace
	{

		void remove_temporary(const std::filesystem::path& path) noexcept
		{
			std::error_code error;
			std::filesystem::remove(path, error);
		}

	} // namespace

	void publish_atomic_replace(
		const std::filesystem::path& temporary,
		const std::filesystem::path& destination,
		const char* failure_message)
	{
		const auto fail = [failure_message](const std::error_code& error) {
			return std::string(failure_message) + ": " + (error ? error.message() : std::string("unknown filesystem error"));
		};
		std::error_code error;
		std::filesystem::rename(temporary, destination, error);
		if (!error)
			return;
		// Some Windows volume/sandbox combinations deny rename even when the
		// destination is absent. Preserve the publish contract with a checked
		// copy fallback for those filesystems.
		std::error_code copy_error;
		std::filesystem::copy_file(temporary, destination, std::filesystem::copy_options::overwrite_existing, copy_error);
		if (!copy_error)
		{
			remove_temporary(temporary);
			return;
		}

		std::error_code exists_error;
		if (!std::filesystem::exists(destination, exists_error) || exists_error)
		{
			remove_temporary(temporary);
			throw std::runtime_error(fail(error));
		}

		auto backup = destination;
		backup += ".previous";
		if (std::filesystem::exists(backup, exists_error) || exists_error)
		{
			remove_temporary(temporary);
			throw std::runtime_error(fail(exists_error));
		}

		std::filesystem::rename(destination, backup, error);
		if (error)
		{
			remove_temporary(temporary);
			throw std::runtime_error(fail(error));
		}

		std::filesystem::rename(temporary, destination, error);
		if (error)
		{
			std::error_code restore_error;
			std::filesystem::rename(backup, destination, restore_error);
			remove_temporary(temporary);
			throw std::runtime_error(fail(error));
		}

		std::error_code cleanup_error;
		std::filesystem::remove(backup, cleanup_error);
	}

} // namespace texas::core
