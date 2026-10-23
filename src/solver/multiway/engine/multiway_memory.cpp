#include "solver/multiway/engine/multiway_memory.hpp"
#include "solver/multiway/continuation/multiway_continuation_selector.hpp"

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
#endif

	} // namespace

	bool MultiwayProcessMemorySnapshot::valid() const noexcept
	{
		if ((!current_working_set_available && current_working_set_bytes != 0U) ||
			(!peak_working_set_available && peak_working_set_bytes != 0U) ||
			(!private_committed_available && private_committed_bytes != 0U) ||
			(!peak_private_committed_available && peak_private_committed_bytes != 0U) ||
			(!virtual_address_available && virtual_address_bytes != 0U) ||
			(!anonymous_resident_available && anonymous_resident_bytes != 0U) ||
			(!file_resident_available && file_resident_bytes != 0U))
		{
			return false;
		}
		return (!current_working_set_available || !peak_working_set_available ||
			peak_working_set_bytes >= current_working_set_bytes) &&
			(!private_committed_available || !peak_private_committed_available ||
				peak_private_committed_bytes >= private_committed_bytes);
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
		}
		return true;
	}

	const char* multiway_memory_phase_name(MultiwayMemoryPhase phase) noexcept
	{
		switch (phase)
		{
			case MultiwayMemoryPhase::ProcessStart: return "process_start";
			case MultiwayMemoryPhase::BeforeBucketMap: return "before_bucket_map";
			case MultiwayMemoryPhase::AfterBucketMapMetadata: return "after_bucket_map_metadata";
			case MultiwayMemoryPhase::AfterBucketAssignmentValidation: return "after_bucket_assignment_validation";
			case MultiwayMemoryPhase::BeforeMemoryPreflight: return "before_memory_preflight";
			case MultiwayMemoryPhase::AfterCoordinatorConstruction: return "after_coordinator_construction";
			case MultiwayMemoryPhase::AfterWorkerPoolConstruction: return "after_worker_pool_construction";
			case MultiwayMemoryPhase::AfterColdBatch: return "after_cold_batch";
			case MultiwayMemoryPhase::AfterSteadyBatch: return "after_steady_batch";
			case MultiwayMemoryPhase::BeforeCheckpoint: return "before_checkpoint";
			case MultiwayMemoryPhase::AfterCheckpoint: return "after_checkpoint";
			case MultiwayMemoryPhase::BeforeExport: return "before_export";
			case MultiwayMemoryPhase::AfterExport: return "after_export";
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
				GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&counters), sizeof(counters)) != 0)
		{
			result.current_working_set_bytes = static_cast<std::uint64_t>(counters.WorkingSetSize);
			result.peak_working_set_bytes = static_cast<std::uint64_t>(counters.PeakWorkingSetSize);
			result.private_committed_bytes = static_cast<std::uint64_t>(counters.PrivateUsage);
			result.current_working_set_available = true;
			result.peak_working_set_available = true;
			result.private_committed_available = true;
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
#endif
		return result;
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

	MultiwayMemoryPreflight preflight_multiway_memory(
		const MultiwaySolverLimits& limits,
		MultiwayMemoryBudget budget)
	{
		return preflight_multiway_memory(limits, budget, MultiwayMemoryInputs{});
	}

	MultiwayMemoryPreflight preflight_multiway_memory(
		const MultiwaySolverLimits& limits,
		MultiwayMemoryBudget budget,
		const MultiwayMemoryInputs& inputs)
	{
		budget.validate();
		if (limits.worker_count != 0U && limits.max_worker_delta_entries > std::numeric_limits<std::size_t>::max() / static_cast<std::size_t>(limits.worker_count))
		{
			MultiwayMemoryPreflight result;
			result.status = MultiwayMemoryStatus::Rejected;
			result.estimate.total_bytes = std::numeric_limits<std::uint64_t>::max();
			result.message = "worker-delta capacity overflows addressable memory";
			return result;
		}
		limits.validate();

		MultiwayMemoryPreflight result;
		result.estimate.current_rss_bytes = inputs.resident_baseline_bytes;
		result.estimate.ledger.resident_baseline_bytes = inputs.resident_baseline_bytes;
		result.estimate.ledger.bucket_registry_bytes = inputs.bucket_registry_bytes;
		result.estimate.ledger.mapped_bucket_artifact_bytes = inputs.mapped_bucket_artifact_bytes;
		result.estimate.ledger.checkpoint_peak_bytes = inputs.checkpoint_peak_bytes;
		result.estimate.ledger.export_peak_bytes = inputs.export_peak_bytes;
		result.estimate.ledger.allocator_overhead_bytes = inputs.allocator_overhead_bytes;
		// Public descriptors contain vectors. Reserve a conservative 1 KiB per
		// admitted state before the packed catalog replaces this compatibility
		// representation.
		result.estimate.public_state_bytes = checked_multiply(limits.max_public_states, 1024U);
		result.estimate.sparse_row_bytes = checked_multiply(
			limits.max_sparse_rows, static_cast<std::uint64_t>(sizeof(MultiwaySparseRowMetadata)));
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
		result.estimate.worker_delta_bytes = checked_add(
			checked_multiply(
				checked_multiply(limits.worker_count, limits.max_worker_delta_entries),
				static_cast<std::uint64_t>(sizeof(MultiwayWorkerDelta))),
			checked_multiply(
				limits.worker_count,
				static_cast<std::uint64_t>(
					sizeof(MultiwayPrivateWorkerScratch) + sizeof(MultiwayWorkerDeltaStream) + sizeof(MultiwayContinuationDeltaStream) + sizeof(std::thread))));
		result.estimate.worker_delta_bytes = checked_add(result.estimate.worker_delta_bytes,
			checked_multiply(checked_multiply(limits.worker_count, limits.max_worker_delta_entries),
				sizeof(MultiwayContinuationDelta)));
		const auto aggregate_discovery_entries = checked_multiply(
			limits.worker_count, limits.max_discovery_entries);
		result.estimate.discovery_stream_bytes = checked_multiply(
			aggregate_discovery_entries,
			static_cast<std::uint64_t>(sizeof(MultiwayPublicStateDescriptor) + sizeof(MultiwaySparseRowShape)));
		const auto aggregate_delta_entries = checked_multiply(
			limits.worker_count, limits.max_worker_delta_entries);
		result.estimate.merge_scratch_bytes = checked_multiply(
			aggregate_delta_entries,
			static_cast<std::uint64_t>(sizeof(MultiwayWorkerDelta) + sizeof(MultiwayContinuationDelta) + 3U * sizeof(std::uint64_t) + sizeof(void*)));
		result.estimate.export_bytes = checked_multiply(
			inputs.export_action_capacity,
			2U * static_cast<std::uint64_t>(sizeof(MultiwayRootActionProbability)));
		result.estimate.continuation_cache_bytes = inputs.continuation_cache_bytes;
		result.estimate.ledger.public_catalog_bytes = result.estimate.public_state_bytes;
		result.estimate.ledger.row_metadata_bytes = result.estimate.sparse_row_bytes;
		result.estimate.ledger.regret_page_bytes = result.estimate.sparse_value_bytes;
		result.estimate.ledger.worker_stream_bytes = result.estimate.worker_delta_bytes;
		result.estimate.ledger.discovery_stream_bytes = result.estimate.discovery_stream_bytes;
		const auto transient_peak = std::max(inputs.checkpoint_peak_bytes, inputs.export_peak_bytes);

		const auto root_stage = checked_add(
			checked_add(result.estimate.public_state_bytes, result.estimate.blueprint_index_bytes),
			checked_add(
				checked_add(result.estimate.range_row_bytes, result.estimate.future_bucket_cache_bytes),
				checked_add(result.estimate.off_tree_menu_bytes, result.estimate.export_bytes)));
		const auto row_stage = checked_add(
			result.estimate.sparse_row_bytes, result.estimate.sparse_value_bytes);
		const auto delta_stage = checked_add(
			checked_add(checked_add(result.estimate.worker_delta_bytes, result.estimate.discovery_stream_bytes), result.estimate.merge_scratch_bytes),
			result.estimate.continuation_scratch_bytes);
		result.estimate.mandatory_bytes = checked_add(
			checked_add(
				checked_add(checked_add(inputs.resident_baseline_bytes, inputs.bucket_registry_bytes), inputs.mapped_bucket_artifact_bytes),
				checked_add(root_stage, row_stage)),
			checked_add(
				checked_add(delta_stage, transient_peak),
				inputs.allocator_overhead_bytes));
		result.estimate.ledger.live_bytes = result.estimate.mandatory_bytes;
		result.estimate.ledger.reserved_bytes = result.estimate.mandatory_bytes;
		result.estimate.ledger.committed_bytes = result.estimate.mandatory_bytes;
		result.estimate.total_bytes = checked_add(
			result.estimate.mandatory_bytes, result.estimate.continuation_cache_bytes);

		if (root_stage > budget.operating_bytes || root_stage >= budget.reject_bytes)
		{
			result.status = MultiwayMemoryStatus::Rejected;
			result.message = "root admission exceeds memory limit";
			return result;
		}
		result.highest_admitted_stage = MultiwayMemoryAdmissionStage::Root;
		const auto through_rows = checked_add(root_stage, row_stage);
		if (through_rows > budget.operating_bytes || through_rows >= budget.reject_bytes)
		{
			result.status = MultiwayMemoryStatus::Rejected;
			result.estimate.admitted_bytes = root_stage;
			result.message = "sparse-row admission exceeds memory limit";
			return result;
		}
		result.highest_admitted_stage = MultiwayMemoryAdmissionStage::SparseRows;
		if (result.estimate.mandatory_bytes > budget.operating_bytes || result.estimate.mandatory_bytes >= budget.reject_bytes)
		{
			result.status = MultiwayMemoryStatus::Rejected;
			result.estimate.admitted_bytes = through_rows;
			result.message = "worker-delta admission exceeds memory limit";
			return result;
		}
		result.highest_admitted_stage = MultiwayMemoryAdmissionStage::WorkerDeltas;
		result.estimate.admitted_bytes = result.estimate.mandatory_bytes;

		if (result.estimate.continuation_cache_bytes != 0U && (result.estimate.total_bytes > budget.operating_bytes || result.estimate.total_bytes >= budget.reject_bytes))
		{
			result.status = MultiwayMemoryStatus::Warning;
			result.degraded = true;
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
