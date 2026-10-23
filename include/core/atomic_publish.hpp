#pragma once

#include <cstdint>
#include <filesystem>

namespace texas::core
{

	enum class AtomicPublishMode : std::uint8_t
	{
		Unknown = 0U,
		Rename,
		WindowsReplace,
		CopyFallback,
		BackupRename
	};

	[[nodiscard]] const char* atomic_publish_mode_name(AtomicPublishMode mode) noexcept;

	AtomicPublishMode publish_atomic_replace(
		const std::filesystem::path& temporary,
		const std::filesystem::path& destination,
		const char* failure_message);

} // namespace texas::core
