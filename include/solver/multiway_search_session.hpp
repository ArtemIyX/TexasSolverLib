#pragma once

#include "solver/multiway_bucket_model.hpp"
#include "solver/multiway_range_belief.hpp"
#include "solver/multiway_solver.hpp"

#include <cstdint>
#include <vector>

namespace core {

// Borrowed immutable, caller-owned artifact input. A bucket registry is
// required for postflop roots and optional for preflop roots. This P1.4
// session does not traverse postflop states.
struct MultiwaySearchSessionDependencies {
    const MultiwayBucketRegistry* buckets = nullptr;
};

struct MultiwaySearchSessionRootMetadata {
    MultiwayPublicStateId public_state{};
    MultiwayActionAbstractionIdentity action_abstraction{};
    std::uint64_t revision = 0U;
};

// One request-local owner for mutable runtime search state. It never retains
// caller data after construction and does not own immutable artifacts.
class MultiwaySearchSession {
public:
    MultiwaySearchSession(
        const MultiwaySolveRequest& request,
        MultiwaySearchSessionDependencies dependencies,
        std::uint64_t public_root_revision);

    MultiwaySearchSession(const MultiwaySearchSession&) = delete;
    MultiwaySearchSession& operator=(const MultiwaySearchSession&) = delete;
    MultiwaySearchSession(MultiwaySearchSession&&) = delete;
    MultiwaySearchSession& operator=(MultiwaySearchSession&&) = delete;

    [[nodiscard]] MultiwaySolverCoordinator& coordinator() noexcept { return coordinator_; }
    [[nodiscard]] const MultiwaySolverCoordinator& coordinator() const noexcept { return coordinator_; }
    [[nodiscard]] MultiwayRangeBeliefView belief(PlayerId seat) const;
    [[nodiscard]] MultiwayRangeBeliefUpdateResult apply_observation(
        PlayerId seat,
        const MultiwayRangeBeliefObservation& observation);
    [[nodiscard]] const MultiwayBucketRegistry* buckets() const noexcept { return buckets_; }
    [[nodiscard]] const std::vector<MultiwayActionDescriptor>& action_menu() const noexcept {
        return action_menu_;
    }
    [[nodiscard]] const MultiwaySearchSessionRootMetadata& root_metadata() const noexcept {
        return root_metadata_;
    }

private:
    void initialize_beliefs();
    void validate_dependencies() const;

    MultiwaySolverCoordinator coordinator_;
    MultiwayRangeBeliefs beliefs_;
    const MultiwayBucketRegistry* buckets_ = nullptr;
    std::vector<MultiwayActionDescriptor> action_menu_;
    MultiwaySearchSessionRootMetadata root_metadata_{};
};

}  // namespace core
