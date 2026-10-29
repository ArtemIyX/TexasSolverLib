#include "solver/multiway/workflow/multiway_diagnostic_journal.hpp"

#include <algorithm>
#include <limits>
#include <stdexcept>

#ifdef _WIN32
	#ifndef NOMINMAX
		#define NOMINMAX
	#endif
	#include <windows.h>
#else
	#include <cerrno>
	#include <fcntl.h>
	#include <unistd.h>
#endif

namespace texas::solver::multiway
{
	MultiwayDiagnosticJournal::MultiwayDiagnosticJournal(const std::filesystem::path& path)
	{
#ifdef _WIN32
		const auto handle = CreateFileW(path.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr,
			CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
		if (handle == INVALID_HANDLE_VALUE)
			throw std::runtime_error("diagnostic journal cannot be created exclusively");
		file_ = reinterpret_cast<std::intptr_t>(handle);
#else
		const auto descriptor = open(path.c_str(), O_WRONLY | O_CREAT | O_EXCL, 0600);
		if (descriptor < 0)
			throw std::runtime_error("diagnostic journal cannot be created exclusively");
		file_ = descriptor;
		const auto parent = path.parent_path().empty() ? std::filesystem::path(".") : path.parent_path();
		const auto directory = open(parent.c_str(), O_RDONLY | O_DIRECTORY);
		if (directory < 0 || fsync(directory) != 0)
		{
			if (directory >= 0)
				close(directory);
			close(descriptor);
			file_ = -1;
			throw std::runtime_error("diagnostic journal directory flush failed");
		}
		close(directory);
#endif
	}

	MultiwayDiagnosticJournal::~MultiwayDiagnosticJournal()
	{
#ifdef _WIN32
		if (file_ != -1)
			CloseHandle(reinterpret_cast<HANDLE>(file_));
#else
		if (file_ != -1)
			close(static_cast<int>(file_));
#endif
	}

	void MultiwayDiagnosticJournal::append(std::string_view record)
	{
		if (failed_)
			throw std::runtime_error("diagnostic journal is unavailable after a write failure");
		if (record.empty() || record.size() > 65536U || record.find_first_of("\r\n") != std::string_view::npos)
			throw std::invalid_argument("diagnostic journal record must be a bounded single line");
		const auto write_all = [this](const char* data, std::size_t size) {
			while (size != 0U)
			{
#ifdef _WIN32
				DWORD written = 0U;
				const auto chunk = static_cast<DWORD>(std::min<std::size_t>(size, std::numeric_limits<DWORD>::max()));
				if (!WriteFile(reinterpret_cast<HANDLE>(file_), data, chunk, &written, nullptr) || written == 0U)
					throw std::runtime_error("diagnostic journal write failed");
#else
				const auto written = write(static_cast<int>(file_), data, size);
				if (written < 0 && errno == EINTR)
					continue;
				if (written <= 0)
					throw std::runtime_error("diagnostic journal write failed");
#endif
				data += written;
				size -= static_cast<std::size_t>(written);
			}
		};
		try
		{
			write_all(record.data(), record.size());
			write_all("\n", 1U);
#ifdef _WIN32
			if (!FlushFileBuffers(reinterpret_cast<HANDLE>(file_)))
#else
			if (fsync(static_cast<int>(file_)) != 0)
#endif
				throw std::runtime_error("diagnostic journal flush failed");
		}
		catch (...)
		{
			failed_ = true;
			throw;
		}
	}
} // namespace texas::solver::multiway
