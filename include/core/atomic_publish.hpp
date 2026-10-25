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

	enum class AtomicPublishFallbackPolicy : std::uint8_t
	{
		AllowCopyFallback = 0U,
		RejectCopyFallback,
		RejectNonAtomicFallback
	};

	[[nodiscard]] const char* atomic_publish_mode_name(AtomicPublishMode mode) noexcept;

	AtomicPublishMode publish_atomic_replace(
		const std::filesystem::path& temporary,
		const std::filesystem::path& destination,
		const char* failure_message,
		AtomicPublishFallbackPolicy fallback_policy = AtomicPublishFallbackPolicy::AllowCopyFallback);

} // namespace texas::core
