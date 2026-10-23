#include "core/atomic_publish.hpp"

#ifdef _WIN32
	#ifndef NOMINMAX
		#define NOMINMAX
	#endif
	#include <windows.h>
#endif

#include <stdexcept>
#include <string>
#include <system_error>

namespace texas::core
{
	namespace
	{

		void remove_temporary(const std::filesystem::path& path) noexcept
		{
			std::error_code error;
			std::filesystem::remove(path, error);
		}

#ifdef _WIN32
		bool windows_replace(
			const std::filesystem::path& temporary,
			const std::filesystem::path& destination,
			bool destination_exists,
			std::error_code& error) noexcept
		{
			BOOL replaced = FALSE;
			if (destination_exists)
			{
				replaced = ::ReplaceFileW(
					destination.c_str(), temporary.c_str(), nullptr,
					REPLACEFILE_WRITE_THROUGH, nullptr, nullptr);
			}
			else
			{
				replaced = ::MoveFileExW(
					temporary.c_str(), destination.c_str(),
					MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH);
			}
			if (replaced != FALSE)
				return true;
			error = std::error_code(static_cast<int>(::GetLastError()), std::system_category());
			return false;
		}
#endif

	} // namespace

	const char* atomic_publish_mode_name(AtomicPublishMode mode) noexcept
	{
		switch (mode)
		{
			case AtomicPublishMode::Rename:
				return "rename";
			case AtomicPublishMode::WindowsReplace:
				return "windows_replace";
			case AtomicPublishMode::CopyFallback:
				return "copy_fallback";
			case AtomicPublishMode::BackupRename:
				return "backup_rename";
			case AtomicPublishMode::Unknown:
			default:
				return "unknown";
		}
	}

	AtomicPublishMode publish_atomic_replace(
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
			return AtomicPublishMode::Rename;

		std::error_code exists_error;
		const auto destination_exists = std::filesystem::exists(destination, exists_error);
		if (exists_error)
		{
			remove_temporary(temporary);
			throw std::runtime_error(fail(exists_error));
		}

#ifdef _WIN32
		std::error_code windows_error;
		if (windows_replace(temporary, destination, destination_exists, windows_error))
			return AtomicPublishMode::WindowsReplace;
#endif

		if (!destination_exists)
		{
			// Some Windows volume/sandbox combinations deny both rename and the
			// checked replacement operation. Preserve the publish contract with
			// one checked copy when no destination can be replaced.
			std::error_code copy_error;
			std::filesystem::copy_file(temporary, destination, std::filesystem::copy_options::overwrite_existing, copy_error);
			if (!copy_error)
			{
				remove_temporary(temporary);
				return AtomicPublishMode::CopyFallback;
			}
			remove_temporary(temporary);
			throw std::runtime_error(fail(copy_error));
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
		return AtomicPublishMode::BackupRename;
	}

} // namespace texas::core
