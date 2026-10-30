#pragma once

/** @file
 * @brief Declares filesystem replacement helpers that report the publish mechanism used.
 */

#include <cstdint>
#include <filesystem>

namespace texas::core
{

	/** @brief Identifies the filesystem operation that published a replacement. */
	enum class AtomicPublishMode : std::uint8_t
	{
		Unknown = 0U,	///< No recognized publish mechanism was reported.
		Rename,			///< The temporary path was renamed to the destination.
		WindowsReplace, ///< A Windows replacement operation published the temporary file.
		CopyFallback,	///< The temporary file was copied because no destination existed.
		BackupRename	///< The destination was backed up before the temporary file was renamed.
	};

	/** @brief Selects which fallback operations are permitted after rename fails. */
	enum class AtomicPublishFallbackPolicy : std::uint8_t
	{
		AllowCopyFallback = 0U, ///< Permit copy fallback when the destination does not exist.
		RejectCopyFallback,		///< Reject copy fallback but permit the backup-and-rename path.
		RejectNonAtomicFallback ///< Reject all fallback paths after native replacement fails.
	};

	/** @brief Returns the lowercase diagnostic name for a publish mode.
	 * @param mode Mode to describe; unrecognized values map to "unknown".
	 * @return A static string literal that remains valid for the program lifetime.
	 */
	[[nodiscard]] const char* atomic_publish_mode_name(AtomicPublishMode mode) noexcept;

	/** @brief Replaces a destination with a temporary file using available filesystem operations.
	 *
	 * The returned mode identifies the operation that succeeded. Fallback policy
	 * controls which non-native paths may be used after rename fails. On failure,
	 * the function removes the temporary file where its cleanup path permits and
	 * throws std::runtime_error with the supplied message and filesystem detail.
	 * @param temporary Existing file to publish; it may be removed on failure.
	 * @param destination Path to create or replace.
	 * @param failure_message Non-null prefix for a thrown failure message.
	 * @param fallback_policy Fallback operations permitted after the initial rename fails.
	 * @return The publish operation that completed.
	 * @throws std::runtime_error If the destination cannot be published under the selected policy.
	 */
	AtomicPublishMode publish_atomic_replace(
		const std::filesystem::path& temporary,
		const std::filesystem::path& destination,
		const char* failure_message,
		AtomicPublishFallbackPolicy fallback_policy = AtomicPublishFallbackPolicy::AllowCopyFallback);

} // namespace texas::core
