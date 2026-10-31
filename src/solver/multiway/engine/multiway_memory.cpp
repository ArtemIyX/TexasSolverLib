#include "solver/multiway/engine/multiway_memory.hpp"
#include "solver/multiway/abstraction/multiway_bucket_model.hpp"
#include "solver/multiway/continuation/multiway_rollout_leaf.hpp"
#include "solver/multiway/continuation/multiway_continuation_selector.hpp"
#include "solver/multiway/engine/multiway_compact_storage.hpp"
#include "solver/multiway/engine/multiway_traversal.hpp"
#include "solver/multiway/engine/multiway_solver.hpp"
#include "solver/multiway/blueprint/multiway_export.hpp"

#if defined(_WIN32)
	#ifndef NOMINMAX
		#define NOMINMAX
	#endif
	#ifndef PSAPI_VERSION
		#define PSAPI_VERSION 2
	#endif
	#include <windows.h>
	#include <psapi.h>
#elif defined(__linux__)
	#include <cerrno>
	#include <cstdio>
	#include <ctime>
	#include <cstring>
	#include <cstdlib>
	#include <unistd.h>
#endif

#include <limits>
#include <algorithm>
#include <stdexcept>
#include <thread>

namespace texas::solver::multiway
{
	namespace
	{

		std::uint64_t checked_multiply(std::uint64_t left, std::uint64_t right) noexcept
		{
			if (left != 0U && right > std::numeric_limits<std::uint64_t>::max() / left)
			{
				return std::numeric_limits<std::uint64_t>::max();
			}
			return left * right;
		}

		std::uint64_t checked_add(std::uint64_t left, std::uint64_t right) noexcept
		{
			if (right > std::numeric_limits<std::uint64_t>::max() - left)
			{
				return std::numeric_limits<std::uint64_t>::max();
			}
			return left + right;
		}

#if defined(__linux__)
		void parse_linux_memory_field(
			const char* line,
			const char* name,
			std::uint64_t& value,
			bool& available) noexcept
		{
			const auto name_length = std::strlen(name);
			if (std::strncmp(line, name, name_length) != 0)
				return;
			const char* cursor = line + name_length;
			while (*cursor == ' ' || *cursor == '\t')
				++cursor;
			errno = 0;
			char* end = nullptr;
			const auto parsed = std::strtoull(cursor, &end, 10);
			if (end == cursor || errno == ERANGE)
				return;
			while (*end == ' ' || *end == '\t')
				++end;
			if (std::strncmp(end, "kB", 2U) != 0)
				return;
			value = checked_multiply(static_cast<std::uint64_t>(parsed), 1024U);
			available = value != std::numeric_limits<std::uint64_t>::max();
		}

		void capture_linux_page_faults(MultiwayProcessMemorySnapshot& result) noexcept
		{
			std::FILE* input = std::fopen("/proc/self/stat", "r");
			if (input == nullptr)
				return;
			char line[4096]{};
			if (std::fgets(line, sizeof(line), input) == nullptr)
			{
				std::fclose(input);
				return;
			}
			std::fclose(input);
			const char* cursor = std::strrchr(line, ')');
			if (cursor == nullptr)
				return;
			++cursor;
			while (*cursor == ' ' || *cursor == '\t')
				++cursor;
			if (*cursor == '\0' || *cursor == '\n')
				return;
			++cursor; // Field 3 is the process state character.
			std::uint64_t minor = 0U;
			std::uint64_t major = 0U;
			for (unsigned field = 4U; field <= 12U; ++field)
			{
				while (*cursor == ' ' || *cursor == '\t')
					++cursor;
				errno = 0;
				char* end = nullptr;
				const auto value = std::strtoull(cursor, &end, 10);
				if (end == cursor || errno == ERANGE)
					return;
				if (field == 10U)
					minor = static_cast<std::uint64_t>(value);
				else if (field == 12U)
					major = static_cast<std::uint64_t>(value);
				cursor = end;
			}
			result.minor_page_faults = minor;
			result.major_page_faults = major;
			result.total_page_faults = checked_add(minor, major);
			result.minor_page_faults_available = true;
			result.major_page_faults_available = true;
			result.total_page_faults_available = true;
		}
#endif

	} // namespace

	std::uint64_t multiway_memory_saturating_add(
		std::uint64_t left, std::uint64_t right) noexcept
	{
		if (right > std::numeric_limits<std::uint64_t>::max() - left)
			return std::numeric_limits<std::uint64_t>::max();
		return left + right;
	}

	std::uint64_t multiway_memory_saturating_capacity_bytes(
		std::size_t element_count, std::size_t element_size) noexcept
	{
		if (element_count == 0U || element_size == 0U)
			return 0U;
		if (element_count > std::numeric_limits<std::uint64_t>::max() / element_size)
			return std::numeric_limits<std::uint64_t>::max();
		return static_cast<std::uint64_t>(element_count) * static_cast<std::uint64_t>(element_size);
	}

	void MultiwayMemoryComponent::add_buffer(
		std::size_t live_elements,
		std::size_t retained_elements,
		std::size_t element_size) noexcept
	{
		const auto live = multiway_memory_saturating_capacity_bytes(live_elements, element_size);
		const auto retained = multiway_memory_saturating_capacity_bytes(retained_elements, element_size);
		const auto next_live = multiway_memory_saturating_add(live_element_bytes, live);
		const auto next_retained = multiway_memory_saturating_add(retained_capacity_bytes, retained);
		arithmetic_saturated = arithmetic_saturated || next_live == std::numeric_limits<std::uint64_t>::max() || next_retained == std::numeric_limits<std::uint64_t>::max();
		live_element_bytes = next_live;
		retained_capacity_bytes = next_retained;
		allocation_count_known = true;
		if (retained_elements != 0U)
			allocation_count = multiway_memory_saturating_add(allocation_count, 1U);
	}

	void MultiwayMemoryComponent::accumulate(const MultiwayMemoryComponent& other) noexcept
	{
		const auto next_live = multiway_memory_saturating_add(live_element_bytes, other.live_element_bytes);
		const auto next_retained = multiway_memory_saturating_add(retained_capacity_bytes, other.retained_capacity_bytes);
		arithmetic_saturated = arithmetic_saturated || other.arithmetic_saturated || next_live == std::numeric_limits<std::uint64_t>::max() || next_retained == std::numeric_limits<std::uint64_t>::max();
		live_element_bytes = next_live;
		retained_capacity_bytes = next_retained;
		const bool empty_unknown = !allocation_count_known && allocation_count == 0U && live_element_bytes == 0U && retained_capacity_bytes == 0U && !arithmetic_saturated;
		if (empty_unknown && other.allocation_count_known)
		{
			allocation_count = other.allocation_count;
			allocation_count_known = true;
		}
		else if (allocation_count_known && other.allocation_count_known)
			allocation_count = multiway_memory_saturating_add(allocation_count, other.allocation_count);
		else
		{
			allocation_count = 0U;
			allocation_count_known = false;
		}
		grouping_only = grouping_only || other.grouping_only;
	}

	bool MultiwayMemoryComponent::valid() const noexcept
	{
		return live_element_bytes <= retained_capacity_bytes || grouping_only;
	}

	namespace
	{
		bool same_component(
			const MultiwayMemoryComponent& left,
			const MultiwayMemoryComponent& right) noexcept
		{
			return left.live_element_bytes == right.live_element_bytes && left.retained_capacity_bytes == right.retained_capacity_bytes && left.allocation_count == right.allocation_count && left.allocation_count_known == right.allocation_count_known && left.arithmetic_saturated == right.arithmetic_saturated && left.grouping_only == right.grouping_only;
		}

	} // namespace

	bool MultiwayBucketRegistryMemorySnapshot::valid() const noexcept
	{
		if (!owned_metadata.valid() || !mapping_virtual.valid() || !total.valid() || !total.grouping_only)
			return false;
		MultiwayMemoryComponent expected = owned_metadata;
		expected.accumulate(mapping_virtual);
		expected.grouping_only = true;
		return same_component(expected, total);
	}

	bool MultiwayPublicCatalogMemorySnapshot::valid() const noexcept
	{
		if (!nodes.valid() || !indexes.valid() || !menus.valid() || !nested_payloads.valid() || !scratch.valid() || !total.valid() || !total.grouping_only)
			return false;
		MultiwayMemoryComponent expected = nodes;
		expected.accumulate(indexes);
		expected.accumulate(menus);
		expected.accumulate(nested_payloads);
		expected.accumulate(scratch);
		expected.grouping_only = true;
		return same_component(expected, total);
	}

	bool MultiwaySparseStorageMemorySnapshot::valid() const noexcept
	{
		if (!row_metadata.valid() || !regret_cells.valid() || !strategy_cells.valid() || !total.valid() || !total.grouping_only)
			return false;
		MultiwayMemoryComponent expected = row_metadata;
		expected.accumulate(regret_cells);
		expected.accumulate(strategy_cells);
		expected.grouping_only = true;
		return same_component(expected, total);
	}

	bool MultiwayCompactStorageMemorySnapshot::valid() const noexcept
	{
		if (!row_metadata.valid() || !page_metadata.valid() || !regret_cells.valid() || !strategy_cells.valid() || !total.valid() || !total.grouping_only)
			return false;
		MultiwayMemoryComponent expected = row_metadata;
		expected.accumulate(page_metadata);
		expected.accumulate(regret_cells);
		expected.accumulate(strategy_cells);
		expected.grouping_only = true;
		return same_component(expected, total);
	}

