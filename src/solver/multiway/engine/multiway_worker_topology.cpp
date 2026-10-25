#include "solver/multiway/engine/multiway_worker_topology.hpp"

#include <algorithm>
#include <stdexcept>
#include <thread>
#include <utility>

#if defined(_WIN32)
	#ifndef NOMINMAX
		#define NOMINMAX
	#endif
	#include <windows.h>
#endif

namespace texas::solver::multiway
{

	namespace
	{
		constexpr std::uint8_t kCpuSetParkedFlag = 0x01U;
		constexpr std::uint8_t kCpuSetAllocatedFlag = 0x02U;

		[[nodiscard]] bool is_known(std::uint32_t value) noexcept
		{
			return value != kMultiwayUnknownTopologyId;
		}

		[[nodiscard]] bool is_known(std::uint16_t value) noexcept
		{
			return value != kMultiwayUnknownTopologyIndex;
		}

		void validate_and_sort(std::vector<MultiwayWorkerTopologyRecord>& records)
		{
			std::sort(records.begin(), records.end(), [](const auto& left, const auto& right) {
				return left.logical_processor_identity() < right.logical_processor_identity();
			});
			for (std::size_t index = 1U; index < records.size(); ++index)
			{
				if (records[index - 1U].logical_processor_identity() == records[index].logical_processor_identity())
					throw std::invalid_argument("multiway topology contains duplicate logical processor identities");
			}

			for (std::size_t index = 0U; index < records.size(); ++index)
			{
				if (!is_known(records[index].cpu_set_id))
					continue;
				for (std::size_t prior = 0U; prior < index; ++prior)
				{
					if (records[prior].cpu_set_id == records[index].cpu_set_id)
						throw std::invalid_argument("multiway topology contains duplicate CPU-set identities");
				}
			}
		}

#if defined(_WIN32)
		struct WindowsCorePlacement
		{
			std::uint16_t processor_group = 0U;
			std::uint16_t logical_processor_index = 0U;
			std::uint32_t physical_core_id = kMultiwayUnknownTopologyId;
			std::uint16_t smt_sibling_index = kMultiwayUnknownTopologyIndex;
		};

		[[nodiscard]] std::uint32_t make_windows_physical_core_id(
			std::uint16_t group, std::uint16_t core_index) noexcept
		{
			return (static_cast<std::uint32_t>(group) << 16U) |
				static_cast<std::uint32_t>(core_index);
		}

		[[nodiscard]] bool query_windows_cpu_sets(
			std::vector<MultiwayWorkerTopologyRecord>& records)
		{
			records.clear();
			DWORD buffer_bytes = 0U;
			if (::GetSystemCpuSetInformation(nullptr, 0U, &buffer_bytes, nullptr, 0U) != FALSE ||
				::GetLastError() != ERROR_INSUFFICIENT_BUFFER || buffer_bytes == 0U)
				return false;

			std::vector<std::uint8_t> buffer(buffer_bytes);
			if (::GetSystemCpuSetInformation(
					reinterpret_cast<PSYSTEM_CPU_SET_INFORMATION>(buffer.data()),
					buffer_bytes, &buffer_bytes, nullptr, 0U) == FALSE)
				return false;

			const auto total_bytes = static_cast<std::size_t>(buffer_bytes);
			std::size_t offset = 0U;
			while (offset <= total_bytes && total_bytes - offset >= sizeof(SYSTEM_CPU_SET_INFORMATION))
			{
				const auto* information = reinterpret_cast<const SYSTEM_CPU_SET_INFORMATION*>(buffer.data() + offset);
				const auto entry_size = static_cast<std::size_t>(information->Size);
				if (entry_size < sizeof(SYSTEM_CPU_SET_INFORMATION) || entry_size > total_bytes - offset)
				{
					records.clear();
					return false;
				}
				if (information->Type == CpuSetInformation)
				{
					const auto& cpu_set = information->CpuSet;
					const auto flags = static_cast<std::uint8_t>(cpu_set.AllFlags);
					records.push_back(MultiwayWorkerTopologyRecord{
						static_cast<std::uint32_t>(cpu_set.Id),
						static_cast<std::uint16_t>(cpu_set.Group),
						static_cast<std::uint16_t>(cpu_set.LogicalProcessorIndex),
						make_windows_physical_core_id(
							static_cast<std::uint16_t>(cpu_set.Group),
							static_cast<std::uint16_t>(cpu_set.CoreIndex)),
						kMultiwayUnknownTopologyIndex,
						static_cast<std::uint16_t>(cpu_set.NumaNodeIndex),
						static_cast<std::uint16_t>(cpu_set.LastLevelCacheIndex),
						static_cast<std::uint8_t>(cpu_set.EfficiencyClass),
						flags,
						static_cast<std::uint32_t>(cpu_set.AllocationTag),
						(flags & kCpuSetParkedFlag) != 0U,
						(flags & kCpuSetAllocatedFlag) != 0U });
				}
				offset += entry_size;
			}
			return offset == total_bytes && !records.empty();
		}

