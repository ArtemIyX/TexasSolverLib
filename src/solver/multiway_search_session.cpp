#include "solver/multiway_search_session.hpp"

#include <array>
#include <cstddef>
#include <stdexcept>
#include <vector>

namespace core {

MultiwaySearchSession::MultiwaySearchSession(
    const MultiwaySolveRequest& request,
    MultiwaySearchSessionDependencies dependencies,
    std::uint64_t public_root_revision)
    : coordinator_(request),
      buckets_(dependencies.buckets),
      action_menu_(coordinator_.root().public_state.legal_actions),
      root_metadata_({
          coordinator_.root().public_state.id,
          coordinator_.root().action_abstraction_identity(),
          public_root_revision,
      }) {
    validate_dependencies();
    initialize_beliefs();
}

void MultiwaySearchSession::validate_dependencies() const {
    const auto& root = coordinator_.root();
    if (root_metadata_.revision == 0U || root_metadata_.public_state != root.public_state.id || action_menu_.empty() ||
        root_metadata_.action_abstraction.menu_id == 0U ||
        root_metadata_.action_abstraction.version == 0U) {
        throw std::invalid_argument("multiway search session has invalid root or action dependencies");
    }

    if (root.public_state.betting.street == Street::Preflop) return;
    if (buckets_ == nullptr) {
        throw std::invalid_argument("multiway postflop search session requires a bucket registry");
    }
    const auto& table = buckets_->table_hunl(
        root.public_state.betting.street, root.public_state.board);
    if (root.root_bucket >= table.bucket_count()) {
        throw std::invalid_argument("multiway search session root bucket is unavailable");
    }
}

MultiwayRangeBeliefView MultiwaySearchSession::belief(PlayerId seat) const {
    if (seat < 0 || static_cast<std::size_t>(seat) >= beliefs_.seat_count()) {
        throw std::out_of_range("multiway search session belief seat is unavailable");
    }
    return beliefs_.view(static_cast<std::size_t>(seat));
}

MultiwayRangeBeliefUpdateResult MultiwaySearchSession::apply_observation(
    PlayerId seat,
    const MultiwayRangeBeliefObservation& observation) {
    if (seat < 0 || static_cast<std::size_t>(seat) >= beliefs_.seat_count()) {
        throw std::out_of_range("multiway search session belief seat is unavailable");
    }
    return beliefs_.apply_observation(static_cast<std::size_t>(seat), observation);
}

MultiwaySearchSessionRowView MultiwaySearchSession::row_view() const noexcept {
    const auto& storage = coordinator_.storage();
    return {
        storage.row_count(),
        storage.value_count(),
        storage.has_row(coordinator_.root().root_infoset),
    };
}

bool MultiwaySearchSession::capture_clean_snapshot(
    bool clean,
    std::uint64_t batch_index,
    std::uint64_t first_trajectory_id,
    std::uint64_t trajectory_count,
    std::uint64_t accepted_trajectories,
    std::uint64_t merged_delta_entries,
    std::uint32_t worker_count) {
    const auto rows = row_view();
    if (!clean || batch_index == 0U || trajectory_count == 0U || accepted_trajectories == 0U ||
        merged_delta_entries == 0U || worker_count != coordinator_.limits().worker_count || !rows.has_root_row) {
        return false;
    }
    clean_snapshot_ = MultiwaySearchSessionCleanSnapshot{
        coordinator_.export_root_policy(),
        rows,
        root_metadata_.revision,
        batch_index,
        first_trajectory_id,
        trajectory_count,
        accepted_trajectories,
        merged_delta_entries,
        worker_count,
    };
    return true;
}

const MultiwaySearchSessionCleanSnapshot* MultiwaySearchSession::clean_snapshot() const noexcept {
    return clean_snapshot_ ? &*clean_snapshot_ : nullptr;
}

void MultiwaySearchSession::initialize_beliefs() {
    const auto& root = coordinator_.root();
    const auto seat_count = root.seat_order.size();
    std::array<std::vector<MultiwayRangeBeliefSuppliedEntry>, MULTIWAY_RANGE_BELIEF_MAX_SEATS> entries;
    std::array<MultiwayRangeBeliefSeatInput, MULTIWAY_RANGE_BELIEF_MAX_SEATS> inputs = {};
    for (std::size_t seat = 0U; seat < seat_count; ++seat) {
        const auto& source = root.private_ranges.ranges[seat];
        auto& destination = entries[seat];
        destination.reserve(source.size());
        for (const auto& weighted_hole : source) {
            destination.push_back({weighted_hole.hole, weighted_hole.weight});
        }
        inputs[seat] = {
            destination.data(),
            destination.size(),
            root.public_state.board.data(),
            root.public_state.board.size(),
        };
    }
    beliefs_.reset_supplied(seat_count, inputs.data());
}

}  // namespace core