	bool MultiwayWorkerMemorySnapshot::valid() const noexcept
	{
		if (!regret_delta_stream.valid() || !continuation_delta_stream.valid() || !discovery_public_states.valid() || !discovery_rows.valid() || !action_menu_scratch.valid() || !total.valid() || !total.grouping_only)
			return false;
		MultiwayMemoryComponent expected = regret_delta_stream;
		expected.accumulate(continuation_delta_stream);
		expected.accumulate(discovery_public_states);
		expected.accumulate(discovery_rows);
		expected.accumulate(action_menu_scratch);
		expected.grouping_only = true;
		return same_component(expected, total);
	}

	bool MultiwayCoordinatorMemorySnapshot::valid() const noexcept
	{
		if (!merge_scratch.valid() || !admission_scratch.valid() || !total.valid() || !total.grouping_only)
			return false;
		MultiwayMemoryComponent expected = merge_scratch;
		expected.accumulate(admission_scratch);
		expected.grouping_only = true;
		return same_component(expected, total);
	}

	bool MultiwayRootBatchMemorySnapshot::valid() const noexcept
	{
		if (worker_count > MULTIWAY_MEMORY_MAX_WORKERS || !stream_views.valid() || !discovery_merge_scratch.valid() || !discovery_outputs.valid() || !worker_batches.valid() || !thread_scratch.valid() || !total.valid() || !total.grouping_only)
			return false;
		for (std::size_t index = 0U; index < worker_count; ++index)
			if (!workers[index].valid())
				return false;
		MultiwayMemoryComponent expected = stream_views;
		expected.accumulate(discovery_merge_scratch);
		expected.accumulate(discovery_outputs);
		expected.accumulate(worker_batches);
		expected.accumulate(thread_scratch);
		for (std::size_t index = 0U; index < worker_count; ++index)
			expected.accumulate(workers[index].total);
		expected.grouping_only = true;
		return same_component(expected, total);
	}

	void MultiwayMemorySubsystemSnapshot::recompute_totals() noexcept
	{
		MultiwayMemoryComponent total{};
		total.grouping_only = true;
		const auto add = [&total](const MultiwayMemoryComponent& component) noexcept { total.accumulate(component); };
		add(bucket_registry_owned);
		add(public_catalog_nodes);
		add(public_catalog_indexes);
		add(public_catalog_menus);
		add(public_catalog_nested_payloads);
		add(public_catalog_scratch);
		add(compact_row_metadata);
		add(compact_page_metadata);
		add(compact_regret_cells);
		add(compact_strategy_cells);
		worker_regret_delta_stream = {};
		worker_continuation_delta_stream = {};
		discovery_public_state_stream = {};
		discovery_row_stream = {};
		worker_action_menu_scratch = {};
		for (std::size_t index = 0U; index < worker_count && index < MULTIWAY_MEMORY_MAX_WORKERS; ++index)
		{
			workers[index].total = workers[index].regret_delta_stream;
			workers[index].total.accumulate(workers[index].continuation_delta_stream);
			workers[index].total.accumulate(workers[index].discovery_public_states);
			workers[index].total.accumulate(workers[index].discovery_rows);
			workers[index].total.accumulate(workers[index].action_menu_scratch);
			workers[index].total.grouping_only = true;
			worker_regret_delta_stream.accumulate(workers[index].regret_delta_stream);
			worker_continuation_delta_stream.accumulate(workers[index].continuation_delta_stream);
			discovery_public_state_stream.accumulate(workers[index].discovery_public_states);
			discovery_row_stream.accumulate(workers[index].discovery_rows);
			worker_action_menu_scratch.accumulate(workers[index].action_menu_scratch);
			add(workers[index].regret_delta_stream);
			add(workers[index].continuation_delta_stream);
			add(workers[index].discovery_public_states);
			add(workers[index].discovery_rows);
			add(workers[index].action_menu_scratch);
		}
		worker_regret_delta_stream.grouping_only = worker_count != 0U;
		worker_continuation_delta_stream.grouping_only = worker_count != 0U;
		discovery_public_state_stream.grouping_only = worker_count != 0U;
		discovery_row_stream.grouping_only = worker_count != 0U;
		worker_action_menu_scratch.grouping_only = worker_count != 0U;
		add(coordinator_merge_scratch);
		add(coordinator_admission_scratch);
		add(discovery_merge_scratch);
		add(batch_runner_scratch);
		add(checkpoint_retained_buffers);
		add(export_retained_buffers);
		add(continuation_cache);
		add(continuation_scratch);
		live_bytes = total.live_element_bytes;
		retained_capacity_bytes = total.retained_capacity_bytes;
		arithmetic_saturated = total.arithmetic_saturated;
	}

	bool MultiwayMemorySubsystemSnapshot::valid() const noexcept
	{
		if (schema_version != MULTIWAY_MEMORY_SUBSYSTEM_SCHEMA_VERSION || worker_count > MULTIWAY_MEMORY_MAX_WORKERS)
			return false;
		const MultiwayMemoryComponent* components[] = {
			&bucket_registry_owned, &bucket_mapping_virtual,
			&public_catalog_nodes, &public_catalog_indexes, &public_catalog_menus,
			&public_catalog_nested_payloads, &public_catalog_scratch,
			&compact_row_metadata, &compact_page_metadata, &compact_regret_cells,
			&compact_strategy_cells, &worker_regret_delta_stream,
			&worker_continuation_delta_stream, &discovery_public_state_stream,
			&discovery_row_stream, &worker_action_menu_scratch,
			&discovery_merge_scratch, &batch_runner_scratch,
			&coordinator_merge_scratch, &coordinator_admission_scratch,
			&checkpoint_retained_buffers, &export_retained_buffers,
			&continuation_cache, &continuation_scratch
		};
		for (const auto* component : components)
			if (!component->valid())
				return false;
		for (std::size_t index = 0U; index < worker_count; ++index)
			if (!workers[index].valid())
				return false;
		MultiwayMemorySubsystemSnapshot expected = *this;
		expected.recompute_totals();
		if (expected.live_bytes != live_bytes || expected.retained_capacity_bytes != retained_capacity_bytes || expected.arithmetic_saturated != arithmetic_saturated)
			return false;
		if (!same_component(expected.worker_regret_delta_stream, worker_regret_delta_stream) || !same_component(expected.worker_continuation_delta_stream, worker_continuation_delta_stream) || !same_component(expected.discovery_public_state_stream, discovery_public_state_stream) || !same_component(expected.discovery_row_stream, discovery_row_stream) || !same_component(expected.worker_action_menu_scratch, worker_action_menu_scratch))
			return false;
		for (std::size_t index = 0U; index < worker_count; ++index)
			if (!same_component(expected.workers[index].total, workers[index].total))
				return false;
		return true;
	}

	bool MultiwayMemorySubsystemSnapshotCollection::record(
		MultiwayMemoryPhase phase,
		MultiwayMemorySubsystemSnapshot snapshot) noexcept
	{
		const auto phase_index = static_cast<std::size_t>(phase);
		if (phase_index >= snapshots.size() || completed || phase_index != count || !snapshot.valid())
			return false;
		if (!enabled)
		{
			if (phase != MultiwayMemoryPhase::ProcessStart || count != 0U)
				return false;
			enabled = true;
		}
		snapshot.phase_id = phase;
		snapshots[count++] = snapshot;
		completed = phase == MultiwayMemoryPhase::AfterExport;
		return true;
	}

	bool MultiwayMemorySubsystemSnapshotCollection::valid() const noexcept
	{
		if (!enabled)
			return count == 0U && !completed;
		if (count > snapshots.size() || (completed && count != snapshots.size()))
			return false;
		for (std::size_t index = 0U; index < count; ++index)
		{
			if (static_cast<std::size_t>(snapshots[index].phase_id) != index || static_cast<std::size_t>(snapshots[index].schema_version) == 0U || !snapshots[index].valid())
				return false;
		}
		return true;
	}

	MultiwayMemorySubsystemSnapshot capture_multiway_memory_subsystems(
		const MultiwayBucketRegistry* buckets,
		const MultiwaySolverCoordinator* coordinator,
		const MultiwayRootBatchRunner* batch_runner,
		const MultiwayFixedContinuationSelector* continuation_selector,
		const MultiwayContinuationCache* continuation_cache,
		MultiwayMemoryComponent checkpoint_buffers,
		MultiwayMemoryComponent export_buffers) noexcept
	{
		MultiwayMemorySubsystemSnapshot result;
		result.checkpoint_retained_buffers = checkpoint_buffers;
		result.export_retained_buffers = export_buffers;
		if (buckets != nullptr)
		{
			const auto snapshot = buckets->memory_snapshot();
			result.bucket_registry_owned = snapshot.owned_metadata;
			result.bucket_mapping_virtual = snapshot.mapping_virtual;
		}
		if (coordinator != nullptr)
		{
			const auto catalog = coordinator->public_catalog().memory_snapshot();
			result.public_catalog_nodes = catalog.nodes;
			result.public_catalog_indexes = catalog.indexes;
			result.public_catalog_menus = catalog.menus;
			result.public_catalog_nested_payloads = catalog.nested_payloads;
			result.public_catalog_scratch = catalog.scratch;
			const auto coordinator_memory = coordinator->memory_snapshot();
			result.coordinator_merge_scratch = coordinator_memory.merge_scratch;
			result.coordinator_admission_scratch = coordinator_memory.admission_scratch;
			if (const auto* compact = coordinator->compact_storage(); compact != nullptr)
			{
				const auto storage = compact->memory_snapshot();
				result.compact_row_metadata = storage.row_metadata;
				result.compact_page_metadata = storage.page_metadata;
				result.compact_regret_cells = storage.regret_cells;
				result.compact_strategy_cells = storage.strategy_cells;
			}
			else
			{
				const auto storage = coordinator->storage().memory_snapshot();
				result.compact_row_metadata = storage.row_metadata;
				result.compact_regret_cells = storage.regret_cells;
				result.compact_strategy_cells = storage.strategy_cells;
			}
		}
		if (batch_runner != nullptr)
		{
			const auto runner = batch_runner->memory_snapshot();
			result.worker_count = runner.worker_count;
			result.worker_regret_delta_stream = {};
			result.worker_continuation_delta_stream = {};
			result.discovery_public_state_stream = {};
			result.discovery_row_stream = {};
			result.worker_action_menu_scratch = {};
			for (std::size_t index = 0U; index < runner.worker_count; ++index)
			{
				result.workers[index] = runner.workers[index];
				result.worker_regret_delta_stream.accumulate(runner.workers[index].regret_delta_stream);
				result.worker_continuation_delta_stream.accumulate(runner.workers[index].continuation_delta_stream);
				result.discovery_public_state_stream.accumulate(runner.workers[index].discovery_public_states);
				result.discovery_row_stream.accumulate(runner.workers[index].discovery_rows);
				result.worker_action_menu_scratch.accumulate(runner.workers[index].action_menu_scratch);
			}
			const bool has_workers = result.worker_count != 0U;
			result.worker_regret_delta_stream.grouping_only = has_workers;
			result.worker_continuation_delta_stream.grouping_only = has_workers;
			result.discovery_public_state_stream.grouping_only = has_workers;
			result.discovery_row_stream.grouping_only = has_workers;
			result.worker_action_menu_scratch.grouping_only = has_workers;
			result.discovery_merge_scratch = runner.discovery_merge_scratch;
			result.batch_runner_scratch = runner.stream_views;
			result.batch_runner_scratch.accumulate(runner.discovery_outputs);
			result.batch_runner_scratch.accumulate(runner.worker_batches);
			result.batch_runner_scratch.accumulate(runner.thread_scratch);
			result.batch_runner_scratch.grouping_only = true;
		}
		if (continuation_selector != nullptr)
			result.continuation_cache = continuation_selector->memory_snapshot();
		if (continuation_cache != nullptr)
			result.continuation_cache.accumulate(continuation_cache->memory_snapshot());
		result.recompute_totals();
		return result;
	}

