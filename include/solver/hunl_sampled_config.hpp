#pragma once

#include "solver/hunl_flat_state.hpp"

#include <cstddef>
#include <cstdint>
#include <stdexcept>

namespace core {

enum class HUNLFlatSamplingMode : std::uint8_t {
    Exact = 0,
    PublicChance = 1,
    External = 2,
    AverageStrategy = 3,
};

enum class HUNLFlatBaselineMode : std::uint8_t {
    None = 0,
    MovingAverage = 1,
    DepthLimitedValueTable = 2,
    TerminalBoardCache = 3,
};

struct HUNLSampledSolverConfig {
    std::uint64_t seed = 1;
    std::uint32_t minibatch_size = 64;
    std::uint32_t max_cached_public_states = 0;
    std::uint32_t bucket_count_hint = 0;
    // Optional preflight hint. The structured root remains authoritative; a
    // non-zero selected cutoff requires HUNLSampledSolveRequest::leaf_evaluator.
    std::uint32_t depth_limit_plies_hint = 0;
    HUNLFlatStoragePrecision precision = HUNLFlatStoragePrecision::Float32;
    HUNLFlatValueLayout layout = HUNLFlatValueLayout::InfosetActionHand;
    // Disabled until private-hand/range suit remapping proves closure and equal reach.
    bool use_public_chance_isomorphism = false;
    bool enable_memory_guardrails = true;
    bool adaptive_memory_fallback = true;
    std::size_t workers = 1;
    std::uint64_t memory_warning_bytes = 48ULL * 1024ULL * 1024ULL * 1024ULL;
    std::uint64_t memory_fail_bytes = 60ULL * 1024ULL * 1024ULL * 1024ULL;
    // Regression-only worker failure injection for transactional session tests.
    std::int32_t test_throw_worker_index = -1;
};

struct HUNLFlatMCCFRConfig {
    HUNLFlatSamplingMode mode = HUNLFlatSamplingMode::External;
    std::uint64_t seed = 1;
    std::uint32_t traversals_per_iteration = 1024;
    std::uint32_t batch_size = 64;
    double as_epsilon = 0.05;
    double as_tau = 1000.0;
    double as_beta = 1e6;
    bool update_both_players = true;
    bool use_discounting = false;
    double dcfr_alpha = 1.5;
    double dcfr_beta = 0.0;
    double dcfr_gamma = 2.0;
    bool use_sparse_storage = false;
    bool keep_dense_validation_backend = false;
    HUNLFlatBaselineMode baseline_mode = HUNLFlatBaselineMode::None;
    bool use_iterative_external_dense_traversal = false;
    // Regression-only fault injection. A non-negative worker index throws at
    // the start of that worker's batch so thread exception propagation can be
    // tested without corrupting a game graph.
    std::int32_t test_throw_worker_index = -1;
};

struct HUNLSampledConfigValidation {
    bool ok = true;
    const char* message = "";
};

[[nodiscard]] HUNLSampledConfigValidation validate_sampled_config(
    const HUNLSampledSolverConfig& config) noexcept;
void validate_sampled_config_or_throw(const HUNLSampledSolverConfig& config);

}  // namespace core
