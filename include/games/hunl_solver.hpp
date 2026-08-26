#pragma once

#include "games/hunl.hpp"
#include "core/types.hpp"
#include "solver/parallel_dcfr.hpp"
#include "solver/hunl_leaf_evaluator.hpp"

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace texas::solver::hunl {

enum class HUNLQualityMetric : std::uint8_t {
    PerPlayerExploitability,
};

// Immutable public resolver boundary. The state contains exact chips, actor,
// pending call, raise state, board, and canonical betting history. The
// request's range/configuration metadata is rebound before private deals are
// attached, so callers cannot mutate a live state after admission.
struct HUNLLiveRootSnapshot {
    HUNLState public_state;
    std::vector<ActionId> legal_actions;
    std::string canonical_public_history;
    std::string state_version;

    void validate(const HUNLConfig& config) const;
    [[nodiscard]] HUNLState bind_config(std::shared_ptr<const HUNLConfig> config) const;
};

struct HUNLStructuredRootRequest {
    HUNLConfig config;
    // Absent roots retain the legacy config-derived balanced entry. Unequal
    // contributions and live/off-tree roots require this full snapshot.
    std::optional<HUNLLiveRootSnapshot> live_root = std::nullopt;
    // Traversal normalizes terminal values from its internal big-blind unit at
    // the root boundary before comparing them with leaf values.
    HUNLLeafValueUnits value_units = HUNLLeafValueUnits::Chips;
    // Provenance for the externally selected blueprint. The sampled solver
    // starts unseeded (uniform regret matching); it never silently treats a
    // version string as a prior strategy or leaf value table.
    std::string blueprint_version;
    std::string model_version;
    // The private hand whose acting-root strategy is exported. Bucket zero is
    // the only current structured-range bucket; keeping it explicit prevents
    // a future abstraction from silently changing the export contract.
    struct HeroSelection {
        PlayerId player = -1;
        std::array<std::uint8_t, 2> hole_cards = {0, 0};
        std::uint32_t bucket = 0;
    };
    std::optional<HeroSelection> hero_selection = std::nullopt;
    // Optional diagnostic only. It never replaces `root_strategy`.
    bool include_range_wide_root_diagnostics = false;

    [[nodiscard]] std::vector<HUNLJointRangeDeal> normalized_joint_range() const;
    [[nodiscard]] HUNLState public_root_state(std::shared_ptr<const HUNLConfig> config) const;
    [[nodiscard]] HeroSelection effective_hero_selection() const;
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

HUNLSolveOutput solve_hunl_postflop(
    const HUNLConfig& config,
    std::uint32_t iterations,
    double alpha,
    double beta,
    double gamma,
    std::size_t workers = 1,
    std::size_t frontier_multiplier = 8,
    bool force_parallel = false,
    HUNLBackendSelection backend = HUNLBackendSelection::Auto);

void validate_config(const HUNLConfig& config);
void validate_structured_root_request(const HUNLStructuredRootRequest& request);

}  // namespace texas::solver::hunl


