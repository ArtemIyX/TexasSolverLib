#pragma once

/** @file
 * Required PB-12 public-state coverage keys and admission helpers.
 */

#include "solver/multiway/engine/multiway_solver.hpp"

#include <array>
#include <cstdint>
#include <filesystem>

namespace texas::solver::multiway
{
	class MultiwayActionAbstraction;
	struct MultiwayModelIdentity;

	/** Stable public state, seat, bucket, and menu key for required coverage. */
	struct MultiwayPB12CoverageKey
	{
		std::uint64_t public_state_id = 0U;
		PlayerId seat = -1;
		std::uint32_t bucket = 0U;
		std::uint64_t action_menu_id = 0U;
	};

	inline constexpr std::array<MultiwayPB12CoverageKey, 8U> MULTIWAY_PB12_COVERAGE_KEYS = { {
		{ 5161244728243252751ULL, 2, 96U, 16789259325962222034ULL },
		{ 13280906303883242178ULL, 3, 116U, 14734177828830091388ULL },
		{ 6142276652331038516ULL, 4, 131U, 14734177828830091388ULL },
		{ 4340285920445306709ULL, 5, 88U, 17638026342764028254ULL },
		{ 1178002496505808984ULL, 2, 96U, 16789259325962222034ULL },
		{ 9932208853613335582ULL, 3, 116U, 14734177828830091388ULL },
		{ 4025415126983093512ULL, 4, 131U, 14734177828830091388ULL },
		{ 6209571308954858702ULL, 5, 88U, 17638026342764028254ULL },
	} };

	/** Public descriptors admitted for the configured PB-12 coverage targets. */
	struct MultiwayPB12CoverageAdmission
	{
		std::array<MultiwayPublicStateDescriptor, MULTIWAY_PB12_COVERAGE_KEYS.size()> target_states{};
	};

	/** Builds and admits all targeted public states into the coordinator. */
	[[nodiscard]] MultiwayPB12CoverageAdmission admit_multiway_pb12_coverage(
		const MultiwayRootSnapshot& root,
		const MultiwayActionAbstraction& abstraction,
		MultiwaySolverCoordinator& coordinator);

	/** Checks a policy artifact contains expected PB-12 keys for its identity. */
	void verify_multiway_pb12_policy_file(
		const std::filesystem::path& policy_path,
		const MultiwayModelIdentity& identity);

} // namespace texas::solver::multiway
