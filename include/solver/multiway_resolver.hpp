#pragma once

#include "games/multiway_private.hpp"
#include "solver/multiway_model_identity.hpp"
#include "solver/multiway_solver.hpp"

#include <array>
#include <chrono>
#include <cstdint>
#include <vector>

namespace core {

enum class MultiwayInferenceMode : std::uint8_t {
    AnonymousWithinHand,
    BlockersOnly,
};

// A range supplied by a resolver caller for one non-hero seat. Seat ids are
// explicit so the request remains valid after players have folded.
struct MultiwayResolverSeatRange {
    PlayerId seat = -1;
    std::vector<MultiwayWeightedHole> hands;
};

// Immutable in-process resolver boundary. public_state must describe the
// exact current public game state; generic ranges are intentionally separate
// from the known hero hole cards.
struct MultiwayResolverRequest {
    MultiwayModelIdentity blueprint_identity{};
    MultiwayPublicStateDescriptor public_state{};
    PlayerId hero_seat = -1;
    std::array<std::uint8_t, 2> hero_cards = {0, 0};
    std::vector<MultiwayResolverSeatRange> opponent_ranges;
    std::chrono::steady_clock::time_point deadline{};
    MultiwayInferenceMode inference_mode = MultiwayInferenceMode::AnonymousWithinHand;
    std::uint64_t sampling_seed = 1;
};

struct MultiwayResolverActionProbability {
    MultiwayActionDescriptor action{};
    double probability = 0.0;
};

enum class MultiwayResolverStatus : std::uint8_t {
    Solved,
    DeadlineFallback,
    InvalidRequest,
    ArtifactMismatch,
    ResourceExhausted,
};

struct MultiwayResolverDiagnostics {
    MultiwayResolverStatus status = MultiwayResolverStatus::InvalidRequest;
    std::uint64_t completed_batches = 0;
    std::uint64_t completed_trajectories = 0;
    bool deadline_expired = false;
    bool used_fallback = false;
    bool policy_normalized = false;
};

struct MultiwayResolverResult {
    MultiwayActionDescriptor sampled_action{};
    bool has_sampled_action = false;
    std::vector<MultiwayResolverActionProbability> policy;
    MultiwayResolverDiagnostics diagnostics{};
};

// Phase 5 supplies the deadline-safe implementation. This declaration keeps
// the public request/result contract stable without adding resolver behavior.
class MultiwayResolver {
public:
    [[nodiscard]] MultiwayResolverResult resolve(const MultiwayResolverRequest& request) const;
};

}  // namespace core
