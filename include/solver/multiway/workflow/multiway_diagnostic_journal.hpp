#pragma once

/** @file
 * Append-only journal for complete newline-delimited diagnostic records.
 */

#include <cstdint>
#include <filesystem>
#include <string_view>

namespace texas::solver::multiway
{
	// Append-only diagnostic records. A complete line is durable on return.
	// A termination during append may leave an incomplete final line.
	/** Owns an append-only diagnostic file opened for durable record writes. */
	class MultiwayDiagnosticJournal
	{
	public:
		/** Opens or creates the journal at path. */
		explicit MultiwayDiagnosticJournal(const std::filesystem::path& path);
		/** Closes the journal handle. */
		~MultiwayDiagnosticJournal();
		MultiwayDiagnosticJournal(const MultiwayDiagnosticJournal&) = delete;
		MultiwayDiagnosticJournal& operator=(const MultiwayDiagnosticJournal&) = delete;
		/** Appends one complete record and newline. */
		void append(std::string_view record);

	private:
		std::intptr_t file_ = -1;
		bool failed_ = false;
	};
} // namespace texas::solver::multiway
