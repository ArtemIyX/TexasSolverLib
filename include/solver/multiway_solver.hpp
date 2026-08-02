#pragma once

#include "games/multiway_private.hpp"
#include "games/multiway_state.hpp"
#include "solver/multiway_cfr.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace core {

class MultiwayTerminalAdapter;

// Stable numeric identities are assigned by the coordinator. Zero is reserved
// as an invalid identity so an omitted id cannot become a storage key.
struct MultiwayPublicStateId {
    std::uint64_t value = 0;

    constexpr bool operator==(const MultiwayPublicStateId& other) const noexcept {
        return value == other.value;
    }
    constexpr bool operator!=(const MultiwayPublicStateId& other) const noexcept {
        return !(*this == other);
    }
    constexpr bool operator<(const MultiwayPublicStateId& other) const noexcept {
        return value < other.value;
    }
};

struct MultiwayInfosetId {
    MultiwayPublicStateId public_state{};
    PlayerId seat = -1;

    constexpr bool operator==(const MultiwayInfosetId& other) const noexcept {
        return public_state == other.public_state && seat == other.seat;
    }
    constexpr bool operator<(const MultiwayInfosetId& other) const noexcept {
        if (seat != other.seat) return seat < other.seat;
        return public_state < other.public_state;
    }
};

struct MultiwayActionDescriptor {
    MultiwayAction action = MultiwayAction::Fold;
    std::uint32_t action_index = 0;
    // Exact acting-seat street contribution after this action. This is
    // required for every action, including Fold, Check, Call, and AllIn.
    int target_street_contribution = 0;
    std::uint64_t action_menu_id = 0;

    constexpr bool operator==(const MultiwayActionDescriptor& other) const noexcept {
        return action == other.action && action_index == other.action_index &&
               target_street_contribution == other.target_street_contribution &&
               action_menu_id == other.action_menu_id;
    }
    constexpr bool operator!=(const MultiwayActionDescriptor& other) const noexcept {
        return !(*this == other);
    }
};

struct MultiwayPublicHistoryEntry {
    PlayerId actor = -1;
    MultiwayActionDescriptor action{};

    constexpr bool operator==(const MultiwayPublicHistoryEntry& other) const noexcept {
        return actor == other.actor && action == other.action;
    }
};

// The known board and its deterministic chance boundary.  `remaining_board_cards`
// prevents a root from silently relying on a caller-selected runout length.
struct MultiwayBoardRunoutState {
    std::uint8_t remaining_board_cards = 2;
    bool chance_only_runout = false;

    constexpr bool operator==(const MultiwayBoardRunoutState& other) const noexcept {
        return remaining_board_cards == other.remaining_board_cards &&
               chance_only_runout == other.chance_only_runout;
    }
    constexpr bool operator!=(const MultiwayBoardRunoutState& other) const noexcept {
        return !(*this == other);
    }
};

// Side-pot settlement follows the existing terminal layer's cyclic seat-id
// order, beginning at `odd_chip_first_seat`.  Seat order for transitions is
// intentionally separate from this legacy settlement convention.
enum class MultiwayOddChipRule : std::uint8_t {
    AscendingSeatIdFromFirstSeat,
};

// A stable action abstraction is identified by its coordinator-assigned menu
// id together with the version that produced its target sizes.
struct MultiwayActionAbstractionIdentity {
    std::uint64_t menu_id = 0;
    std::uint64_t version = 0;

    constexpr bool operator==(const MultiwayActionAbstractionIdentity& other) const noexcept {
        return menu_id == other.menu_id && version == other.version;
    }
};

enum class MultiwayPublicParentEdgeKind : std::uint8_t {
    None,
    BettingAction,
    BoardChance,
    StreetTransition,
};

// Typed incoming edge for a coordinator-admitted public state. Only the
// payload matching `kind` is consumed: action, dealt_card, or next board.
struct MultiwayPublicParentEdge {
    MultiwayPublicParentEdgeKind kind = MultiwayPublicParentEdgeKind::None;
    MultiwayActionDescriptor action{};
    std::uint8_t dealt_card = 0;
    // A preflop-to-flop chance edge deals a canonical sorted three-card
    // combination. Turn and river edges contain one card.
    std::vector<std::uint8_t> dealt_cards;
    std::vector<std::uint8_t> transition_board;
};

// This descriptor is the canonical public object admitted by the coordinator.
// It intentionally contains no private cards, ranges, or mutable policy data.
struct MultiwayPublicStateDescriptor {
    MultiwayPublicStateId id{};
    MultiwayPublicStateId parent_id{};
    MultiwayPublicParentEdge incoming_edge{};
    std::uint64_t canonical_history_id = 0;
    MultiwayBettingSnapshot betting{};
    std::vector<std::uint8_t> board;
    MultiwayBoardRunoutState board_runout{};
    std::vector<MultiwayPublicHistoryEntry> history;
    std::vector<MultiwayActionDescriptor> legal_actions;
};

