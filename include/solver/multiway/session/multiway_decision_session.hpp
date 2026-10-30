#pragma once

/** @file
 * User-owned lifecycle for multi-round multiway search sessions.
 */

#include "solver/multiway/session/multiway_search_session.hpp"

#include <memory>

namespace texas::solver::multiway
{

	// User-owned live-hand lifecycle. It owns each round's mutable search state
	// and replaces it on a street transition or qualifying same-street reroot.
	/** Coordinates round search state, range observations, and root changes. */
	class MultiwayDecisionSession
	{
	public:
		/** Creates the initial round from a request and borrowed dependencies. */
		MultiwayDecisionSession(
			MultiwaySolveRequest initial_request,
			MultiwaySearchSessionDependencies dependencies);

		/** Returns the active round as a read-only search session. */
		[[nodiscard]] const MultiwaySearchSession& round() const noexcept { return *round_; }
		/** Returns the active round for continued runtime search. */
		[[nodiscard]] MultiwaySearchSession& round() noexcept { return *round_; }
		/** Returns the revision used to invalidate stale root policies. */
		[[nodiscard]] std::uint64_t root_revision() const noexcept { return root_revision_; }

		/** Applies one observed action to the selected seat's range belief. */
		[[nodiscard]] MultiwayRangeBeliefUpdateResult observe_action(
			core::PlayerId seat,
			const MultiwayRangeBeliefObservation& observation);
		/** Replaces the current round after a street transition or root change. */
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

} // namespace texas::solver::multiway
