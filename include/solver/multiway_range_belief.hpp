#pragma once

#include "core/canonical_combo.hpp"
#include "solver/multiway_range_update.hpp"

#include <array>
#include <cstddef>
#include <cstdint>

namespace core {

inline constexpr std::size_t MULTIWAY_RANGE_BELIEF_MAX_SEATS = 6U;

enum class MultiwayRangeBeliefSource : std::uint8_t {
    None,
    Uniform,
    Supplied,
    Blueprint,
    Search,
    Translated,
    Fallback,
};

struct MultiwayRangeBeliefMetadata {
    MultiwayRangeBeliefSource source = MultiwayRangeBeliefSource::None;
    std::uint64_t last_update_revision = 0U;
    struct Observation {
        MultiwayRangeBeliefSource source = MultiwayRangeBeliefSource::None;
        std::uint64_t public_state_id = 0U;
        std::uint64_t action_menu_id = 0U;
        std::uint64_t source_revision = 0U;
        std::uint8_t observed_action = 0U;
        bool applied = false;
    } observation{};
    // Legal mass before this row is normalized.
    double input_mass = 0.0;
    double normalized_mass = 0.0;
};

struct MultiwayRangeBeliefSuppliedEntry {
    CanonicalComboCards hole = {0U, 0U};
    double weight = 0.0;
};

// Each seat supplies its own public/dead-card mask. A null dead-card pointer
// is valid only when dead_card_count is zero.
struct MultiwayRangeBeliefSeatInput {
    const MultiwayRangeBeliefSuppliedEntry* entries = nullptr;
    std::size_t entry_count = 0U;
    const std::uint8_t* dead_cards = nullptr;
    std::size_t dead_card_count = 0U;
};

enum class MultiwayRangeBeliefUpdateResult : std::uint8_t {
    Applied,
    NoPosteriorMass,
};

// Immutable, borrowed binding to the exact policy row used to observe an
// action. The referenced table and policy must remain alive for apply_observation.
class MultiwayRangeBeliefObservation {
public:
    MultiwayRangeBeliefObservation(
        const MultiwayBucketTable& table,
        const MultiwayBucketActionPolicy& policy,
        MultiwayRangeBeliefSource source,
        std::uint8_t observed_action,
        std::uint64_t source_revision) noexcept;

    [[nodiscard]] const MultiwayBucketTable& table() const noexcept { return *table_; }
    [[nodiscard]] const MultiwayBucketActionPolicy& policy() const noexcept { return *policy_; }
    [[nodiscard]] MultiwayRangeBeliefSource source() const noexcept { return source_; }
    [[nodiscard]] std::uint8_t observed_action() const noexcept { return observed_action_; }
    [[nodiscard]] std::uint64_t source_revision() const noexcept { return source_revision_; }
    void validate() const;

private:
    const MultiwayBucketTable* table_ = nullptr;
    const MultiwayBucketActionPolicy* policy_ = nullptr;
    MultiwayRangeBeliefSource source_ = MultiwayRangeBeliefSource::None;
    std::uint8_t observed_action_ = 0U;
    std::uint64_t source_revision_ = 0U;
};

class MultiwayRangeBeliefView {
public:
    [[nodiscard]] bool valid() const noexcept { return weights_ != nullptr; }
    [[nodiscard]] const double* data() const noexcept { return weights_; }
    [[nodiscard]] std::size_t size() const noexcept { return CANONICAL_HOLE_COMBINATION_COUNT; }
    [[nodiscard]] double weight(CanonicalComboId id) const;
    [[nodiscard]] bool legal(CanonicalComboId id) const;
    [[nodiscard]] const CanonicalComboLegalMask& legal_mask() const;
    [[nodiscard]] const MultiwayRangeBeliefMetadata& metadata() const;

private:
    friend class MultiwayRangeBeliefs;

    MultiwayRangeBeliefView(
        const double* weights,
        const CanonicalComboLegalMask* legal_mask,
        const MultiwayRangeBeliefMetadata* metadata) noexcept
        : weights_(weights), legal_mask_(legal_mask), metadata_(metadata) {}

    const double* weights_ = nullptr;
    const CanonicalComboLegalMask* legal_mask_ = nullptr;
    const MultiwayRangeBeliefMetadata* metadata_ = nullptr;
};

// Request-local fixed storage for up to six independent private-card beliefs.
// Reset methods either replace every requested seat or leave this object intact.
// Returned views are invalidated by a subsequent reset or copy_from call.
class MultiwayRangeBeliefs {
public:
    MultiwayRangeBeliefs() = default;
    MultiwayRangeBeliefs(const MultiwayRangeBeliefs&) = delete;
    MultiwayRangeBeliefs& operator=(const MultiwayRangeBeliefs&) = delete;
    MultiwayRangeBeliefs(MultiwayRangeBeliefs&&) = delete;
    MultiwayRangeBeliefs& operator=(MultiwayRangeBeliefs&&) = delete;

    void reset_uniform(std::size_t seat_count, const MultiwayRangeBeliefSeatInput* seats);
    void reset_supplied(std::size_t seat_count, const MultiwayRangeBeliefSeatInput* seats);
    [[nodiscard]] MultiwayRangeBeliefUpdateResult apply_observation(
        std::size_t seat,
        const MultiwayRangeBeliefObservation& observation);

    void copy_from(const MultiwayRangeBeliefs& other) noexcept;

    [[nodiscard]] std::size_t seat_count() const noexcept { return seat_count_; }
    [[nodiscard]] std::uint64_t revision() const noexcept { return revision_; }
    [[nodiscard]] MultiwayRangeBeliefView view(std::size_t seat) const;

private:
    struct Row {
        std::array<double, CANONICAL_HOLE_COMBINATION_COUNT> weights = {};
        CanonicalComboLegalMask legal_mask = {};
        MultiwayRangeBeliefMetadata metadata = {};
    };

    static Row make_uniform_row(const MultiwayRangeBeliefSeatInput& input, std::uint64_t revision);
    static Row make_supplied_row(const MultiwayRangeBeliefSeatInput& input, std::uint64_t revision);
    static void normalize_row(Row& row);
    static void apply_observation_metadata(
        Row& row,
        const MultiwayRangeBeliefObservation& observation,
        std::uint64_t revision,
        double input_mass) noexcept;
    static void validate_seat_reset(std::size_t seat_count, const MultiwayRangeBeliefSeatInput* seats);
    using RowBuilder = Row (*)(const MultiwayRangeBeliefSeatInput&, std::uint64_t);
    void reset_rows(
        std::size_t seat_count,
        const MultiwayRangeBeliefSeatInput* seats,
        RowBuilder builder);

    std::array<Row, MULTIWAY_RANGE_BELIEF_MAX_SEATS> rows_ = {};
    std::size_t seat_count_ = 0U;
    std::uint64_t revision_ = 0U;
};

}  // namespace core
