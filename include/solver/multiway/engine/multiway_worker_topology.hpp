#pragma once

/** @file
 * Worker topology discovery and deterministic processor-placement planning.
 */

#include <cstdint>
#include <limits>
#include <vector>

namespace texas::solver::multiway
{

	inline constexpr std::uint32_t kMultiwayUnknownTopologyId =
		(std::numeric_limits<std::uint32_t>::max)();
	inline constexpr std::uint16_t kMultiwayUnknownTopologyIndex =
		(std::numeric_limits<std::uint16_t>::max)();

	/** Source used to construct a worker processor inventory. */
	enum class MultiwayWorkerTopologySource : std::uint8_t
	{
		Synthetic,
		Windows,
		Fallback,
	};

	/** Topology identifiers and allocation state for one logical processor. */
	struct MultiwayWorkerTopologyRecord
	{
		std::uint32_t cpu_set_id = kMultiwayUnknownTopologyId;
		std::uint16_t processor_group = 0U;
		std::uint16_t logical_processor_index = 0U;
		std::uint32_t physical_core_id = kMultiwayUnknownTopologyId;
		std::uint16_t smt_sibling_index = kMultiwayUnknownTopologyIndex;
		std::uint16_t numa_node_id = kMultiwayUnknownTopologyIndex;
		std::uint16_t cache_domain_id = kMultiwayUnknownTopologyIndex;
		std::uint8_t efficiency_class = 0U;
		std::uint8_t cpu_set_flags = 0U;
		std::uint32_t allocation_tag = 0U;
		bool is_parked = false;
		bool is_allocated = false;

		[[nodiscard]] constexpr std::uint32_t logical_processor_identity() const noexcept
		{
			return (static_cast<std::uint32_t>(processor_group) << 16U) | static_cast<std::uint32_t>(logical_processor_index);
		}
	};

	/** Processor records and provenance returned by topology discovery. */
	struct MultiwayWorkerTopologyInventory
	{
		MultiwayWorkerTopologySource source = MultiwayWorkerTopologySource::Synthetic;
		std::vector<MultiwayWorkerTopologyRecord> processors;

		[[nodiscard]] bool empty() const noexcept
		{
			return processors.empty();
		}

		[[nodiscard]] bool is_fallback() const noexcept
		{
			return source == MultiwayWorkerTopologySource::Fallback;
		}

		/** Reports whether all required Windows topology identifiers are present. */
		[[nodiscard]] bool has_complete_windows_topology() const noexcept;

		/** Finds a processor by group and logical index, or returns null. */
		[[nodiscard]] const MultiwayWorkerTopologyRecord* find(
			std::uint16_t processor_group, std::uint16_t logical_processor_index) const noexcept;
	};

	/** Strategy for mapping solver workers to discovered processors. */
	enum class MultiwayWorkerPlacementPolicy : std::uint8_t
	{
		OsDefault,
		PhysicalCompact,
		PhysicalSpread,
		CpuSetHint,
		DiagnosticHardAffinity,
	};

	/** Returns a stable display name for a placement policy. */
	[[nodiscard]] const char* multiway_worker_placement_policy_name(
		MultiwayWorkerPlacementPolicy policy) noexcept;
	/** Parses a placement-policy name and rejects unsupported values. */
	[[nodiscard]] MultiwayWorkerPlacementPolicy parse_multiway_worker_placement_policy(
		const char* value);

	/** Requested placement policy and migration-sampling interval. */
	struct MultiwayWorkerPlacementConfig
	{
		MultiwayWorkerPlacementPolicy policy = MultiwayWorkerPlacementPolicy::OsDefault;
		// Zero disables migration sampling. Sampling is performed only at batch
		// boundaries, never from traversal or merge code.
		std::uint32_t migration_sample_interval_batches = 0U;

		/** Checks that the selected policy and sampling interval are supported. */
		void validate() const;
	};

	/** Resolved processor requests for the coordinator and solver workers. */
	struct MultiwayWorkerPlacementPlan
	{
		MultiwayWorkerPlacementPolicy policy = MultiwayWorkerPlacementPolicy::OsDefault;
		bool topology_available = false;
		bool coordinator_reserved = false;
		std::uint32_t requested_worker_count = 0U;
		std::uint32_t resolved_worker_count = 0U;
		std::uint32_t coordinator_logical_processor_identity = kMultiwayUnknownTopologyId;
		std::uint32_t coordinator_physical_core_id = kMultiwayUnknownTopologyId;
		std::vector<MultiwayWorkerTopologyRecord> requested;
	};

	/** Creates an inventory from caller-provided processor records. */
	[[nodiscard]] MultiwayWorkerTopologyInventory make_multiway_worker_topology(
		std::vector<MultiwayWorkerTopologyRecord> records,
		MultiwayWorkerTopologySource source = MultiwayWorkerTopologySource::Synthetic);

	// This fallback intentionally reports only unique logical identities. It never
	// infers physical cores, SMT siblings, NUMA nodes, or cache domains.
	[[nodiscard]] MultiwayWorkerTopologyInventory make_multiway_worker_topology_fallback(
		std::uint32_t logical_processor_count);

	// Cold-path discovery. It has no callers in traversal, row-update, terminal,
	// or merge loops.
	[[nodiscard]] MultiwayWorkerTopologyInventory discover_multiway_worker_topology();

	// Cold-path, deterministic placement selection. The coordinator identity is
	// excluded by physical core when that identity is available.
	/** Resolves stable worker placements using inventory, policy, and coordinator. */
	[[nodiscard]] MultiwayWorkerPlacementPlan resolve_multiway_worker_placement(
		const MultiwayWorkerTopologyInventory& inventory,
		std::uint32_t worker_count,
		MultiwayWorkerPlacementPolicy policy,
		std::uint32_t coordinator_logical_processor_identity = kMultiwayUnknownTopologyId);

} // namespace texas::solver::multiway