	bool MultiwayProcessMemorySnapshot::valid() const noexcept
	{
		if ((!current_working_set_available && current_working_set_bytes != 0U) || (!peak_working_set_available && peak_working_set_bytes != 0U) || (!private_committed_available && private_committed_bytes != 0U) || (!peak_private_committed_available && peak_private_committed_bytes != 0U) || (!virtual_address_available && virtual_address_bytes != 0U) || (!anonymous_resident_available && anonymous_resident_bytes != 0U) || (!file_resident_available && file_resident_bytes != 0U) || (!minor_page_faults_available && minor_page_faults != 0U) || (!major_page_faults_available && major_page_faults != 0U) || (!total_page_faults_available && total_page_faults != 0U))
		{
			return false;
		}
		return (!current_working_set_available || !peak_working_set_available || peak_working_set_bytes >= current_working_set_bytes) && (!private_committed_available || !peak_private_committed_available || peak_private_committed_bytes >= private_committed_bytes) && (!minor_page_faults_available || !major_page_faults_available || !total_page_faults_available || total_page_faults >= multiway_memory_saturating_add(minor_page_faults, major_page_faults));
	}

	bool MultiwayProcessMemorySnapshotCollection::record(
		MultiwayMemoryPhase phase,
		MultiwayProcessMemorySnapshot snapshot) noexcept
	{
		const auto phase_index = static_cast<std::size_t>(phase);
		if (phase_index >= snapshots.size() || completed)
			return false;
		if (!enabled)
		{
			if (phase != MultiwayMemoryPhase::ProcessStart || count != 0U)
				return false;
			enabled = true;
		}
		if (phase_index != count || count >= snapshots.size())
			return false;
		snapshot.phase_id = phase;
		if (!snapshot.valid())
			return false;
		snapshots[count++] = snapshot;
		completed = phase == MultiwayMemoryPhase::AfterExport;
		return true;
	}

	bool MultiwayProcessMemorySnapshotCollection::record_current(MultiwayMemoryPhase phase) noexcept
	{
		if (!enabled && phase != MultiwayMemoryPhase::ProcessStart)
			return true;
		return record(phase, capture_multiway_process_memory_snapshot());
	}

	bool MultiwayProcessMemorySnapshotCollection::valid() const noexcept
	{
		if (!enabled)
			return count == 0U && !completed;
		if (count > snapshots.size() || (completed && count != snapshots.size()))
			return false;
		for (std::size_t index = 0U; index < count; ++index)
		{
			if (static_cast<std::size_t>(snapshots[index].phase_id) != index || !snapshots[index].valid())
				return false;
			if (index != 0U)
			{
				const auto& previous = snapshots[index - 1U];
				const auto& current = snapshots[index];
				if ((previous.minor_page_faults_available && current.minor_page_faults_available && current.minor_page_faults < previous.minor_page_faults) || (previous.major_page_faults_available && current.major_page_faults_available && current.major_page_faults < previous.major_page_faults) || (previous.total_page_faults_available && current.total_page_faults_available && current.total_page_faults < previous.total_page_faults))
					return false;
			}
		}
		return true;
	}

	const char* multiway_memory_phase_name(MultiwayMemoryPhase phase) noexcept
	{
		switch (phase)
		{
			case MultiwayMemoryPhase::ProcessStart:
				return "process_start";
			case MultiwayMemoryPhase::BeforeBucketMap:
				return "before_bucket_map";
			case MultiwayMemoryPhase::AfterBucketMapMetadata:
				return "after_bucket_map_metadata";
			case MultiwayMemoryPhase::AfterBucketAssignmentValidation:
				return "after_bucket_assignment_validation";
			case MultiwayMemoryPhase::BeforeMemoryPreflight:
				return "before_memory_preflight";
			case MultiwayMemoryPhase::AfterCoordinatorConstruction:
				return "after_coordinator_construction";
			case MultiwayMemoryPhase::AfterWorkerPoolConstruction:
				return "after_worker_pool_construction";
			case MultiwayMemoryPhase::AfterColdBatch:
				return "after_cold_batch";
			case MultiwayMemoryPhase::AfterSteadyBatch:
				return "after_steady_batch";
			case MultiwayMemoryPhase::BeforeCheckpoint:
				return "before_checkpoint";
			case MultiwayMemoryPhase::AfterCheckpoint:
				return "after_checkpoint";
			case MultiwayMemoryPhase::BeforeExport:
				return "before_export";
			case MultiwayMemoryPhase::AfterExport:
				return "after_export";
		}
		return "unknown";
	}

