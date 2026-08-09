#include "solver/multiway_range_belief.hpp"

#include <cmath>
#include <stdexcept>
#include <utility>

namespace core {
namespace {

void validate_dead_cards(const MultiwayRangeBeliefSeatInput& input) {
    if (input.dead_cards == nullptr && input.dead_card_count != 0U) {
        throw std::invalid_argument("multiway range belief dead-card input is missing");
    }
}

}  // namespace

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

MultiwayRangeBeliefs::Row MultiwayRangeBeliefs::make_uniform_row(
    const MultiwayRangeBeliefSeatInput& input,
    std::uint64_t revision) {
    validate_dead_cards(input);
    Row row;
    row.legal_mask = canonical_combos().legal_mask(input.dead_cards, input.dead_card_count);
    row.metadata.source = MultiwayRangeBeliefSource::Uniform;
    row.metadata.last_update_revision = revision;
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
    validate_seat_reset(seat_count, seats);
    const auto next_revision = revision_ + 1U;
    std::array<Row, MULTIWAY_RANGE_BELIEF_MAX_SEATS> next = {};
    for (std::size_t seat = 0U; seat < seat_count; ++seat) {
        next[seat] = make_uniform_row(seats[seat], next_revision);
    }
    rows_ = std::move(next);
    seat_count_ = seat_count;
    revision_ = next_revision;
}

void MultiwayRangeBeliefs::reset_supplied(
    std::size_t seat_count,
    const MultiwayRangeBeliefSeatInput* seats) {
    validate_seat_reset(seat_count, seats);
    const auto next_revision = revision_ + 1U;
    std::array<Row, MULTIWAY_RANGE_BELIEF_MAX_SEATS> next = {};
    for (std::size_t seat = 0U; seat < seat_count; ++seat) {
        next[seat] = make_supplied_row(seats[seat], next_revision);
    }
    rows_ = std::move(next);
    seat_count_ = seat_count;
    revision_ = next_revision;
}

void MultiwayRangeBeliefs::copy_from(const MultiwayRangeBeliefs& other) noexcept {
    if (this != &other) *this = other;
}

MultiwayRangeBeliefView MultiwayRangeBeliefs::view(std::size_t seat) const {
    if (seat >= seat_count_) throw std::out_of_range("multiway range belief seat is unavailable");
    const auto& row = rows_[seat];
    return MultiwayRangeBeliefView(row.weights.data(), &row.legal_mask, &row.metadata);
}

}  // namespace core
