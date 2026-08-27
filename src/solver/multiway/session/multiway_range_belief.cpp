#include "solver/multiway/session/multiway_range_belief.hpp"

#include <cmath>
#include <limits>
#include <new>
#include <stdexcept>
#include <type_traits>
#include <utility>

namespace texas::solver::multiway {
namespace {

void validate_dead_cards(const MultiwayRangeBeliefSeatInput& input) {
    if (input.dead_cards == nullptr && input.dead_card_count != 0U) {
        throw std::invalid_argument("multiway range belief dead-card input is missing");
    }
}

bool is_observation_source(MultiwayRangeBeliefSource source) noexcept {
    return source == MultiwayRangeBeliefSource::Blueprint ||
           source == MultiwayRangeBeliefSource::Search ||
           source == MultiwayRangeBeliefSource::Translated ||
           source == MultiwayRangeBeliefSource::Fallback;
}

}  // namespace

MultiwayRangeBeliefObservation::MultiwayRangeBeliefObservation(
    const MultiwayBucketTable& table,
    const MultiwayBucketActionPolicy& policy,
    MultiwayRangeBeliefSource source,
    std::uint8_t observed_action,
    std::uint64_t source_revision) noexcept
    : table_(&table),
      policy_(&policy),
      source_(source),
      observed_action_(observed_action),
      source_revision_(source_revision) {}

void MultiwayRangeBeliefObservation::validate() const {
    if (table_ == nullptr || policy_ == nullptr || !is_observation_source(source_)) {
        throw std::invalid_argument("multiway range belief observation is invalid");
    }
    policy_->validate();
    if (table_->identity() != policy_->identity || table_->bucket_count() != policy_->bucket_count ||
        policy_->bucket_table_identity == 0U ||
        policy_->bucket_table_identity != table_->table_identity() ||
        observed_action_ >= policy_->action_count) {
        throw std::invalid_argument("multiway range belief observation has incompatible policy data");
    }
}

double MultiwayRangeBeliefView::weight(CanonicalComboId id) const {
    if (!valid() || id >= CANONICAL_HOLE_COMBINATION_COUNT) {
        throw std::out_of_range("multiway range belief weight is unavailable");
    }
    return weights_[id];
}

bool MultiwayRangeBeliefView::legal(CanonicalComboId id) const {
    if (!valid() || id >= CANONICAL_HOLE_COMBINATION_COUNT) {
        throw std::out_of_range("multiway range belief legal mask is unavailable");
    }
    return legal_mask_->test(id);
}

const CanonicalComboLegalMask& MultiwayRangeBeliefView::legal_mask() const {
    if (!valid()) throw std::logic_error("multiway range belief view is empty");
    return *legal_mask_;
}

const MultiwayRangeBeliefMetadata& MultiwayRangeBeliefView::metadata() const {
    if (!valid()) throw std::logic_error("multiway range belief view is empty");
    return *metadata_;
}

void MultiwayRangeBeliefs::validate_seat_reset(
    std::size_t seat_count,
    const MultiwayRangeBeliefSeatInput* seats) {
    if (seat_count < 2U || seat_count > MULTIWAY_RANGE_BELIEF_MAX_SEATS || seats == nullptr) {
        throw std::invalid_argument("multiway range belief requires two through six seat inputs");
    }
    for (std::size_t seat = 0U; seat < seat_count; ++seat) validate_dead_cards(seats[seat]);
}

void MultiwayRangeBeliefs::normalize_row(Row& row) {
    double total = 0.0;
    for (std::size_t id = 0U; id < CANONICAL_HOLE_COMBINATION_COUNT; ++id) {
        if (!row.legal_mask.test(id)) {
            row.weights[id] = 0.0;
            continue;
        }
        total += row.weights[id];
    }
    if (!std::isfinite(total) || total <= 0.0) {
        throw std::invalid_argument("multiway range belief has no legal positive mass");
    }
    row.metadata.input_mass = total;
    for (auto& weight : row.weights) weight /= total;
    row.metadata.normalized_mass = 1.0;
}

void MultiwayRangeBeliefs::apply_observation_metadata(
    Row& row,
    const MultiwayRangeBeliefObservation& observation,
    std::uint64_t revision,
    double input_mass) noexcept {
    row.metadata.source = observation.source();
    row.metadata.last_update_revision = revision;
    row.metadata.observation.source = observation.source();
    row.metadata.observation.public_state_id = observation.policy().public_state.value;
    row.metadata.observation.action_menu_id = observation.policy().action_menu_id;
    row.metadata.observation.bucket_table_identity = observation.table().table_identity();
    row.metadata.observation.source_revision = observation.source_revision();
    row.metadata.observation.observed_action = observation.observed_action();
    row.metadata.observation.applied = true;
    row.metadata.input_mass = input_mass;
    row.metadata.normalized_mass = 1.0;
}

MultiwayRangeBeliefs::Row MultiwayRangeBeliefs::make_uniform_row(
    const MultiwayRangeBeliefSeatInput& input,
    std::uint64_t revision) {
    validate_dead_cards(input);
    Row row;
    row.legal_mask = canonical_combos().legal_mask(input.dead_cards, input.dead_card_count);
    row.metadata.source = MultiwayRangeBeliefSource::Uniform;
    row.metadata.last_update_revision = revision;
    row.metadata.observation = {};
    for (std::size_t id = 0U; id < CANONICAL_HOLE_COMBINATION_COUNT; ++id) {
        row.weights[id] = row.legal_mask.test(id) ? 1.0 : 0.0;
    }
    normalize_row(row);
    return row;
}

MultiwayRangeBeliefs::Row MultiwayRangeBeliefs::make_supplied_row(
    const MultiwayRangeBeliefSeatInput& input,
    std::uint64_t revision) {
    validate_dead_cards(input);
    if (input.entries == nullptr || input.entry_count == 0U) {
        throw std::invalid_argument("multiway range belief supplied input is empty");
    }

    Row row;
    row.legal_mask = canonical_combos().legal_mask(input.dead_cards, input.dead_card_count);
    row.metadata.source = MultiwayRangeBeliefSource::Supplied;
    row.metadata.last_update_revision = revision;
    row.metadata.observation = {};
    for (std::size_t index = 0U; index < input.entry_count; ++index) {
        const auto& entry = input.entries[index];
        if (!std::isfinite(entry.weight) || entry.weight < 0.0) {
            throw std::invalid_argument("multiway range belief supplied weight is invalid");
        }
        const auto id = canonical_combos().id(entry.hole);
        if (row.legal_mask.test(id)) row.weights[id] += entry.weight;
    }
    normalize_row(row);
    return row;
}

void MultiwayRangeBeliefs::reset_uniform(
    std::size_t seat_count,
    const MultiwayRangeBeliefSeatInput* seats) {
    reset_rows(seat_count, seats, make_uniform_row);
}

void MultiwayRangeBeliefs::reset_supplied(
    std::size_t seat_count,
    const MultiwayRangeBeliefSeatInput* seats) {
    reset_rows(seat_count, seats, make_supplied_row);
}

MultiwayRangeBeliefUpdateResult MultiwayRangeBeliefs::apply_observation(
    std::size_t seat,
    const MultiwayRangeBeliefObservation& observation) {
    if (seat >= seat_count_) throw std::out_of_range("multiway range belief seat is unavailable");
    observation.validate();

    const auto& table = observation.table();
    const auto& policy = observation.policy();
    const auto& row = rows_[seat];
    const auto& assignments = table.assignments();
    const auto action_offset = static_cast<std::size_t>(observation.observed_action()) * policy.bucket_count;
    const auto* action_probabilities = policy.probabilities.data() + action_offset;
    const auto probability_scale = 1.0 / static_cast<double>(std::numeric_limits<std::uint16_t>::max());
    double posterior_mass = 0.0;
    for (std::size_t id = 0U; id < CANONICAL_HOLE_COMBINATION_COUNT; ++id) {
        const auto bucket = assignments[id];
        if (!row.legal_mask.test(id) || bucket == MULTIWAY_INVALID_BUCKET) continue;
        const auto likelihood = static_cast<double>(action_probabilities[bucket]) * probability_scale;
        posterior_mass += row.weights[id] * likelihood;
    }
    if (!std::isfinite(posterior_mass) || posterior_mass < 0.0) {
        throw std::invalid_argument("multiway range belief posterior mass is invalid");
    }
    if (posterior_mass == 0.0) return MultiwayRangeBeliefUpdateResult::NoPosteriorMass;

    const auto next_revision = revision_ + 1U;
    auto& updated = rows_[seat];
    for (std::size_t id = 0U; id < CANONICAL_HOLE_COMBINATION_COUNT; ++id) {
        const auto bucket = assignments[id];
        if (!updated.legal_mask.test(id) || bucket == MULTIWAY_INVALID_BUCKET) {
            updated.legal_mask.reset(id);
            updated.weights[id] = 0.0;
            continue;
        }
        const auto likelihood = static_cast<double>(action_probabilities[bucket]) * probability_scale;
        updated.weights[id] = updated.weights[id] * likelihood / posterior_mass;
    }
    apply_observation_metadata(updated, observation, next_revision, posterior_mass);
    revision_ = next_revision;
    return MultiwayRangeBeliefUpdateResult::Applied;
}

void MultiwayRangeBeliefs::reset_rows(
    std::size_t seat_count,
    const MultiwayRangeBeliefSeatInput* seats,
    RowBuilder builder) {
    validate_seat_reset(seat_count, seats);
    const auto next_revision = revision_ + 1U;
    static_assert(std::is_nothrow_move_assignable<Row>::value,
                  "range-belief row replacement must preserve transactional reset");
    using RowStorage = std::aligned_storage_t<sizeof(Row), alignof(Row)>;
    std::array<RowStorage, MULTIWAY_RANGE_BELIEF_MAX_SEATS> staged;
    std::size_t staged_count = 0U;
    const auto staged_row = [&staged](std::size_t seat) noexcept -> Row* {
        return std::launder(reinterpret_cast<Row*>(&staged[seat]));
    };
    try {
        for (; staged_count < seat_count; ++staged_count) {
            ::new (static_cast<void*>(&staged[staged_count])) Row(builder(seats[staged_count], next_revision));
        }
    } catch (...) {
        for (std::size_t seat = 0U; seat < staged_count; ++seat) staged_row(seat)->~Row();
        throw;
    }
    for (std::size_t seat = 0U; seat < seat_count; ++seat) {
        rows_[seat] = std::move(*staged_row(seat));
        staged_row(seat)->~Row();
    }
    seat_count_ = seat_count;
    revision_ = next_revision;
}

void MultiwayRangeBeliefs::copy_from(const MultiwayRangeBeliefs& other) noexcept {
    if (this == &other) return;
    for (std::size_t seat = 0U; seat < other.seat_count_; ++seat) rows_[seat] = other.rows_[seat];
    seat_count_ = other.seat_count_;
    revision_ = other.revision_;
}

MultiwayRangeBeliefView MultiwayRangeBeliefs::view(std::size_t seat) const {
    if (seat >= seat_count_) throw std::out_of_range("multiway range belief seat is unavailable");
    const auto& row = rows_[seat];
    return MultiwayRangeBeliefView(row.weights.data(), &row.legal_mask, &row.metadata);
}

}  // namespace texas::solver::multiway