	MultiwayProcessMemorySnapshot capture_multiway_process_memory_snapshot() noexcept
	{
		MultiwayProcessMemorySnapshot result;
#if defined(_WIN32)
		PROCESS_MEMORY_COUNTERS_EX counters{};
		counters.cb = sizeof(counters);
		if (K32GetProcessMemoryInfo(
				GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&counters), sizeof(counters))
			!= 0)
		{
			result.current_working_set_bytes = static_cast<std::uint64_t>(counters.WorkingSetSize);
			// Windows can transiently report a peak below the current working set.
			result.peak_working_set_bytes = std::max(
				static_cast<std::uint64_t>(counters.PeakWorkingSetSize),
				result.current_working_set_bytes);
			result.private_committed_bytes = static_cast<std::uint64_t>(counters.PrivateUsage);
			result.total_page_faults = static_cast<std::uint64_t>(counters.PageFaultCount);
			result.current_working_set_available = true;
			result.peak_working_set_available = true;
			result.private_committed_available = true;
			result.total_page_faults_available = true;
		}
#elif defined(__linux__)
		std::FILE* input = std::fopen("/proc/self/status", "r");
		if (input == nullptr)
			return result;
		char line[256]{};
		while (std::fgets(line, sizeof(line), input) != nullptr)
		{
			parse_linux_memory_field(line, "VmRSS:", result.current_working_set_bytes, result.current_working_set_available);
			parse_linux_memory_field(line, "VmHWM:", result.peak_working_set_bytes, result.peak_working_set_available);
			parse_linux_memory_field(line, "VmSize:", result.virtual_address_bytes, result.virtual_address_available);
			parse_linux_memory_field(line, "RssAnon:", result.anonymous_resident_bytes, result.anonymous_resident_available);
			parse_linux_memory_field(line, "RssFile:", result.file_resident_bytes, result.file_resident_available);
		}
		std::fclose(input);
		capture_linux_page_faults(result);
#endif
		return result;
	}

	bool MultiwayExportPlanMemoryValues::valid() const noexcept
	{
		return schema_version == MULTIWAY_EXPORT_TELEMETRY_SCHEMA_VERSION && (compact_direct_order ? order_index_bytes == 0U : true);
	}

	bool MultiwayExportScratchEstimate::valid() const noexcept
	{
		return schema_version == MULTIWAY_EXPORT_TELEMETRY_SCHEMA_VERSION && (total_bytes == std::numeric_limits<std::uint64_t>::max() || (total_bytes >= order_index_bytes && total_bytes >= sharding_metadata_bytes && total_bytes >= strategy_scratch_bytes && total_bytes >= action_scratch_bytes && total_bytes >= encoder_buffer_bytes && total_bytes >= exporter_headroom_bytes));
	}

	bool MultiwayExportProcessEstimate::valid() const noexcept
	{
		return schema_version == MULTIWAY_EXPORT_TELEMETRY_SCHEMA_VERSION && (current_working_set_available || current_working_set_bytes == 0U) && (estimated_process_export_peak_bytes == std::numeric_limits<std::uint64_t>::max() || estimated_process_export_peak_bytes >= current_working_set_bytes);
	}

	bool MultiwaySystemMemorySnapshot::valid() const noexcept
	{
		return schema_version == MULTIWAY_EXPORT_TELEMETRY_SCHEMA_VERSION && (total_physical_available || total_physical_bytes == 0U) && (available_physical_available || available_physical_bytes == 0U);
	}

	void MultiwaySystemMemoryGuardConfig::validate() const
	{
		if (enabled && minimum_available_physical_bytes == 0U)
			throw std::invalid_argument("enabled system memory guard requires a non-zero reserve");
	}

	bool MultiwayExportObservedMemoryDelta::valid() const noexcept
	{
		return schema_version == MULTIWAY_EXPORT_TELEMETRY_SCHEMA_VERSION && (!before_working_set_available || before_working_set_bytes != 0U || working_set_delta_bytes == 0U) && (!after_working_set_available || after_working_set_bytes != 0U || working_set_delta_bytes == 0U) && (!before_private_committed_available || before_private_committed_bytes != 0U || private_committed_delta_bytes == 0U) && (!after_private_committed_available || after_private_committed_bytes != 0U || private_committed_delta_bytes == 0U);
	}

	void MultiwayExportProcessGateConfig::validate() const
	{
	}

	void MultiwayExportSystemGateConfig::validate() const
	{
		if (!enabled && system_reserve_bytes != 0U)
			throw std::invalid_argument("disabled export system gate cannot configure a system reserve");
	}

	void MultiwayExportMemoryGateConfig::validate() const
	{
		process.validate();
		system.validate();
	}

	MultiwayExportScratchEstimate make_multiway_export_scratch_estimate(
		const MultiwayExportPlanMemoryValues& plan,
		MultiwayExportProcessGateConfig config) noexcept
	{
		MultiwayExportScratchEstimate result;
		result.order_index_bytes = plan.order_index_bytes;
		result.sharding_metadata_bytes = plan.sharding_metadata_bytes;
		result.strategy_scratch_bytes = multiway_memory_saturating_capacity_bytes(
			MULTIWAY_MAX_ABSTRACTED_ACTIONS, sizeof(Probability));
		result.action_scratch_bytes = multiway_memory_saturating_capacity_bytes(
			MULTIWAY_MAX_ABSTRACTED_ACTIONS, sizeof(MultiwayQuantizedRootAction));
		result.encoder_buffer_bytes = config.encoder_buffer_bytes;
		result.exporter_headroom_bytes = config.exporter_headroom_bytes;
		const auto add = [&result](std::uint64_t value) noexcept {
			const auto next = multiway_memory_saturating_add(result.total_bytes, value);
			result.arithmetic_saturated = result.arithmetic_saturated || next == std::numeric_limits<std::uint64_t>::max();
			result.total_bytes = next;
		};
		add(result.order_index_bytes);
		add(result.sharding_metadata_bytes);
		add(result.strategy_scratch_bytes);
		add(result.action_scratch_bytes);
		add(result.encoder_buffer_bytes);
		add(result.exporter_headroom_bytes);
		return result;
	}

	MultiwayExportProcessEstimate make_multiway_export_process_estimate(
		const MultiwayExportPlanMemoryValues& plan,
		const MultiwayProcessMemorySnapshot& snapshot,
		MultiwayExportProcessGateConfig config) noexcept
	{
		const auto scratch = make_multiway_export_scratch_estimate(plan, config);
		MultiwayExportProcessEstimate result;
		result.current_working_set_available = snapshot.current_working_set_available;
		result.current_working_set_bytes = snapshot.current_working_set_bytes;
		result.order_index_bytes = scratch.order_index_bytes;
		result.sharding_metadata_bytes = scratch.sharding_metadata_bytes;
		result.strategy_scratch_bytes = scratch.strategy_scratch_bytes;
		result.action_scratch_bytes = scratch.action_scratch_bytes;
		result.encoder_buffer_bytes = scratch.encoder_buffer_bytes;
		result.exporter_headroom_bytes = scratch.exporter_headroom_bytes;
		result.mapped_residency_allowance_bytes = config.mapped_residency_allowance_bytes;
		const auto add = [&result](std::uint64_t value) noexcept {
			const auto next = multiway_memory_saturating_add(
				result.estimated_process_export_peak_bytes, value);
			result.arithmetic_saturated = result.arithmetic_saturated || next == std::numeric_limits<std::uint64_t>::max();
			result.estimated_process_export_peak_bytes = next;
		};
		add(result.current_working_set_bytes);
		add(result.order_index_bytes);
		add(result.sharding_metadata_bytes);
		add(result.strategy_scratch_bytes);
		add(result.action_scratch_bytes);
		add(result.encoder_buffer_bytes);
		add(result.exporter_headroom_bytes);
		add(result.mapped_residency_allowance_bytes);
		result.arithmetic_saturated = result.arithmetic_saturated || scratch.arithmetic_saturated;
		return result;
	}

	MultiwayExportProcessGateResult evaluate_multiway_export_process_gate(
		const MultiwayExportPlanMemoryValues& plan,
		const MultiwayProcessMemorySnapshot& snapshot,
		MultiwayExportProcessGateConfig config) noexcept
	{
		MultiwayExportProcessGateResult result;
		result.hard_limit_bytes = config.hard_limit_bytes;
		result.estimate = make_multiway_export_process_estimate(plan, snapshot, config);
		result.compared_process_working_set_bytes = result.estimate.estimated_process_export_peak_bytes;
		if (config.hard_limit_bytes == 0U)
		{
			result.status = MultiwayExportGateStatus::Disabled;
			result.message = "export process gate disabled";
			return result;
		}
		if (!snapshot.current_working_set_available)
		{
			result.status = MultiwayExportGateStatus::Unavailable;
			result.rejection_reason = MultiwayExportProcessRejectionReason::CurrentWorkingSetUnavailable;
			result.message = "current working-set metric unavailable";
			return result;
		}
		if (result.estimate.arithmetic_saturated)
		{
			result.status = MultiwayExportGateStatus::Rejected;
			result.rejection_reason = MultiwayExportProcessRejectionReason::EstimateOverflow;
			result.message = "export process estimate saturated";
			return result;
		}
		if (result.compared_process_working_set_bytes > config.hard_limit_bytes)
		{
			result.status = MultiwayExportGateStatus::Rejected;
			result.rejection_reason = MultiwayExportProcessRejectionReason::ProcessLimitExceeded;
			result.message = "estimated export process working set exceeds hard limit";
			return result;
		}
		result.status = MultiwayExportGateStatus::Admitted;
		result.message = "export process gate admitted";
		return result;
	}

	MultiwaySystemMemorySnapshot capture_multiway_system_memory_snapshot() noexcept
	{
		MultiwaySystemMemorySnapshot result;
#if defined(_WIN32)
		MEMORYSTATUSEX status{};
		status.dwLength = sizeof(status);
		if (GlobalMemoryStatusEx(&status) != 0)
		{
			result.total_physical_bytes = static_cast<std::uint64_t>(status.ullTotalPhys);
			result.available_physical_bytes = static_cast<std::uint64_t>(status.ullAvailPhys);
			result.total_physical_available = true;
			result.available_physical_available = true;
			result.used_physical_bytes = result.total_physical_bytes >= result.available_physical_bytes
				? result.total_physical_bytes - result.available_physical_bytes
				: std::numeric_limits<std::uint64_t>::max();
		}
#elif defined(__linux__)
		std::FILE* input = std::fopen("/proc/meminfo", "r");
		if (input == nullptr)
			return result;
		char line[256]{};
		while (std::fgets(line, sizeof(line), input) != nullptr)
		{
			parse_linux_memory_field(line, "MemTotal:", result.total_physical_bytes, result.total_physical_available);
			parse_linux_memory_field(line, "MemAvailable:", result.available_physical_bytes, result.available_physical_available);
		}
		std::fclose(input);
		if (result.total_physical_available && result.available_physical_available)
			result.used_physical_bytes = result.total_physical_bytes >= result.available_physical_bytes
				? result.total_physical_bytes - result.available_physical_bytes
				: std::numeric_limits<std::uint64_t>::max();
#endif
		return result;
	}

	MultiwaySystemMemoryGuardResult evaluate_multiway_system_memory_guard(
		MultiwaySystemMemorySnapshot snapshot,
		MultiwaySystemMemoryGuardConfig config) noexcept
	{
		MultiwaySystemMemoryGuardResult result;
		result.total_physical_bytes = snapshot.total_physical_bytes;
		result.available_physical_bytes = snapshot.available_physical_bytes;
		result.used_physical_bytes = snapshot.used_physical_bytes;
		result.minimum_available_physical_bytes = config.minimum_available_physical_bytes;
		result.maximum_used_physical_bytes = snapshot.total_physical_available && snapshot.total_physical_bytes >= config.minimum_available_physical_bytes
			? snapshot.total_physical_bytes - config.minimum_available_physical_bytes
			: 0U;
		if (!config.enabled)
		{
			result.status = MultiwaySystemMemoryGuardStatus::Disabled;
			result.message = "system memory guard disabled";
			return result;
		}
		if (!snapshot.total_physical_available || !snapshot.available_physical_available || snapshot.used_physical_bytes == std::numeric_limits<std::uint64_t>::max())
		{
			result.status = MultiwaySystemMemoryGuardStatus::Unavailable;
			result.rejected_category = "system memory telemetry";
			result.message = "total or available physical memory metric unavailable";
			return result;
		}
		if (snapshot.available_physical_bytes < config.minimum_available_physical_bytes)
		{
			result.status = MultiwaySystemMemoryGuardStatus::Rejected;
			result.rejected_category = "system physical memory ceiling";
			result.message = "system memory usage exceeds maximum minus reserve";
			return result;
		}
		result.status = MultiwaySystemMemoryGuardStatus::Admitted;
		result.message = "system memory guard admitted";
		return result;
	}

	MultiwayExportSystemGateResult evaluate_multiway_export_system_gate(
		const MultiwayExportPlanMemoryValues& plan,
		const MultiwayExportScratchEstimate& scratch,
		const MultiwaySystemMemorySnapshot& snapshot,
		MultiwayExportSystemGateConfig config) noexcept
	{
		MultiwayExportSystemGateResult result;
		result.available_physical_bytes = snapshot.available_physical_bytes;
		result.serialized_output_bytes = plan.serialized_bytes;
		result.exporter_scratch_bytes = scratch.total_bytes;
		result.system_reserve_bytes = config.system_reserve_bytes;
		if (!config.enabled)
		{
			result.status = MultiwayExportGateStatus::Disabled;
			result.message = "export system gate disabled";
			return result;
		}
		if (!snapshot.available_physical_available)
		{
			result.status = MultiwayExportGateStatus::Unavailable;
			result.rejection_reason = MultiwayExportSystemRejectionReason::AvailablePhysicalMemoryUnavailable;
			result.message = "available physical memory metric unavailable";
			return result;
		}
		result.required_available_bytes = multiway_memory_saturating_add(
			multiway_memory_saturating_add(plan.serialized_bytes, scratch.total_bytes),
			config.system_reserve_bytes);
		if (result.required_available_bytes == std::numeric_limits<std::uint64_t>::max())
		{
			result.status = MultiwayExportGateStatus::Rejected;
			result.rejection_reason = MultiwayExportSystemRejectionReason::AllowanceOverflow;
			result.message = "export system allowance saturated";
			return result;
		}
		if (snapshot.available_physical_bytes < result.required_available_bytes)
		{
			result.status = MultiwayExportGateStatus::Rejected;
			result.rejection_reason = MultiwayExportSystemRejectionReason::AvailablePhysicalMemoryInsufficient;
			result.message = "available physical memory is below export allowance";
			return result;
		}
		result.status = MultiwayExportGateStatus::Admitted;
		result.message = "export system gate admitted";
		return result;
	}

	MultiwayExportDiskGateResult evaluate_multiway_export_disk_gate(
		std::uint64_t serialized_output_bytes, std::uint64_t reserve_bytes,
		bool available_free_bytes_available, std::uint64_t available_free_bytes) noexcept
	{
		return evaluate_multiway_export_disk_gate(serialized_output_bytes,
			serialized_output_bytes, reserve_bytes, available_free_bytes_available,
			available_free_bytes);
	}

	MultiwayExportDiskGateResult evaluate_multiway_export_disk_gate(
		std::uint64_t serialized_output_bytes, std::uint64_t temporary_overlap_bytes,
		std::uint64_t reserve_bytes, bool available_free_bytes_available,
		std::uint64_t available_free_bytes) noexcept
	{
		MultiwayExportDiskGateResult result;
		result.serialized_output_bytes = serialized_output_bytes;
		result.temporary_overlap_bytes = temporary_overlap_bytes;
		result.reserve_bytes = reserve_bytes;
		result.available_free_bytes = available_free_bytes;
		if (!available_free_bytes_available)
		{
			result.status = MultiwayExportDiskGateStatus::Unavailable;
			result.rejection_reason = MultiwayExportDiskRejectionReason::FreeSpaceUnavailable;
			return result;
		}
		const auto publication_bytes = multiway_memory_saturating_add(
			serialized_output_bytes, temporary_overlap_bytes);
		result.required_free_bytes = multiway_memory_saturating_add(
			publication_bytes, reserve_bytes);
		if (publication_bytes == std::numeric_limits<std::uint64_t>::max() || result.required_free_bytes == std::numeric_limits<std::uint64_t>::max())
		{
			result.status = MultiwayExportDiskGateStatus::Rejected;
			result.rejection_reason = MultiwayExportDiskRejectionReason::RequiredSpaceOverflow;
			return result;
		}
		if (available_free_bytes < result.required_free_bytes)
		{
			result.status = MultiwayExportDiskGateStatus::Rejected;
			result.rejection_reason = MultiwayExportDiskRejectionReason::InsufficientFreeSpace;
			return result;
		}
		result.status = MultiwayExportDiskGateStatus::Admitted;
		return result;
	}

	MultiwayExportObservedMemoryDelta observe_multiway_export_memory_delta(
		const MultiwayProcessMemorySnapshot& before,
		const MultiwayProcessMemorySnapshot& after) noexcept
	{
		MultiwayExportObservedMemoryDelta result;
		result.before_working_set_available = before.current_working_set_available;
		result.after_working_set_available = after.current_working_set_available;
		result.before_private_committed_available = before.private_committed_available;
		result.after_private_committed_available = after.private_committed_available;
		result.before_working_set_bytes = before.current_working_set_bytes;
		result.after_working_set_bytes = after.current_working_set_bytes;
		result.before_private_committed_bytes = before.private_committed_bytes;
		result.after_private_committed_bytes = after.private_committed_bytes;
		if (result.before_working_set_available && result.after_working_set_available)
		{
			if (after.current_working_set_bytes >= before.current_working_set_bytes)
				result.working_set_delta_bytes = after.current_working_set_bytes - before.current_working_set_bytes;
			else
				result.working_set_delta_decreased = true;
		}
		if (result.before_private_committed_available && result.after_private_committed_available)
		{
			if (after.private_committed_bytes >= before.private_committed_bytes)
				result.private_committed_delta_bytes = after.private_committed_bytes - before.private_committed_bytes;
			else
				result.private_committed_delta_decreased = true;
		}
		return result;
	}

	const char* multiway_export_process_rejection_reason_name(
		MultiwayExportProcessRejectionReason reason) noexcept
	{
		switch (reason)
		{
			case MultiwayExportProcessRejectionReason::None:
				return "none";
			case MultiwayExportProcessRejectionReason::CurrentWorkingSetUnavailable:
				return "current_working_set_unavailable";
			case MultiwayExportProcessRejectionReason::EstimateOverflow:
				return "estimate_overflow";
			case MultiwayExportProcessRejectionReason::ProcessLimitExceeded:
				return "process_limit_exceeded";
		}
		return "invalid";
	}

	const char* multiway_export_system_rejection_reason_name(
		MultiwayExportSystemRejectionReason reason) noexcept
	{
		switch (reason)
		{
			case MultiwayExportSystemRejectionReason::None:
				return "none";
			case MultiwayExportSystemRejectionReason::AvailablePhysicalMemoryUnavailable:
				return "available_physical_memory_unavailable";
			case MultiwayExportSystemRejectionReason::AllowanceOverflow:
				return "allowance_overflow";
			case MultiwayExportSystemRejectionReason::AvailablePhysicalMemoryInsufficient:
				return "available_physical_memory_insufficient";
		}
		return "invalid";
	}

	void MultiwayExportTelemetry::validate() const
	{
		if (schema_version != MULTIWAY_EXPORT_TELEMETRY_SCHEMA_VERSION)
			throw std::invalid_argument("invalid multiway export telemetry schema");
		if (!attempted)
			return;
		if (!plan.valid() || !scratch.valid() || !process_estimate.valid() || !system_before.valid() || !system_after.valid() || !observed_delta.valid() || process_gate.schema_version != MULTIWAY_EXPORT_TELEMETRY_SCHEMA_VERSION || system_gate.schema_version != MULTIWAY_EXPORT_TELEMETRY_SCHEMA_VERSION || static_cast<unsigned>(process_gate.status) > static_cast<unsigned>(MultiwayExportGateStatus::Unavailable) || static_cast<unsigned>(system_gate.status) > static_cast<unsigned>(MultiwayExportGateStatus::Unavailable) || static_cast<unsigned>(process_gate.rejection_reason) > static_cast<unsigned>(MultiwayExportProcessRejectionReason::ProcessLimitExceeded) || static_cast<unsigned>(system_gate.rejection_reason) > static_cast<unsigned>(MultiwayExportSystemRejectionReason::AvailablePhysicalMemoryInsufficient) || static_cast<unsigned>(publication_mode) > static_cast<unsigned>(core::AtomicPublishMode::BackupRename))
			throw std::invalid_argument("invalid multiway export telemetry values");
		if (static_cast<unsigned>(disk_gate.status) > static_cast<unsigned>(MultiwayExportDiskGateStatus::Unavailable) || static_cast<unsigned>(disk_gate.rejection_reason) > static_cast<unsigned>(MultiwayExportDiskRejectionReason::InsufficientFreeSpace))
			throw std::invalid_argument("invalid multiway export disk gate telemetry");
		if (disk_gate.status == MultiwayExportDiskGateStatus::Admitted && ((sharded_output ? disk_gate.serialized_output_bytes < plan.serialized_bytes : disk_gate.serialized_output_bytes != plan.serialized_bytes) || disk_gate.required_free_bytes < disk_gate.serialized_output_bytes || disk_gate.available_free_bytes < disk_gate.required_free_bytes))
			throw std::invalid_argument("admitted multiway export disk telemetry is inconsistent");
		if (sharded_output != (shard_count != 0U) || (!sharded_output && manifest_bytes != 0U))
			throw std::invalid_argument("multiway export shard telemetry is inconsistent");
		if (succeeded && (process_gate.blocked() || system_gate.blocked() || disk_gate.status != MultiwayExportDiskGateStatus::Admitted || result_bytes != disk_gate.serialized_output_bytes || (!sharded_output && (result_bytes != plan.serialized_bytes || payload_hash == 0U)) || policy_content_hash == 0U || publication_mode == core::AtomicPublishMode::Unknown))
			throw std::invalid_argument("successful multiway export telemetry is incomplete");
	}

	std::uint64_t observed_multiway_process_memory_bytes() noexcept
	{
		const auto snapshot = capture_multiway_process_memory_snapshot();
		return snapshot.current_working_set_available ? snapshot.current_working_set_bytes : 0U;
	}

	std::uint64_t observed_multiway_process_peak_working_set_bytes() noexcept
	{
		const auto snapshot = capture_multiway_process_memory_snapshot();
		return snapshot.peak_working_set_available ? snapshot.peak_working_set_bytes : 0U;
	}

	std::uint64_t observed_multiway_training_process_cpu_nanoseconds() noexcept
	{
#if defined(_WIN32)
		FILETIME creation{}, exit{}, kernel{}, user{};
		if (GetProcessTimes(GetCurrentProcess(), &creation, &exit, &kernel, &user) == 0)
			return 0U;
		const auto ticks = [](const FILETIME& value) noexcept {
			return (static_cast<std::uint64_t>(value.dwHighDateTime) << 32U) | value.dwLowDateTime;
		};
		const auto total = ticks(kernel) + ticks(user);
		return total > UINT64_MAX / 100U ? UINT64_MAX : total * 100U;
#elif defined(__linux__)
		struct timespec value{};
		if (clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &value) != 0 || value.tv_sec < 0 || value.tv_nsec < 0)
			return 0U;
		const auto seconds = static_cast<std::uint64_t>(value.tv_sec);
		return seconds > UINT64_MAX / 1'000'000'000ULL ? UINT64_MAX : seconds * 1'000'000'000ULL + static_cast<std::uint64_t>(value.tv_nsec);
