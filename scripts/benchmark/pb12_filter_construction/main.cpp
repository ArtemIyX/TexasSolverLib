#include "solver/multiway/engine/multiway_external_storage.hpp"
#include "core/fingerprint.hpp"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>
#include <windows.h>

static void append_u64(std::vector<std::uint8_t>& data, std::uint64_t value, unsigned bytes)
{
	for (unsigned shift = 0; shift < bytes * 8U; shift += 8U)
		data.push_back(static_cast<std::uint8_t>(value >> shift));
}

static void write_file(const std::filesystem::path& path, const std::vector<std::uint8_t>& data)
{
	std::ofstream stream(path, std::ios::binary | std::ios::trunc);
	stream.write(reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(data.size()));
	stream.close();
	if (!stream) throw std::runtime_error("synthetic file write failed");
}

static void create_singleton_runs(const std::filesystem::path& directory, std::uint64_t count,
	std::size_t payload_bytes)
{
	using namespace texas::core::fingerprint;
	std::vector<std::uint8_t> manifest{ 'T','X','M','W','E','X','T','1' };
	manifest.reserve(static_cast<std::size_t>(48U + 24U * count));
	append_u64(manifest, 1U, 4U);
	append_u64(manifest, payload_bytes, 4U);
	append_u64(manifest, count, 8U);
	append_u64(manifest, count, 8U);
	append_u64(manifest, count, 8U);
	for (std::uint64_t i = 0U; i < count; ++i)
	{
		const auto key = 1U + (i % 7016U) * 23U;
		std::vector<std::uint8_t> index, data;
		index.reserve(8U);
		data.reserve(payload_bytes + 8U);
		append_u64(index, key, 8U);
		append_u64(data, key, 8U);
		data.resize(payload_bytes + 8U, static_cast<std::uint8_t>(i));
		auto hash = FNV1A_OFFSET;
		for (auto byte : index) append_u8(hash, byte);
		for (auto byte : data) append_u8(hash, byte);
		const auto prefix = "run_" + std::to_string(i + 1U);
		write_file(directory / (prefix + ".idx"), index);
		write_file(directory / (prefix + ".dat"), data);
		append_u64(manifest, i + 1U, 8U);
		append_u64(manifest, 1U, 8U);
		append_u64(manifest, finish(hash), 8U);
	}
	auto hash = FNV1A_OFFSET;
	for (auto byte : manifest) append_u8(hash, byte);
	append_u64(manifest, hash, 8U);
	write_file(directory / "CURRENT", manifest);
}

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
		if (argc != 3 && argc != 4) throw std::invalid_argument("expected empty output directory, record count and optional singleton mode");
		const std::filesystem::path directory(argv[1]);
		const auto count = std::stoull(argv[2]);
		const bool singleton = argc == 4 && std::string(argv[3]) == "singleton";
		if ((argc == 4 && !singleton) || count == 0U || count > 1000000U ||
			(singleton ? count > 8321U : count % 1000U != 0U) ||
			!std::filesystem::is_directory(directory) || !std::filesystem::is_empty(directory))
			throw std::invalid_argument("record count or output directory is invalid");
		constexpr std::size_t payload_bytes = 1584U; // 16 buckets and eight actions.
		constexpr std::uint64_t batch_size = 1000U;
		MultiwayExternalStorageConfig config{ directory, 4096U, 8U * 1024U * 1024U,
			8U * 1024U * 1024U, 2U, 65536U, 1536ULL * 1024U * 1024U, false };
		const auto create_start = std::chrono::steady_clock::now();
		if (singleton)
			create_singleton_runs(directory, count, payload_bytes);
		else
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
		const auto open_start = std::chrono::steady_clock::now();
		MultiwayExternalRecordStore reopened(config, payload_bytes);
		const auto open_end = std::chrono::steady_clock::now();
		const auto wall_start = std::chrono::steady_clock::now();
		const auto cpu_start = process_cpu_100ns();
		reopened.begin_export_filter();
		const auto cpu_end = process_cpu_100ns();
		const auto wall_end = std::chrono::steady_clock::now();
		MultiwayExternalRecordLookupProfile profile;
		std::vector<std::uint8_t> output;
		for (std::uint64_t i = 0U; i < 10000U; ++i)
		{
			const auto key = singleton ? (1000000U + i * 2U) : ((i * 7919U) % count) * 2U + 1U;
			if (reopened.lookup_profiled(key, output, profile))
				throw std::runtime_error("absent key unexpectedly found");
		}
		const std::uint64_t first_key = singleton ? 1U : 0U;
		const std::uint64_t last_key = singleton ? 1U + (std::min<std::uint64_t>(count, 7016U) - 1U) * 23U : (count - 1U) * 2U;
		for (const auto key : { first_key, last_key })
			if (!reopened.lookup(key, output)) throw std::runtime_error("present key missing");
		reopened.end_export_filter();
		const auto wall_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(wall_end - wall_start).count();
		std::cout << "PB12_CONSTRUCTION records=" << count << " runs=" << (singleton ? count : count / batch_size)
			<< " payload_bytes=" << payload_bytes << " data_bytes=" << count * (payload_bytes + 8U)
			<< " index_bytes=" << count * 8U << " build_ns=" << wall_ns
			<< " create_ns=" << std::chrono::duration_cast<std::chrono::nanoseconds>(open_start - create_start).count()
			<< " reopen_ns=" << std::chrono::duration_cast<std::chrono::nanoseconds>(open_end - open_start).count()
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
