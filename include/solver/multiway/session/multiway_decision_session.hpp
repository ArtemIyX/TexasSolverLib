#pragma once

#include "solver/multiway/session/multiway_search_session.hpp"

#include <memory>

namespace texas::solver::multiway {

// User-owned live-hand lifecycle. It owns each round's mutable search state
// and replaces it on a street transition or qualifying same-street reroot.
class MultiwayDecisionSession {
public:
    MultiwayDecisionSession(
        MultiwaySolveRequest initial_request,
        MultiwaySearchSessionDependencies dependencies);

    [[nodiscard]] const MultiwaySearchSession& round() const noexcept { return *round_; }
    [[nodiscard]] MultiwaySearchSession& round() noexcept { return *round_; }
    [[nodiscard]] std::uint64_t root_revision() const noexcept { return root_revision_; }

    [[nodiscard]] MultiwayRangeBeliefUpdateResult observe_action(
        core::PlayerId seat,
        const MultiwayRangeBeliefObservation& observation);
    void reroot(
        MultiwayRootSnapshot next_root,
        MultiwayCFRConfig cfr,
        MultiwaySolverLimits limits,
        bool street_transition);

private:
    MultiwaySearchSessionDependencies dependencies_{};
    std::uint64_t root_revision_ = 1U;
    std::unique_ptr<MultiwaySearchSession> round_;
};

}  // namespace texas::solver::multiway
