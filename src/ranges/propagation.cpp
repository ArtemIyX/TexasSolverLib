#include "ranges/propagation.hpp"

#include <algorithm>
#include <stdexcept>
#include <utility>

namespace core {

namespace {

std::uint16_t encode_hole(std::array<std::uint8_t, 2> hole) {
    if (hole[1] < hole[0]) {
        std::swap(hole[0], hole[1]);
    }
    return static_cast<std::uint16_t>(
        (static_cast<std::uint16_t>(hole[0]) << 8U) |
        static_cast<std::uint16_t>(hole[1]));
}

bool overlaps_dead_cards(
    const std::array<std::uint8_t, 2>& hole,
    const std::vector<std::uint8_t>& dead_cards) {
    for (const auto card : dead_cards) {
        if (hole[0] == card || hole[1] == card) {
            return true;
        }
    }
    return false;
}

RangeMask make_identity_mask(std::size_t value_count, RangeVector::Kind kind) {
    RangeMask mask;
    mask.kind = kind;
    mask.enabled.assign(value_count, 1U);
    return mask;
}

CanonicalRange normalize_canonical_range(CanonicalRange range) {
    if (range.mask.empty()) {
        range.mask = make_identity_mask(range.range.size(), range.range.kind);
    }
    apply_mask(range.range, range.mask);
    return range;
}

}  // namespace

std::size_t ComboIndex::size() const noexcept {
    return hands.size();
}

bool ComboIndex::empty() const noexcept {
    return hands.empty();
}

bool ComboIndex::contains(const std::array<std::uint8_t, 2>& hole) const noexcept {
    return hand_to_index.find(encode_hole(hole)) != hand_to_index.end();
}

std::size_t ComboIndex::index_of(const std::array<std::uint8_t, 2>& hole) const {
    const auto it = hand_to_index.find(encode_hole(hole));
    if (it == hand_to_index.end()) {
        throw std::out_of_range("ComboIndex::index_of missing hole");
    }
    return it->second;
}

ComboIndex ComboIndex::from_board(const std::vector<std::uint8_t>& board_cards) {
    return enumerate_combos(board_cards);
}

ComboIndex enumerate_combos(const std::vector<std::uint8_t>& board) {
    std::array<bool, 64> blocked = {};
    for (const auto card : board) {
        blocked[card] = true;
    }

    ComboIndex out;
    out.board = board;
    out.hands.reserve(1326);
    for (std::uint8_t rank0 = 2; rank0 <= 14; ++rank0) {
        for (std::uint8_t suit0 = 0; suit0 < 4; ++suit0) {
            const auto card0 = card_to_int(rank0, suit0);
            if (blocked[card0]) {
                continue;
            }
            for (std::uint8_t rank1 = 2; rank1 <= 14; ++rank1) {
                for (std::uint8_t suit1 = 0; suit1 < 4; ++suit1) {
                    const auto card1 = card_to_int(rank1, suit1);
                    if (blocked[card1] || card0 >= card1) {
                        continue;
                    }
                    const std::array<std::uint8_t, 2> hole = {card0, card1};
                    out.hands.push_back(hole);
                    out.hand_to_index.emplace(encode_hole(hole), out.hands.size() - 1U);
                }
            }
        }
    }
    return out;
}

RangeMask board_mask(const ComboIndex& combos, const std::vector<std::uint8_t>& board) {
    return dead_card_mask(combos, board);
}

RangeMask card_mask(const ComboIndex& combos, std::uint8_t card) {
    return dead_card_mask(combos, std::vector<std::uint8_t>{card});
}

RangeMask dead_card_mask(const ComboIndex& combos, const std::vector<std::uint8_t>& dead_cards) {
    RangeMask mask;
    mask.kind = RangeVector::Kind::Combo;
    mask.enabled.reserve(combos.hands.size());
    for (const auto& hole : combos.hands) {
        mask.enabled.push_back(static_cast<std::uint8_t>(!overlaps_dead_cards(hole, dead_cards)));
    }
    return mask;
}

CanonicalRange propagate_range_to_action(
    const CanonicalRange& parent,
    const ActionRangeFilter& filter) {
    if (parent.range.kind != RangeVector::Kind::Combo &&
        parent.range.kind != RangeVector::Kind::Bucket) {
        throw std::invalid_argument("propagate_range_to_action received unsupported range kind");
    }

    if (!filter.mask.empty()) {
        if (filter.mask.kind != parent.range.kind) {
            throw std::invalid_argument("action filter mask kind must match parent range kind");
        }
        if (filter.mask.size() != parent.range.size()) {
            throw std::invalid_argument("action filter mask size must match parent range size");
        }
    }
    if (!filter.multipliers.empty() && filter.multipliers.size() != parent.range.size()) {
        throw std::invalid_argument("action filter multipliers size must match parent range size");
    }

    CanonicalRange out = parent;
    if (out.mask.empty()) {
        out.mask = make_identity_mask(out.range.size(), out.range.kind);
    }
    if (!filter.mask.empty()) {
        out.mask = combine_masks(out.mask, filter.mask);
    }
    for (std::size_t i = 0; i < out.range.weights.size(); ++i) {
        if (!out.mask.empty() && !out.mask.allows(i)) {
            out.range.weights[i] = 0.0;
            continue;
        }
        if (!filter.multipliers.empty()) {
            out.range.weights[i] *= filter.multipliers[i];
        }
    }
    return normalize_canonical_range(std::move(out));
}

std::vector<ActionRangeTransition> propagate_range_to_actions(
    const CanonicalRange& parent,
    const std::vector<ActionRangeFilter>& filters) {
    std::vector<ActionRangeTransition> out;
    out.reserve(filters.size());
    for (const auto& filter : filters) {
        out.push_back(ActionRangeTransition{
            filter.action,
            propagate_range_to_action(parent, filter),
        });
    }
    return out;
}

ChanceRangeTransition propagate_range_to_chance_card(
    const CanonicalRange& parent,
    const ComboIndex& combos,
    std::uint8_t card,
    Probability probability) {
    if (parent.range.kind != RangeVector::Kind::Combo) {
        throw std::invalid_argument("chance propagation currently requires combo ranges");
    }
    if (parent.range.size() != combos.size()) {
        throw std::invalid_argument("chance propagation requires combo index aligned with parent range");
    }

    CanonicalRange out = parent;
    if (out.mask.empty()) {
        out.mask = make_identity_mask(out.range.size(), out.range.kind);
    }
    const auto chance_mask = card_mask(combos, card);
    out.mask = combine_masks(out.mask, chance_mask);
    out = normalize_canonical_range(std::move(out));
    out.context.street = parent.context.street;

    ChanceRangeTransition transition;
    transition.card = card;
    transition.probability = probability;
    transition.range = std::move(out);
    return transition;
}

std::vector<ChanceRangeTransition> propagate_range_to_chance_outcomes(
    const CanonicalRange& parent,
    const ComboIndex& combos,
    const std::vector<ChanceOutcome>& outcomes) {
    std::vector<ChanceRangeTransition> out;
    out.reserve(outcomes.size());
    for (const auto& outcome : outcomes) {
        out.push_back(propagate_range_to_chance_card(
            parent,
            combos,
            static_cast<std::uint8_t>(outcome.action),
            outcome.probability));
    }
    return out;
}

}  // namespace core