		[[nodiscard]] bool query_windows_core_relationships(
			std::vector<WindowsCorePlacement>& placements)
		{
			placements.clear();
			DWORD buffer_bytes = 0U;
			if (::GetLogicalProcessorInformationEx(RelationProcessorCore, nullptr, &buffer_bytes) != FALSE ||
				::GetLastError() != ERROR_INSUFFICIENT_BUFFER || buffer_bytes == 0U)
				return false;

			std::vector<std::uint8_t> buffer(buffer_bytes);
			if (::GetLogicalProcessorInformationEx(
					RelationProcessorCore,
					reinterpret_cast<PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX>(buffer.data()),
					&buffer_bytes) == FALSE)
				return false;

			const auto total_bytes = static_cast<std::size_t>(buffer_bytes);
			std::size_t offset = 0U;
			std::uint32_t core_serial = 0U;
			while (offset <= total_bytes && total_bytes - offset >= sizeof(SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX))
			{
				const auto* information = reinterpret_cast<const SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX*>(buffer.data() + offset);
				const auto entry_size = static_cast<std::size_t>(information->Size);
				if (entry_size < sizeof(SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX) || entry_size > total_bytes - offset)
				{
					placements.clear();
					return false;
				}
				if (information->Relationship == RelationProcessorCore)
				{
					const auto& relationship = information->Processor;
					for (WORD group_index = 0U; group_index < relationship.GroupCount; ++group_index)
					{
						const auto group = static_cast<std::uint16_t>(relationship.GroupMask[group_index].Group);
						const auto mask = relationship.GroupMask[group_index].Mask;
						std::uint16_t sibling = 0U;
						for (std::uint16_t logical = 0U; logical < sizeof(KAFFINITY) * 8U; ++logical)
						{
							const auto bit = static_cast<KAFFINITY>(1) << logical;
							if ((mask & bit) == 0U)
								continue;
							placements.push_back(WindowsCorePlacement{
								group,
								logical,
								make_windows_physical_core_id(group, static_cast<std::uint16_t>(core_serial)),
								sibling++ });
						}
					}
					++core_serial;
				}
				offset += entry_size;
			}
			return offset == total_bytes && !placements.empty();
		}

		void apply_windows_core_relationships(
			std::vector<MultiwayWorkerTopologyRecord>& records,
			const std::vector<WindowsCorePlacement>& placements)
		{
			for (const auto& placement : placements)
			{
				auto record = std::find_if(records.begin(), records.end(), [&placement](const auto& candidate) {
					return candidate.processor_group == placement.processor_group &&
						candidate.logical_processor_index == placement.logical_processor_index;
				});
				if (record != records.end())
				{
					record->smt_sibling_index = placement.smt_sibling_index;
					if (!is_known(record->physical_core_id))
						record->physical_core_id = placement.physical_core_id;
				}
				else
				{
					records.push_back(MultiwayWorkerTopologyRecord{
						kMultiwayUnknownTopologyId,
						placement.processor_group,
						placement.logical_processor_index,
						placement.physical_core_id,
						placement.smt_sibling_index });
				}
			}
		}
#endif

	} // namespace

	bool MultiwayWorkerTopologyInventory::has_complete_windows_topology() const noexcept
	{
		if (source != MultiwayWorkerTopologySource::Windows || processors.empty())
			return false;
		for (const auto& processor : processors)
		{
			if (!is_known(processor.cpu_set_id) || !is_known(processor.physical_core_id) ||
				!is_known(processor.smt_sibling_index) || !is_known(processor.numa_node_id) ||
				!is_known(processor.cache_domain_id))
				return false;
		}
		return true;
	}

	const MultiwayWorkerTopologyRecord* MultiwayWorkerTopologyInventory::find(
		std::uint16_t processor_group, std::uint16_t logical_processor_index) const noexcept
	{
		const auto identity = (static_cast<std::uint32_t>(processor_group) << 16U) |
			static_cast<std::uint32_t>(logical_processor_index);
		const auto record = std::lower_bound(processors.begin(), processors.end(), identity,
			[](const auto& candidate, std::uint32_t value) {
				return candidate.logical_processor_identity() < value;
			});
		return record != processors.end() && record->logical_processor_identity() == identity ? &*record : nullptr;
	}

	MultiwayWorkerTopologyInventory make_multiway_worker_topology(
		std::vector<MultiwayWorkerTopologyRecord> records,
		MultiwayWorkerTopologySource source)
	{
		validate_and_sort(records);
		return MultiwayWorkerTopologyInventory{ source, std::move(records) };
	}

	MultiwayWorkerTopologyInventory make_multiway_worker_topology_fallback(
		std::uint32_t logical_processor_count)
	{
		const auto count = logical_processor_count == 0U ? 1U : logical_processor_count;
		if (count > static_cast<std::uint32_t>(kMultiwayUnknownTopologyIndex) + 1U)
			throw std::invalid_argument("multiway topology fallback exceeds logical processor identity capacity");
		std::vector<MultiwayWorkerTopologyRecord> records;
		records.reserve(count);
		for (std::uint32_t logical = 0U; logical < count; ++logical)
		{
			records.push_back(MultiwayWorkerTopologyRecord{
				kMultiwayUnknownTopologyId,
				0U,
				static_cast<std::uint16_t>(logical),
				kMultiwayUnknownTopologyId,
				kMultiwayUnknownTopologyIndex,
				kMultiwayUnknownTopologyIndex,
				kMultiwayUnknownTopologyIndex });
		}
		return make_multiway_worker_topology(std::move(records), MultiwayWorkerTopologySource::Fallback);
	}

	MultiwayWorkerTopologyInventory discover_multiway_worker_topology()
	{
#if defined(_WIN32)
		std::vector<MultiwayWorkerTopologyRecord> records;
		(void)query_windows_cpu_sets(records);
		std::vector<WindowsCorePlacement> placements;
		(void)query_windows_core_relationships(placements);
		if (!records.empty() || !placements.empty())
		{
			apply_windows_core_relationships(records, placements);
			return make_multiway_worker_topology(std::move(records), MultiwayWorkerTopologySource::Windows);
		}
#endif
		const auto hardware_threads = std::thread::hardware_concurrency();
		return make_multiway_worker_topology_fallback(hardware_threads == 0U ? 1U : hardware_threads);
	}

} // namespace texas::solver::multiway
