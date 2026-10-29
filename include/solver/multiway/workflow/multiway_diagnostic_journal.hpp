#pragma once

#include <cstdint>
#include <filesystem>
#include <string_view>

namespace texas::solver::multiway
{
	// Append-only diagnostic records. A complete line is durable on return.
	// A termination during append may leave an incomplete final line.
	class MultiwayDiagnosticJournal
	{
	public:
		explicit MultiwayDiagnosticJournal(const std::filesystem::path& path);
		~MultiwayDiagnosticJournal();
		MultiwayDiagnosticJournal(const MultiwayDiagnosticJournal&) = delete;
		MultiwayDiagnosticJournal& operator=(const MultiwayDiagnosticJournal&) = delete;
		void append(std::string_view record);

	private:
		std::intptr_t file_ = -1;
		bool failed_ = false;
	};
} // namespace texas::solver::multiway
