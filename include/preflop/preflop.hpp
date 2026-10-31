#pragma once

/** @file
 * @brief Declares the preflop HUNL solve wrapper and its runtime output.
 */

#include "core/legacy_namespace_compat.hpp"

#include "core/types.hpp"
#include "games/hunl.hpp"
#include "preflop/preflop_equity.hpp"
#include "solver/generic/solver.hpp"

namespace texas::preflop
{

	/** @brief Preflop solver output with runtime metadata. */
#pragma pack(push, 8)
	struct PreflopSolveOutput
	{
		SolveOutput base;				 ///< Generic solver result and strategy data.
		std::uint32_t infoset_count = 0; ///< Number of returned information sets.
		double wallclock_seconds = 0.0;	 ///< Total wrapper elapsed time in seconds.
	};
#pragma pack(pop)

	/** @brief Solves the HUNL preflop subgame using the supplied DCFR parameters. */
	PreflopSolveOutput solve_hunl_preflop(const HUNLConfig& config, std::uint32_t iterations, double alpha, double beta, double gamma);

} // namespace texas::preflop