// The complete public/private root needed by a future traversal. It is copied
// into MultiwaySolveRequest and then exposed only through const accessors.
struct MultiwayRootSnapshot {
    MultiwayPublicStateDescriptor public_state;
    MultiwayInfosetId root_infoset{};
    std::uint32_t root_bucket = 0;
    std::vector<PlayerId> seat_order;
    // First actionable seat after a non-runout street transition.  This makes
    // future transition ownership deterministic instead of caller-selected.
    PlayerId next_street_first_seat = 0;
    PlayerId odd_chip_first_seat = -1;
    MultiwayOddChipRule odd_chip_rule = MultiwayOddChipRule::AscendingSeatIdFromFirstSeat;
    // Rake is immutable root/model metadata, including explicit zero rake.
    MultiwayRakePolicy rake_policy = MultiwayRakePolicy::explicit_zero();
    MultiwayPrivateConfig private_ranges{};
    std::uint64_t action_abstraction_version = 0;
    std::uint64_t leaf_model_version = 0;
    // Terminal settlement is chips. The adapter converts utility values once
    // at this root boundary; unsupported normalizations are rejected.
    MultiwayValueUnits value_units = MultiwayValueUnits::Chips;

    void validate() const;

    [[nodiscard]] std::uint64_t action_menu_id() const noexcept {
        return public_state.legal_actions.empty() ? 0 : public_state.legal_actions.front().action_menu_id;
    }
    [[nodiscard]] MultiwayActionAbstractionIdentity action_abstraction_identity() const noexcept {
        return {action_menu_id(), action_abstraction_version};
    }
    [[nodiscard]] std::uint64_t terminal_model_identity() const noexcept {
        return leaf_model_version ^ rake_policy.identity();
    }
};

struct MultiwaySolverLimits {
    std::uint64_t seed = 1;
    std::uint32_t worker_count = 1;
    std::uint32_t trajectories_per_batch = 1;
    std::size_t max_public_states = 0;
    std::size_t max_sparse_rows = 0;
    std::size_t max_sparse_values = 0;
    std::size_t max_worker_delta_entries = 0;

    void validate() const;
};

// Immutable boundary input. Traversal must treat this request as a batch
// snapshot and cannot mutate the caller's root, ranges, or policy metadata.
class MultiwaySolveRequest {
public:
    MultiwaySolveRequest(
        MultiwayRootSnapshot root,
        MultiwayCFRConfig cfr_config,
        MultiwaySolverLimits limits = {});

    [[nodiscard]] const MultiwayRootSnapshot& root() const noexcept { return root_; }
    [[nodiscard]] const MultiwayCFRConfig& cfr_config() const noexcept { return cfr_config_; }
    [[nodiscard]] const MultiwaySolverLimits& limits() const noexcept { return limits_; }
    [[nodiscard]] const MultiwayPrivateRangeFeasibilityResult& private_range_feasibility() const noexcept {
        return private_range_feasibility_;
    }
    [[nodiscard]] const MultiwayCompiledPrivateRanges& compiled_private_ranges() const noexcept {
        return *compiled_private_ranges_;
    }

private:
    MultiwayRootSnapshot root_;
    MultiwayCFRConfig cfr_config_;
    MultiwaySolverLimits limits_;
    MultiwayPrivateRangeFeasibilityResult private_range_feasibility_{};
    std::optional<MultiwayCompiledPrivateRanges> compiled_private_ranges_;
};

struct MultiwaySparseRowShape {
    MultiwayInfosetId infoset{};
    std::uint32_t bucket_count = 0;
    std::uint8_t action_count = 0;
};

struct MultiwaySparseRowMetadata {
    MultiwaySparseRowShape shape{};
    std::size_t regret_offset = 0;
    std::size_t strategy_sum_offset = 0;

    [[nodiscard]] std::size_t value_count() const noexcept;
};

// Coordinator-owned sparse rows. Values are action-major: [action][bucket].
// Persistent accumulators are Float64. Merges use fixed global delta order and
// reject a nonzero delta that a Float64 accumulator cannot represent, rather
// than silently losing long-run regret or average-strategy mass.
// Only the coordinator receives a mutable instance; workers emit deltas.
class MultiwaySparseRowStorage {
public:
    MultiwaySparseRowStorage(std::size_t max_rows, std::size_t max_values);

    [[nodiscard]] bool has_row(MultiwayInfosetId infoset) const noexcept;
    [[nodiscard]] const MultiwaySparseRowMetadata* metadata(MultiwayInfosetId infoset) const noexcept;
    [[nodiscard]] std::vector<Probability> average_strategy(
        MultiwayInfosetId infoset,
        std::uint32_t bucket) const;
    [[nodiscard]] std::vector<Probability> regret_matched_strategy(
        MultiwayInfosetId infoset,
        std::uint32_t bucket) const;
    void regret_matched_strategy_into(
        MultiwayInfosetId infoset,
        std::uint32_t bucket,
        Probability* output,
        std::size_t output_size) const;
    [[nodiscard]] std::size_t row_count() const noexcept { return metadata_.size(); }
    [[nodiscard]] std::size_t value_count() const noexcept { return regret_.size(); }

private:
    friend class MultiwaySolverCoordinator;

