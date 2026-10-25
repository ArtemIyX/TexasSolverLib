#include "../../server/TracyFileRead.hpp"
#include "../../server/TracyWorker.hpp"

#include <cstdint>
#include <cstdio>
#include <memory>

int main(int argc, char** argv)
{
	if (argc != 2)
	{
		std::fprintf(stderr, "Usage: tracy-memoryexport <trace file>\n");
		return 1;
	}

	std::unique_ptr<tracy::FileRead> file(tracy::FileRead::Open(argv[1]));
	if (!file)
	{
		std::fprintf(stderr, "Could not open file %s\n", argv[1]);
		return 1;
	}

	try
	{
		const tracy::Worker worker(*file, tracy::EventType::Memory, false);
		if (worker.GetFailureType() != tracy::Worker::Failure::None)
		{
			std::fprintf(stderr, "Tracy capture contains an instrumentation failure: %s\n",
				tracy::Worker::GetFailureString(worker.GetFailureType()));
			return 2;
		}

		std::uint64_t allocation_events = 0U;
		std::uint64_t free_events = 0U;
		std::uint64_t active_events = 0U;
		for (const auto& pool : worker.GetMemNameMap())
		{
			if (pool.second == nullptr)
				continue;
			for (const auto& event : pool.second->data)
			{
				++allocation_events;
				if (event.TimeFree() >= 0)
					++free_events;
				else
					++active_events;
			}
		}

		std::printf("{\"memory_pool_count\":%zu,\"allocation_events\":%llu,\"free_events\":%llu,\"active_allocations\":%llu}\n",
			worker.GetMemNameMap().size(),
			static_cast<unsigned long long>(allocation_events),
			static_cast<unsigned long long>(free_events),
			static_cast<unsigned long long>(active_events));
		return 0;
	}
	catch (const std::exception& error)
	{
		std::fprintf(stderr, "Could not inspect Tracy capture: %s\n", error.what());
		return 2;
	}
}
