#pragma once

/** @file
 * @brief Sampling, storage and memory-guard configuration for HUNL solvers.
 */

#include "solver/hunl/sampled/hunl_sampled_config_types.hpp"
#include "solver/hunl/flat/hunl_flat_state.hpp"

#include <cstddef>
#include <cstdint>
#include <stdexcept>

namespace texas::solver::hunl
{
	/** @brief Validates a sampled-solver configuration without throwing. */
	[[nodiscard]] HUNLSampledConfigValidation validate_sampled_config(
		const HUNLSampledSolverConfig& config) noexcept;
	/** @brief Validates sampled-solver settings and throws on failure. */
	void validate_sampled_config_or_throw(const HUNLSampledSolverConfig& config);
} // namespace texas::solver::hunl