    void admit_row(const MultiwaySparseRowShape& shape);
    void apply_delta(
        MultiwayInfosetId infoset,
        std::uint32_t bucket,
        std::uint8_t action,
        double regret,
        double strategy_sum);

    std::size_t max_rows_ = 0;
    std::size_t max_values_ = 0;
    std::vector<MultiwaySparseRowMetadata> metadata_;
    std::vector<double> regret_;
    std::vector<double> strategy_sum_;
};

struct MultiwayWorkerDelta {
    MultiwayInfosetId infoset{};
    std::uint32_t bucket = 0;
    std::uint8_t action = 0;
    double regret = 0.0;
    double strategy_sum = 0.0;
    std::uint64_t trajectory_id = 0;
};

// Bounded worker-local stream. Traversal appends allocation-free after the
// constructor reserves capacity, sorts once at the worker boundary, and never
// accesses coordinator storage directly.
class MultiwayWorkerDeltaStream {
public:
    MultiwayWorkerDeltaStream(std::size_t worker_index, std::size_t capacity);

    [[nodiscard]] std::size_t worker_index() const noexcept { return worker_index_; }
    [[nodiscard]] std::size_t capacity() const noexcept { return capacity_; }
    [[nodiscard]] std::size_t size() const noexcept { return deltas_.size(); }
    [[nodiscard]] bool try_append(const MultiwayWorkerDelta& delta) noexcept;
    void rewind(std::size_t size) noexcept;
    void sort_fixed_order() noexcept;
    [[nodiscard]] bool is_fixed_order() const noexcept;
    [[nodiscard]] const std::vector<MultiwayWorkerDelta>& deltas() const noexcept { return deltas_; }

private:
    std::size_t worker_index_ = 0;
    std::size_t capacity_ = 0;
    std::vector<MultiwayWorkerDelta> deltas_;
};

struct MultiwayRootActionProbability {
    MultiwayActionDescriptor action{};
    Probability probability = 0.0;
};

struct MultiwayRootPolicy {
    MultiwayPublicStateId public_state{};
    MultiwayInfosetId infoset{};
    std::uint32_t bucket = 0;
    std::vector<MultiwayRootActionProbability> actions;
};

struct MultiwaySolveDiagnostics {
    std::uint64_t batches_completed = 0;
    std::uint64_t trajectories_attempted = 0;
    std::uint64_t trajectories_accepted = 0;
    std::uint64_t trajectories_discarded = 0;
    std::uint64_t public_states_admitted = 0;
    std::uint64_t sparse_rows_admitted = 0;
    std::uint64_t worker_delta_entries_merged = 0;
    double traversal_seconds = 0.0;
    double merge_seconds = 0.0;
    double export_seconds = 0.0;
    std::optional<MultiwayNashConv> quality;
};

// Immutable boundary output. It deliberately retains only a root policy,
// per-seat root values, and diagnostics; it never exports dense global policy.
class MultiwaySolveResult {
public:
    MultiwaySolveResult(
        MultiwayRootPolicy root_policy,
        std::vector<Value> root_values,
        MultiwaySolveDiagnostics diagnostics);

    [[nodiscard]] const MultiwayRootPolicy& root_policy() const noexcept { return root_policy_; }
    [[nodiscard]] const std::vector<Value>& root_values() const noexcept { return root_values_; }
    [[nodiscard]] const MultiwaySolveDiagnostics& diagnostics() const noexcept { return diagnostics_; }

private:
    MultiwayRootPolicy root_policy_;
    std::vector<Value> root_values_;
    MultiwaySolveDiagnostics diagnostics_;
};

// Coordinator boundary for multiway batches. It is the sole owner of public
// state admission and row mutation. This is intentionally not a traversal API.
class MultiwaySolverCoordinator {
public:
    explicit MultiwaySolverCoordinator(const MultiwaySolveRequest& request);

    void admit_public_state(const MultiwayPublicStateDescriptor& state);
    void admit_infoset_row(const MultiwaySparseRowShape& shape);
    void merge_worker_streams(const std::vector<MultiwayWorkerDeltaStream>& streams);

    [[nodiscard]] MultiwayRootPolicy export_root_policy() const;
    [[nodiscard]] const MultiwaySparseRowStorage& storage() const noexcept { return storage_; }
    [[nodiscard]] const MultiwaySolveDiagnostics& diagnostics() const noexcept { return diagnostics_; }
    [[nodiscard]] const MultiwaySolverLimits& limits() const noexcept { return request_.limits(); }

private:
    friend class MultiwayTerminalAdapter;
    [[nodiscard]] const MultiwayPublicStateDescriptor* public_state(
        MultiwayPublicStateId id) const noexcept;

    MultiwaySolveRequest request_;
    MultiwaySparseRowStorage storage_;
    std::vector<MultiwayPublicStateDescriptor> public_states_;
    MultiwaySolveDiagnostics diagnostics_;
};

}  // namespace core
