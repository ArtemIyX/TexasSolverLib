#pragma once

#include "core/legacy_namespace_compat.hpp"

#include "solver/multiway_bucket_model.hpp"
#include "solver/multiway_action_abstraction.hpp"
#include "solver/multiway_blueprint_policy_provider.hpp"
#include "solver/multiway_continuation_selector.hpp"
#include "solver/multiway_range_belief.hpp"
#include "solver/multiway_solver.hpp"

#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

namespace texas::solver::multiway {

// Borrowed immutable, caller-owned artifact input. A bucket registry is
// required for postflop roots and optional for preflop roots. This P1.4
// session does not traverse postflop states.
struct MultiwaySearchSessionDependencies {
    const MultiwayBucketRegistry* buckets = nullptr;
    std::shared_ptr<const MultiwayFixedContinuationSelector> continuation_selector;
    bool lossless_current_round_keys = true;
};

struct MultiwaySearchSessionRootMetadata {
    MultiwayPublicStateId public_state{};
    MultiwayActionAbstractionIdentity action_abstraction{};
    std::uint64_t revision = 0U;
};

struct MultiwaySearchSessionRowView {
    std::size_t row_count = 0U;
    std::size_t value_count = 0U;
    bool has_root_row = false;
};

// A prepared replacement root. Planning it is transactional: the current
// session remains unchanged until the runtime owner elects to reroot.
struct MultiwayLocalExpansion {
    MultiwayRootSnapshot root{};
    MultiwayActionDescriptor observed_action{};
};

// Range-wide policy export remains request-local. Only the selected actual
// hand may be frozen; hypothetical rows remain available to range updates.
struct MultiwaySearchSessionHeroRow {
    CanonicalComboId combo{};
    std::vector<MultiwayRootActionProbability> actions;
};

struct MultiwaySearchSessionHeroPolicy {
    PlayerId hero_seat = -1;
    MultiwayInfosetId infoset{};
    std::uint64_t root_revision = 0U;
    std::vector<MultiwaySearchSessionHeroRow> rows;
    std::vector<MultiwayRootActionProbability> actual_hand_actions;
    bool actual_hand_frozen = false;
};

// Immutable export captured only after a fully merged batch. It contains no
// private cards, ranges, worker streams, or sampling seed.
struct MultiwaySearchSessionCleanSnapshot {
    MultiwayRootPolicy root_policy{};
    MultiwaySearchSessionRowView rows{};
    std::uint64_t root_revision = 0U;
    std::uint64_t batch_index = 0U;
    std::uint64_t first_trajectory_id = 0U;
    std::uint64_t trajectory_count = 0U;
    std::uint64_t accepted_trajectories = 0U;
    std::uint64_t merged_delta_entries = 0U;
    std::uint32_t worker_count = 0U;
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
    [[nodiscard]] const MultiwayFixedContinuationSelector* continuation_selector() const noexcept {
        return dependencies_.continuation_selector.get();
    }
    [[nodiscard]] const std::vector<MultiwayActionDescriptor>& action_menu() const noexcept {
        return action_menu_;
    }
    void record_action_translation(MultiwayActionTranslation translation);
    [[nodiscard]] const MultiwayActionTranslation* action_translation() const noexcept;
    [[nodiscard]] MultiwayLocalExpansion plan_local_expansion(
        const MultiwayActionAbstraction& abstraction,
        MultiwayAction observed_action,
        int target_street_contribution,
        const MultiwayDeviationExpansionConfig& expansion,
        MultiwayActionAbstractionContext context = {}) const;
    [[nodiscard]] const MultiwaySearchSessionRootMetadata& root_metadata() const noexcept {
        return root_metadata_;
    }
    [[nodiscard]] MultiwaySearchSessionRowView row_view() const noexcept;
    [[nodiscard]] bool capture_clean_snapshot(
        bool clean,
        std::uint64_t batch_index,
        std::uint64_t first_trajectory_id,
        std::uint64_t trajectory_count,
        std::uint64_t accepted_trajectories,
        std::uint64_t merged_delta_entries,
        std::uint32_t worker_count);
    [[nodiscard]] const MultiwaySearchSessionCleanSnapshot* clean_snapshot() const noexcept;
    [[nodiscard]] MultiwaySearchSessionHeroPolicy export_hero_policy(
        PlayerId hero_seat,
        CanonicalComboId actual_hand,
        const MultiwayBlueprintPolicyProvider* blueprint_policy = nullptr) const;
    void freeze_actual_hand_policy(
        PlayerId hero_seat,
        CanonicalComboId actual_hand,
        const std::vector<MultiwayRootActionProbability>& actions);
    void clear_actual_hand_freeze() noexcept;
    [[nodiscard]] MultiwayRootSnapshot make_next_round_root(
        MultiwayRootSnapshot next_root) const;
    [[nodiscard]] MultiwayRootSnapshot make_reroot_root(
        MultiwayRootSnapshot next_root) const;

private:
    void initialize_beliefs();
    void validate_dependencies() const;

    MultiwaySolverCoordinator coordinator_;
    MultiwaySearchSessionDependencies dependencies_{};
    MultiwayRangeBeliefs beliefs_;
    const MultiwayBucketRegistry* buckets_ = nullptr;
    std::vector<MultiwayActionDescriptor> action_menu_;
    std::optional<MultiwayActionTranslation> action_translation_;
    MultiwaySearchSessionRootMetadata root_metadata_{};
    std::optional<MultiwaySearchSessionCleanSnapshot> clean_snapshot_;
    struct ActualHandFreeze {
        PlayerId hero_seat = -1;
        CanonicalComboId combo{};
        std::uint64_t root_revision = 0U;
        std::vector<MultiwayRootActionProbability> actions;
    };
    std::optional<ActualHandFreeze> actual_hand_freeze_;
};

}  // namespace texas::solver::multiway