#else
		return 0U;
#endif
	}

	void MultiwayMemoryBudget::validate() const
	{
		if (warning_bytes == std::numeric_limits<std::uint64_t>::max() && operating_bytes == std::numeric_limits<std::uint64_t>::max() && reject_bytes == std::numeric_limits<std::uint64_t>::max())
		{
			return;
		}
		if (warning_bytes == 0U || operating_bytes == 0U || reject_bytes == 0U || warning_bytes > operating_bytes || operating_bytes >= reject_bytes)
		{
			throw std::invalid_argument("multiway memory budget requires ordered non-zero limits");
		}
	}

	void MultiwayWorkingSetGateConfig::validate() const
	{
		if (hard_limit_bytes == 0U && resident_allowance_bytes != 0U)
			throw std::invalid_argument("working-set resident allowance requires a hard limit");
		if (private_commit_limit_bytes != 0U && private_commit_limit_bytes < hard_limit_bytes)
			throw std::invalid_argument("private-commit limit must be at least the working-set limit");
	}

	void MultiwayWorkingSetMonitor::validate() const
	{
		gate.validate();
		export_gate.validate();
		system_memory_guard.validate();
		if (single_file_blueprint_limit_bytes == 0U || shard_max_serialized_bytes == 0U)
			throw std::invalid_argument("multiway blueprint export file bounds must be nonzero");
		if (observer == nullptr && observer_context != nullptr)
			throw std::invalid_argument("working-set observer context requires an observer");
		if (export_process_observer == nullptr && export_system_observer == nullptr && export_observer_context != nullptr)
			throw std::invalid_argument("export observer context requires an export observer");
		if (system_memory_observer == nullptr && system_memory_observer_context != nullptr)
			throw std::invalid_argument("system memory observer context requires an observer");
	}

	MultiwayWorkingSetGateResult evaluate_multiway_working_set_preflight(
		const MultiwayMemoryPreflight& preflight,
		MultiwayWorkingSetGateConfig config) noexcept
	{
		MultiwayWorkingSetGateResult result;
		result.phase = MultiwayWorkingSetGatePhase::Preflight;
		result.hard_limit_bytes = config.hard_limit_bytes;
		result.resident_allowance_bytes = config.resident_allowance_bytes;
		result.estimated_working_set_bytes = preflight.estimate.estimated_working_set_peak_bytes;
		result.compared_working_set_bytes = multiway_memory_saturating_add(
			result.estimated_working_set_bytes, config.resident_allowance_bytes);
		result.admission_stage = preflight.highest_admitted_stage;
		if (preflight.status == MultiwayMemoryStatus::Rejected)
		{
			result.status = MultiwayWorkingSetGateStatus::Rejected;
			result.rejected_category = preflight.rejected_category;
			result.message = preflight.message;
			return result;
		}
		if (config.hard_limit_bytes == 0U)
		{
			result.status = MultiwayWorkingSetGateStatus::Disabled;
			result.message = "working-set gate disabled";
			return result;
		}
		if (result.compared_working_set_bytes > config.hard_limit_bytes || result.compared_working_set_bytes == std::numeric_limits<std::uint64_t>::max())
		{
			result.status = MultiwayWorkingSetGateStatus::Rejected;
			result.rejected_category = "process working set";
			result.message = "estimated working set plus resident allowance exceeds hard gate";
			return result;
		}
		result.status = MultiwayWorkingSetGateStatus::Admitted;
		result.message = "working-set preflight admitted";
		return result;
	}

	MultiwayWorkingSetGateResult evaluate_multiway_working_set_runtime(
		MultiwayProcessMemorySnapshot snapshot,
		MultiwayWorkingSetGateConfig config) noexcept
	{
		MultiwayWorkingSetGateResult result;
		result.phase = MultiwayWorkingSetGatePhase::Runtime;
		result.hard_limit_bytes = config.hard_limit_bytes;
		result.resident_allowance_bytes = config.resident_allowance_bytes;
		result.observed_private_commit_bytes = snapshot.private_committed_available
			? snapshot.private_committed_bytes
			: (snapshot.peak_private_committed_available ? snapshot.peak_private_committed_bytes : 0U);
		if (snapshot.private_committed_available && snapshot.peak_private_committed_available)
			result.observed_private_commit_bytes = std::max(result.observed_private_commit_bytes,
				snapshot.peak_private_committed_bytes);
		result.observed_working_set_bytes = snapshot.current_working_set_available
			? snapshot.current_working_set_bytes
			: (snapshot.peak_working_set_available ? snapshot.peak_working_set_bytes : 0U);
		if (snapshot.current_working_set_available && snapshot.peak_working_set_available)
			result.observed_working_set_bytes = std::max(result.observed_working_set_bytes, snapshot.peak_working_set_bytes);
		result.compared_working_set_bytes = multiway_memory_saturating_add(
			result.observed_working_set_bytes, config.resident_allowance_bytes);
		if (config.hard_limit_bytes == 0U)
		{
			result.status = MultiwayWorkingSetGateStatus::Disabled;
			result.message = "working-set gate disabled";
			return result;
		}
		if (!snapshot.current_working_set_available && !snapshot.peak_working_set_available)
		{
			result.status = MultiwayWorkingSetGateStatus::Unavailable;
			result.rejected_category = "process working set telemetry";
			result.message = "working-set metric unavailable";
			return result;
		}
		if (config.private_commit_limit_bytes != 0U && !snapshot.private_committed_available && !snapshot.peak_private_committed_available)
		{
			result.status = MultiwayWorkingSetGateStatus::Unavailable;
			result.rejected_category = "private commit telemetry";
			result.message = "private-commit metric unavailable";
			return result;
		}
		if (config.private_commit_limit_bytes != 0U && result.observed_private_commit_bytes > config.private_commit_limit_bytes)
		{
			result.status = MultiwayWorkingSetGateStatus::Rejected;
			result.rejected_category = "process private commit";
			result.message = "observed private commit exceeds hard gate";
			return result;
		}
		if (result.compared_working_set_bytes > config.hard_limit_bytes || result.compared_working_set_bytes == std::numeric_limits<std::uint64_t>::max())
		{
			result.status = MultiwayWorkingSetGateStatus::Rejected;
			result.rejected_category = "process working set";
			result.message = "observed working set plus resident allowance exceeds hard gate";
			return result;
		}
		result.status = MultiwayWorkingSetGateStatus::Admitted;
		result.message = "working-set runtime sample admitted";
		return result;
	}

	MultiwayMemoryPreflight preflight_multiway_memory(
		const MultiwaySolverLimits& limits,
		MultiwayMemoryBudget budget)
	{
		return preflight_multiway_memory(limits, budget, MultiwayMemoryInputs{});
	}

	std::uint64_t multiway_external_page_lookup_max_page_id(
		std::uint64_t maximum_rows, std::uint64_t maximum_values,
		std::uint32_t page_width)
	{
		if (page_width == 0U)
			throw std::invalid_argument("external page lookup index requires a positive page width");
		const auto additional_pages = maximum_values / page_width;
		if (additional_pages > std::numeric_limits<std::uint64_t>::max() - maximum_rows)
			throw std::length_error("external page lookup index bound overflows");
		return maximum_rows + additional_pages;
	}

	MultiwayCatalogCapacityEstimate estimate_multiway_catalog_capacity(
		std::uint64_t state_capacity,
		std::uint64_t menu_capacity,
		std::uint64_t action_capacity) noexcept
	{
#pragma pack(push, 8)
		struct StateIndexLayout
		{
			MultiwayPublicStateId id;
			std::size_t node_index;
		};
#pragma pack(pop)
#pragma pack(push, 8)
		struct MenuIndexLayout
		{
			std::uint64_t key;
			std::uint32_t handle;
		};
#pragma pack(pop)
#pragma pack(push, 8)
		struct PendingMenuIndexLayout
		{
			std::uint64_t key;
			std::uint32_t pending_index;
		};
#pragma pack(pop)
		MultiwayCatalogCapacityEstimate result;
		if (state_capacity > std::numeric_limits<std::size_t>::max() || menu_capacity > std::numeric_limits<std::size_t>::max() || action_capacity > std::numeric_limits<std::size_t>::max())
		{
			result.node_bytes = result.index_bytes = result.menu_header_bytes =
				result.menu_action_bytes = result.admission_scratch_bytes = result.total_bytes = std::numeric_limits<std::uint64_t>::max();
			return result;
		}
		result.node_bytes = multiway_memory_saturating_capacity_bytes(
			static_cast<std::size_t>(state_capacity), sizeof(MultiwayPackedPublicNode));
		result.index_bytes = multiway_memory_saturating_capacity_bytes(
			static_cast<std::size_t>(state_capacity), sizeof(StateIndexLayout));
		result.index_bytes = multiway_memory_saturating_add(result.index_bytes,
			multiway_memory_saturating_capacity_bytes(static_cast<std::size_t>(menu_capacity), sizeof(MenuIndexLayout)));
		result.menu_header_bytes = multiway_memory_saturating_capacity_bytes(
			static_cast<std::size_t>(menu_capacity), sizeof(std::vector<MultiwayActionDescriptor>));
		result.menu_action_bytes = multiway_memory_saturating_capacity_bytes(
			static_cast<std::size_t>(action_capacity), sizeof(MultiwayActionDescriptor));
		const auto per_state_scratch = sizeof(std::vector<MultiwayActionDescriptor>) + MULTIWAY_MAX_ABSTRACTED_ACTIONS * sizeof(MultiwayActionDescriptor) + sizeof(std::uint64_t) + 2U * sizeof(std::uint32_t) + 2U * sizeof(StateIndexLayout);
		result.admission_scratch_bytes = multiway_memory_saturating_capacity_bytes(
			static_cast<std::size_t>(state_capacity), per_state_scratch);
		result.admission_scratch_bytes = multiway_memory_saturating_add(result.admission_scratch_bytes,
			multiway_memory_saturating_capacity_bytes(static_cast<std::size_t>(menu_capacity),
				2U * sizeof(PendingMenuIndexLayout)));
		result.reallocation_peak_bytes = std::max(result.node_bytes,
			std::max(result.index_bytes, result.menu_header_bytes));
		result.total_bytes = multiway_memory_saturating_add(result.node_bytes, result.index_bytes);
		result.total_bytes = multiway_memory_saturating_add(result.total_bytes, result.menu_header_bytes);
		result.total_bytes = multiway_memory_saturating_add(result.total_bytes, result.menu_action_bytes);
		result.total_bytes = multiway_memory_saturating_add(result.total_bytes, result.admission_scratch_bytes);
		return result;
	}

	MultiwayMemoryPreflight preflight_multiway_memory(
		const MultiwaySolverLimits& limits,
		MultiwayMemoryBudget budget,
		const MultiwayMemoryInputs& inputs)
	{
		budget.validate();
		if (limits.worker_count != 0U && (limits.regret_delta_capacity() > std::numeric_limits<std::size_t>::max() / static_cast<std::size_t>(limits.worker_count) || limits.continuation_delta_capacity() > std::numeric_limits<std::size_t>::max() / static_cast<std::size_t>(limits.worker_count) || (limits.discovery_public_state_capacity() != 0U && limits.discovery_public_state_capacity() > std::numeric_limits<std::size_t>::max() / static_cast<std::size_t>(limits.worker_count)) || (limits.discovery_row_capacity() != 0U && limits.discovery_row_capacity() > std::numeric_limits<std::size_t>::max() / static_cast<std::size_t>(limits.worker_count))))
		{
			MultiwayMemoryPreflight result;
			result.status = MultiwayMemoryStatus::Rejected;
			result.estimate.total_bytes = std::numeric_limits<std::uint64_t>::max();
			result.rejected_category = "worker-stream capacity";
			result.message = "worker-stream capacity overflows addressable memory";
			return result;
		}
		limits.validate();

		MultiwayMemoryPreflight result;
		const auto existing_working_set_bytes = inputs.existing_working_set_bytes != 0U
			? inputs.existing_working_set_bytes
			: inputs.resident_baseline_bytes;
		const auto existing_private_committed_bytes = inputs.existing_private_committed_bytes;
		const auto mapped_virtual_bytes = inputs.mapped_virtual_bytes != 0U
			? inputs.mapped_virtual_bytes
			: inputs.mapped_bucket_artifact_bytes;
		const auto mapped_resident_allowance_bytes = inputs.mapped_resident_allowance_bytes;
		result.estimate.current_rss_bytes = existing_working_set_bytes;
		result.estimate.ledger.resident_baseline_bytes = inputs.resident_baseline_bytes;
		result.estimate.ledger.bucket_registry_bytes = inputs.bucket_registry_bytes;
		result.estimate.ledger.mapped_bucket_artifact_bytes = inputs.mapped_bucket_artifact_bytes;
		result.estimate.ledger.checkpoint_peak_bytes = inputs.checkpoint_peak_bytes;
		result.estimate.ledger.export_peak_bytes = inputs.export_peak_bytes;
		result.estimate.ledger.allocator_overhead_bytes = inputs.allocator_overhead_bytes;
		result.estimate.existing_working_set_bytes = existing_working_set_bytes;
		result.estimate.existing_private_committed_bytes = existing_private_committed_bytes;
		result.estimate.mapped_virtual_bytes = mapped_virtual_bytes;
		result.estimate.mapped_resident_allowance_bytes = mapped_resident_allowance_bytes;
		result.estimate.checkpoint_transient_bytes = inputs.checkpoint_peak_bytes;
		result.estimate.export_transient_bytes = inputs.export_peak_bytes;
		result.estimate.allocator_overhead_bytes = inputs.allocator_overhead_bytes;
		result.estimate.actual_retained_capacity_bytes = inputs.retained_capacity_known ? inputs.retained_capacity_bytes : 0U;
		result.estimate.ledger.existing_working_set_bytes = existing_working_set_bytes;
		result.estimate.ledger.existing_private_committed_bytes = existing_private_committed_bytes;
		result.estimate.ledger.mapped_virtual_bytes = mapped_virtual_bytes;
		result.estimate.ledger.mapped_resident_allowance_bytes = mapped_resident_allowance_bytes;
		result.estimate.ledger.checkpoint_transient_bytes = inputs.checkpoint_peak_bytes;
		result.estimate.ledger.export_transient_bytes = inputs.export_peak_bytes;
		result.estimate.ledger.actual_retained_capacity_bytes = result.estimate.actual_retained_capacity_bytes;
		const auto state_capacity = static_cast<std::uint64_t>(limits.max_public_states);
		const auto menu_capacity = inputs.public_catalog_menu_capacity != 0U
			? inputs.public_catalog_menu_capacity
			: state_capacity;
		const auto action_capacity = inputs.public_catalog_action_capacity != 0U
			? inputs.public_catalog_action_capacity
			: checked_multiply(state_capacity, MULTIWAY_MAX_ABSTRACTED_ACTIONS);
		const auto catalog = estimate_multiway_catalog_capacity(state_capacity, menu_capacity, action_capacity);
		result.estimate.public_state_bytes = checked_add(catalog.total_bytes, inputs.public_catalog_admission_scratch_bytes);
		result.estimate.public_catalog_reallocation_peak_bytes = catalog.reallocation_peak_bytes;
		result.estimate.sparse_row_bytes = checked_multiply(
			limits.max_sparse_rows, static_cast<std::uint64_t>(sizeof(MultiwaySparseRowMetadata)));
		if (limits.storage_backend == MultiwaySolverLimits::StorageBackend::CompactInt32)
			result.estimate.compact_page_bytes = checked_multiply(
				limits.max_sparse_values, MultiwayCompactStorage::page_metadata_size());
		const auto value_bytes_per_cell = limits.storage_backend == MultiwaySolverLimits::StorageBackend::CompactInt32
			? static_cast<std::uint64_t>(sizeof(std::int32_t) + sizeof(std::uint64_t))
			: 2U * static_cast<std::uint64_t>(sizeof(double));
		result.estimate.sparse_value_bytes = checked_multiply(limits.max_sparse_values, value_bytes_per_cell);
		result.estimate.blueprint_index_bytes = inputs.blueprint_index_bytes;
		result.estimate.range_row_bytes = checked_add(
			checked_multiply(inputs.range_row_count, sizeof(std::vector<MultiwayWeightedHole>)),
			checked_multiply(
				checked_multiply(inputs.range_entry_count, sizeof(MultiwayWeightedHole)), 2U));
		result.estimate.future_bucket_cache_bytes = inputs.future_bucket_cache_bytes;
		result.estimate.off_tree_menu_bytes = checked_multiply(
			inputs.off_tree_menu_entries, sizeof(MultiwayActionDescriptor));
		result.estimate.continuation_scratch_bytes = checked_multiply(
			limits.worker_count, inputs.continuation_scratch_bytes_per_worker);
		const auto aggregate_regret_entries = checked_multiply(
			limits.worker_count, limits.regret_delta_capacity());
		const auto aggregate_continuation_entries = checked_multiply(
			limits.worker_count, limits.continuation_delta_capacity());
		result.estimate.worker_delta_bytes = checked_add(
			checked_multiply(aggregate_regret_entries, static_cast<std::uint64_t>(sizeof(MultiwayWorkerDelta))),
			checked_multiply(
				limits.worker_count,
				static_cast<std::uint64_t>(
					sizeof(MultiwayPrivateWorkerScratch) + sizeof(MultiwayWorkerDeltaStream) + sizeof(MultiwayContinuationDeltaStream) + sizeof(std::thread))));
		result.estimate.worker_delta_bytes = checked_add(result.estimate.worker_delta_bytes,
			checked_multiply(aggregate_continuation_entries, sizeof(MultiwayContinuationDelta)));
		const auto aggregate_public_states = checked_multiply(
			limits.worker_count, limits.discovery_public_state_capacity());
		const auto aggregate_rows = checked_multiply(
			limits.worker_count, limits.discovery_row_capacity());
		result.estimate.discovery_stream_bytes = checked_add(
			checked_multiply(aggregate_public_states, sizeof(MultiwayPublicStateDescriptor)),
			checked_multiply(aggregate_rows, sizeof(MultiwaySparseRowShape)));
		result.estimate.merge_scratch_bytes = checked_add(
			checked_multiply(limits.max_sparse_values, MultiwaySolverCoordinator::pending_merge_cell_size()),
			checked_add(checked_multiply(
							aggregate_regret_entries,
							static_cast<std::uint64_t>(sizeof(MultiwayWorkerDelta) + 3U * sizeof(std::uint64_t) + sizeof(void*))),
				checked_multiply(aggregate_continuation_entries, sizeof(MultiwayContinuationDelta))));
		result.estimate.export_bytes = 0U; // Streamed export uses fixed scratch; output is disk/cache pressure.
		result.estimate.continuation_cache_bytes = inputs.continuation_cache_bytes;
		result.estimate.ledger.public_catalog_bytes = result.estimate.public_state_bytes;
		result.estimate.ledger.row_metadata_bytes = result.estimate.sparse_row_bytes;
		result.estimate.ledger.page_metadata_bytes = result.estimate.compact_page_bytes;
		result.estimate.ledger.regret_page_bytes = result.estimate.sparse_value_bytes;
		result.estimate.ledger.worker_stream_bytes = result.estimate.worker_delta_bytes;
		result.estimate.ledger.discovery_stream_bytes = result.estimate.discovery_stream_bytes;
		const auto transient_peak = std::max(inputs.checkpoint_peak_bytes, inputs.export_peak_bytes);

		const auto root_stage = checked_add(
			checked_add(result.estimate.public_state_bytes, result.estimate.blueprint_index_bytes),
			checked_add(
				checked_add(result.estimate.range_row_bytes, result.estimate.future_bucket_cache_bytes),
				checked_add(result.estimate.off_tree_menu_bytes, result.estimate.export_bytes)));
		const auto row_stage = checked_add(result.estimate.sparse_row_bytes,
			checked_add(result.estimate.compact_page_bytes, result.estimate.sparse_value_bytes));
		const auto delta_stage = checked_add(
			checked_add(checked_add(result.estimate.worker_delta_bytes, result.estimate.discovery_stream_bytes), result.estimate.merge_scratch_bytes),
			result.estimate.continuation_scratch_bytes);
		result.estimate.compact_reallocation_peak_bytes = inputs.compact_reallocation_peak_bytes != 0U
			? inputs.compact_reallocation_peak_bytes
			: checked_add(
				  checked_multiply(result.estimate.sparse_row_bytes, 2U),
				  checked_add(checked_multiply(result.estimate.compact_page_bytes, 2U),
					  result.estimate.sparse_value_bytes));
		result.estimate.file_cache_allowance_bytes = inputs.file_cache_allowance_bytes;
		result.estimate.disk_output_bytes = inputs.disk_output_bytes;
		result.estimate.external_storage_cache_bytes = inputs.external_storage_cache_bytes;
		result.estimate.external_storage_scratch_bytes = inputs.external_storage_scratch_bytes;
		result.estimate.external_storage_disk_reserve_bytes = inputs.external_storage_disk_reserve_bytes;
		result.estimate.configured_capacity_growth_bytes = checked_add(
			checked_add(checked_add(root_stage, row_stage), delta_stage),
			checked_add(result.estimate.external_storage_cache_bytes, result.estimate.external_storage_scratch_bytes));
		result.estimate.ledger.compact_reallocation_peak_bytes = result.estimate.compact_reallocation_peak_bytes;
		result.estimate.ledger.public_catalog_reallocation_peak_bytes = result.estimate.public_catalog_reallocation_peak_bytes;
		result.estimate.ledger.file_cache_allowance_bytes = result.estimate.file_cache_allowance_bytes;
		result.estimate.ledger.disk_output_bytes = result.estimate.disk_output_bytes;
		result.estimate.ledger.external_storage_cache_bytes = result.estimate.external_storage_cache_bytes;
		result.estimate.ledger.external_storage_scratch_bytes = result.estimate.external_storage_scratch_bytes;
		result.estimate.ledger.external_storage_disk_reserve_bytes = result.estimate.external_storage_disk_reserve_bytes;
		result.estimate.ledger.configured_capacity_growth_bytes = result.estimate.configured_capacity_growth_bytes;
		result.estimate.additional_private_capacity_bytes = inputs.retained_capacity_known
			? inputs.retained_capacity_bytes
			: result.estimate.configured_capacity_growth_bytes;
		result.estimate.ledger.additional_private_capacity_bytes = result.estimate.additional_private_capacity_bytes;
		const auto private_peak_without_optional_cache = checked_add(
			checked_add(
				checked_add(existing_private_committed_bytes,
					checked_add(result.estimate.additional_private_capacity_bytes,
						checked_add(result.estimate.public_catalog_reallocation_peak_bytes, result.estimate.compact_reallocation_peak_bytes))),
				transient_peak),
			inputs.allocator_overhead_bytes);
		const auto working_set_peak_without_optional_cache = checked_add(
			checked_add(
				checked_add(
					checked_add(existing_working_set_bytes,
						checked_add(result.estimate.additional_private_capacity_bytes,
							checked_add(result.estimate.public_catalog_reallocation_peak_bytes, result.estimate.compact_reallocation_peak_bytes))),
					mapped_resident_allowance_bytes),
				transient_peak),
			inputs.allocator_overhead_bytes);
		result.estimate.estimated_private_peak_bytes = checked_add(
			private_peak_without_optional_cache, result.estimate.continuation_cache_bytes);
		result.estimate.estimated_working_set_peak_bytes = checked_add(
			working_set_peak_without_optional_cache, result.estimate.continuation_cache_bytes);
		result.estimate.ledger.estimated_private_peak_bytes = result.estimate.estimated_private_peak_bytes;
		result.estimate.ledger.estimated_working_set_peak_bytes = result.estimate.estimated_working_set_peak_bytes;
		result.estimate.ledger.live_bytes = result.estimate.mandatory_bytes = std::max(
			private_peak_without_optional_cache, working_set_peak_without_optional_cache);
		result.estimate.ledger.reserved_bytes = result.estimate.mandatory_bytes;
		result.estimate.ledger.committed_bytes = private_peak_without_optional_cache;
		result.estimate.total_bytes = std::max(
			result.estimate.estimated_private_peak_bytes,
			result.estimate.estimated_working_set_peak_bytes);
		result.estimate.peak_rss_bytes = result.estimate.estimated_working_set_peak_bytes;

		const auto root_private_stage = checked_add(
			checked_add(existing_private_committed_bytes,
				checked_add(root_stage, result.estimate.public_catalog_reallocation_peak_bytes)),
			inputs.allocator_overhead_bytes);
		const auto root_working_set_stage = checked_add(
			checked_add(checked_add(existing_working_set_bytes,
							checked_add(root_stage, result.estimate.public_catalog_reallocation_peak_bytes)),
				mapped_resident_allowance_bytes),
			inputs.allocator_overhead_bytes);
		const auto root_admission_bytes = std::max(root_private_stage, root_working_set_stage);
		if (root_admission_bytes > budget.operating_bytes || root_admission_bytes >= budget.reject_bytes)
		{
			result.status = MultiwayMemoryStatus::Rejected;
			result.rejected_category = "root";
			result.message = "root admission exceeds memory limit";
			return result;
		}
		result.highest_admitted_stage = MultiwayMemoryAdmissionStage::Root;
		const auto through_rows_private = checked_add(root_private_stage,
			checked_add(row_stage, result.estimate.compact_reallocation_peak_bytes));
		const auto through_rows_working_set = checked_add(root_working_set_stage,
			checked_add(row_stage, result.estimate.compact_reallocation_peak_bytes));
		const auto through_rows = std::max(through_rows_private, through_rows_working_set);
		if (through_rows > budget.operating_bytes || through_rows >= budget.reject_bytes)
		{
			result.status = MultiwayMemoryStatus::Rejected;
			result.estimate.admitted_bytes = root_admission_bytes;
			result.rejected_category = "sparse rows";
			result.message = "sparse-row admission exceeds memory limit";
			return result;
		}
		result.highest_admitted_stage = MultiwayMemoryAdmissionStage::SparseRows;
		if (result.estimate.mandatory_bytes > budget.operating_bytes || result.estimate.mandatory_bytes >= budget.reject_bytes)
		{
			result.status = MultiwayMemoryStatus::Rejected;
			result.estimate.admitted_bytes = through_rows;
			result.rejected_category = "worker deltas";
			result.message = "worker-delta admission exceeds memory limit";
			return result;
		}
		result.highest_admitted_stage = MultiwayMemoryAdmissionStage::WorkerDeltas;
		result.estimate.admitted_bytes = result.estimate.mandatory_bytes;

		if (result.estimate.continuation_cache_bytes != 0U && (result.estimate.total_bytes > budget.operating_bytes || result.estimate.total_bytes >= budget.reject_bytes))
		{
			result.status = MultiwayMemoryStatus::Warning;
			result.degraded = true;
			result.rejected_category = "optional continuation cache";
			result.message = "optional continuation cache was not admitted";
			return result;
		}
		result.optional_continuation_cache_admitted =
			result.estimate.continuation_cache_bytes != 0U;
		if (result.optional_continuation_cache_admitted)
		{
			result.highest_admitted_stage = MultiwayMemoryAdmissionStage::OptionalContinuationCache;
			result.estimate.admitted_bytes = result.estimate.total_bytes;
		}

		if (result.estimate.admitted_bytes >= budget.warning_bytes)
		{
			result.status = MultiwayMemoryStatus::Warning;
			result.message = "estimated memory exceeds warning limit";
		}
		return result;
	}

} // namespace texas::solver::multiway
