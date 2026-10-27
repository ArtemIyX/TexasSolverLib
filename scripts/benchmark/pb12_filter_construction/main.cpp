#include "solver/multiway/engine/multiway_external_storage.hpp"

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>
#include <windows.h>

static std::uint64_t process_cpu_100ns()
{
	FILETIME creation{}, exit{}, kernel{}, user{};
	if (!GetProcessTimes(GetCurrentProcess(), &creation, &exit, &kernel, &user))
		throw std::runtime_error("process CPU clock unavailable");
	ULARGE_INTEGER k{}, u{};
	k.LowPart = kernel.dwLowDateTime; k.HighPart = kernel.dwHighDateTime;
	u.LowPart = user.dwLowDateTime; u.HighPart = user.dwHighDateTime;
	return k.QuadPart + u.QuadPart;
}

int main(int argc, char** argv)
{
	using namespace texas::solver::multiway;
	try
	{
		if (argc != 3) throw std::invalid_argument("expected empty output directory and record count");
		const std::filesystem::path directory(argv[1]);
		const auto count = std::stoull(argv[2]);
		if (count == 0U || count > 1000000U || count % 1000U != 0U ||
			!std::filesystem::is_directory(directory) || !std::filesystem::is_empty(directory))
			throw std::invalid_argument("record count or output directory is invalid");
		constexpr std::size_t payload_bytes = 1584U; // 16 buckets and eight actions.
		constexpr std::uint64_t batch_size = 1000U;
		MultiwayExternalStorageConfig config{ directory, 4096U, 8U * 1024U * 1024U,
			8U * 1024U * 1024U, 2U, 65536U, 1536ULL * 1024U * 1024U, false };
		{
			MultiwayExternalRecordStore store(config, payload_bytes);
			std::vector<MultiwayExternalRecord> records;
			records.reserve(batch_size);
			for (std::uint64_t base = 0U; base < count; base += batch_size)
			{
				records.clear();
				for (std::uint64_t i = 0U; i < batch_size; ++i)
					records.push_back({ (base + i) * 2U, std::vector<std::uint8_t>(payload_bytes,
						static_cast<std::uint8_t>(base + i)) });
				store.append_batch(base / batch_size + 1U, base / batch_size + 1U, records);
			}
		}
		MultiwayExternalRecordStore reopened(config, payload_bytes);
		const auto wall_start = std::chrono::steady_clock::now();
		const auto cpu_start = process_cpu_100ns();
		reopened.begin_export_filter();
		const auto cpu_end = process_cpu_100ns();
		const auto wall_end = std::chrono::steady_clock::now();
		MultiwayExternalRecordLookupProfile profile;
		std::vector<std::uint8_t> output;
		for (std::uint64_t i = 0U; i < 10000U; ++i)
		{
			const auto key = ((i * 7919U) % count) * 2U + 1U;
			if (reopened.lookup_profiled(key, output, profile))
				throw std::runtime_error("absent key unexpectedly found");
		}
		for (const auto key : { 0ULL, (count - 1U) * 2U })
			if (!reopened.lookup(key, output)) throw std::runtime_error("present key missing");
		reopened.end_export_filter();
		const auto wall_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(wall_end - wall_start).count();
		std::cout << "PB12_CONSTRUCTION records=" << count << " runs=" << count / batch_size
			<< " payload_bytes=" << payload_bytes << " data_bytes=" << count * (payload_bytes + 8U)
			<< " index_bytes=" << count * 8U << " build_ns=" << wall_ns
			<< " build_cpu_ns=" << (cpu_end - cpu_start) * 100U
			<< " absent_queries=10000 filter_negatives=" << profile.membership_misses
			<< " false_positive_queries=" << (10000U - profile.membership_misses)
			<< " index_opens=" << profile.index_opens << " index_reads=" << profile.index_reads << '\n';
		return 0;
	}
	catch (const std::exception& error)
	{
		std::cerr << error.what() << '\n';
		return 1;
	}
}
