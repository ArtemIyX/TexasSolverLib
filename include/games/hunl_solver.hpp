#pragma once

#include "games/hunl.hpp"
#include "core/types.hpp"
#include "solver/parallel_dcfr.hpp"
#include "solver/hunl_leaf_evaluator.hpp"

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace core {

enum class HUNLQualityMetric : std::uint8_t {
    PerPlayerExploitability,
};

struct HUNLStructuredRootRequest {
    HUNLConfig config;
    HUNLLeafValueUnits value_units = HUNLLeafValueUnits::Chips;
    // Provenance for the externally selected blueprint. The sampled solver
    // starts unseeded (uniform regret matching); it never silently treats a
    // version string as a prior strategy or leaf value table.
    std::string blueprint_version;
    std::string model_version;

    [[nodiscard]] std::vector<HUNLJointRangeDeal> normalized_joint_range() const;
    void validate() const;
};

struct HUNLSolveOutput {
    std::unordered_map<std::string, std::vector<double>> average_strategy;
    double exploitability = 0.0;
    double total_nash_conv = 0.0;
    HUNLQualityMetric quality_metric = HUNLQualityMetric::PerPlayerExploitability;
    double game_value = 0.0;
    std::uint32_t iterations = 0;
    double wallclock_seconds = 0.0;
    double traversal_seconds = 0.0;
    double solver_finalize_seconds = 0.0;
    double wrapper_postprocess_seconds = 0.0;
    std::uint32_t infoset_count = 0;
    bool used_parallel = false;
    SolveProfile profile;
};

enum class HUNLSolveError {
    PreflopNotSupported = 0,
    BoardLengthMismatch = 1,
    RakeNonZero = 2,
    InvalidConfig = 3,
};

enum class HUNLBackendSelection {
    Auto = 0,
    Recursive = 1,
    Flat = 2,
};

HUNLBackendSelection hunl_backend_selection_from_env();

HUNLSolveOutput solve_hunl_postflop(
    const HUNLConfig& config,
    std::uint32_t iterations,
    double alpha,
    double beta,
    double gamma,
    std::size_t workers = 1,
    std::size_t frontier_multiplier = 8,
    bool force_parallel = false);

void validate_config(const HUNLConfig& config);
void validate_structured_root_request(const HUNLStructuredRootRequest& request);

}  // namespace core


