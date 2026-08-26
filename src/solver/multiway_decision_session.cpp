#include "solver/multiway_decision_session.hpp"

#include <limits>
#include <stdexcept>
#include <utility>

namespace texas::solver::multiway {

MultiwayDecisionSession::MultiwayDecisionSession(
    MultiwaySolveRequest initial_request,
    MultiwaySearchSessionDependencies dependencies)
    : dependencies_(dependencies),
      round_(std::make_unique<MultiwaySearchSession>(initial_request, dependencies_, root_revision_)) {}

MultiwayRangeBeliefUpdateResult MultiwayDecisionSession::observe_action(
    core::PlayerId seat,
    const MultiwayRangeBeliefObservation& observation) {
    return round_->apply_observation(seat, observation);
}

void MultiwayDecisionSession::reroot(
    MultiwayRootSnapshot next_root,
    MultiwayCFRConfig cfr,
    MultiwaySolverLimits limits,
    bool street_transition) {
    if (root_revision_ == std::numeric_limits<std::uint64_t>::max()) {
        throw std::overflow_error("multiway decision root revision overflow");
    }
    const auto root = street_transition
        ? round_->make_next_round_root(std::move(next_root))
        : round_->make_reroot_root(std::move(next_root));
    MultiwaySolveRequest request(root, cfr, limits);
    round_ = std::make_unique<MultiwaySearchSession>(request, dependencies_, ++root_revision_);
}

}  // namespace texas::solver::multiway
