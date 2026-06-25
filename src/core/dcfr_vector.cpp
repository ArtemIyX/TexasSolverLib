#include "core/dcfr_vector.hpp"

#include <utility>
#include <stdexcept>

namespace core {

namespace {

std::string hole_string(const std::array<std::uint8_t, 2>& hole) {
    std::array<std::uint8_t, 2> sorted = hole;
    if (sorted[1] < sorted[0]) {
        std::swap(sorted[0], sorted[1]);
    }
    return card_to_string(sorted[0]) + card_to_string(sorted[1]);
}

}

VectorInfosetData::VectorInfosetData(std::size_t action_count_in, std::size_t hand_count_in)
    : action_count(action_count_in),
      hand_count(hand_count_in),
      regret(action_count_in * hand_count_in, 0.0),
      strategy_sum(action_count_in * hand_count_in, 0.0),
      last_discount_iter(0) {}

EvalContext EvalContext::from_root(const HUNLState& initial) {
    std::array<bool, 64> held = {};
    for (const auto card : initial.board) {
        held[card] = true;
    }

    std::vector<std::array<std::uint8_t, 2>> single_holes;
    for (std::uint8_t r0 = 2; r0 <= 14; ++r0) {
        for (std::uint8_t s0 = 0; s0 < 4; ++s0) {
            const auto c0 = card_to_int(r0, s0);
            if (held[c0]) {
                continue;
            }
            for (std::uint8_t r1 = 2; r1 <= 14; ++r1) {
                for (std::uint8_t s1 = 0; s1 < 4; ++s1) {
                    const auto c1 = card_to_int(r1, s1);
                    if (held[c1] || c0 >= c1) {
                        continue;
                    }
                    single_holes.push_back({c0, c1});
                }
            }
        }
    }

    return from_hand_lists(single_holes, single_holes, initial.config ? initial.config->big_blind : 0);
}

EvalContext EvalContext::from_suit_iso(const HUNLState&) {
    throw std::logic_error("EvalContext::from_suit_iso is not implemented yet");
}

EvalContext EvalContext::from_hand_lists(
    std::vector<std::array<std::uint8_t, 2>> p0_holes,
    std::vector<std::array<std::uint8_t, 2>> p1_holes,
    int big_blind) {
    EvalContext ctx;
    ctx.hand_count = {p0_holes.size(), p1_holes.size()};
    ctx.hole[0] = std::move(p0_holes);
    ctx.hole[1] = std::move(p1_holes);
    ctx.hole_str[0].reserve(ctx.hole[0].size());
    ctx.hole_str[1].reserve(ctx.hole[1].size());
    for (const auto& hand : ctx.hole[0]) {
        ctx.hole_str[0].push_back(hole_string(hand));
    }
    for (const auto& hand : ctx.hole[1]) {
        ctx.hole_str[1].push_back(hole_string(hand));
    }
    ctx.big_blind = big_blind;
    return ctx;
}

}  // namespace core
