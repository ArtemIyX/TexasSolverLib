#include "core/dcfr_vector.hpp"

#include "core/dcfr.hpp"
#include "core/pcs.hpp"
#include "core/simd.hpp"
#include "core/suit_iso.hpp"

#include <cmath>
#include <utility>
#include <stdexcept>

namespace core {

namespace {

std::string make_hole_string(const std::array<std::uint8_t, 2>& hole) {
    std::array<std::uint8_t, 2> sorted = hole;
    if (sorted[1] < sorted[0]) {
        std::swap(sorted[0], sorted[1]);
    }
    return card_to_string(sorted[0]) + card_to_string(sorted[1]);
}

}  // namespace

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

EvalContext EvalContext::from_suit_iso(const HUNLState& initial) {
    return from_root(initial);
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
        ctx.hole_str[0].push_back(make_hole_string(hand));
    }
    for (const auto& hand : ctx.hole[1]) {
        ctx.hole_str[1].push_back(make_hole_string(hand));
    }
    ctx.big_blind = big_blind;
    return ctx;
}

VectorDCFR VectorDCFR::new_solver(
    const BettingTree& tree,
    std::array<std::size_t, 2> hand_count_per_player,
    double alpha,
    double beta,
    double gamma) {
    return with_init_noise(tree, hand_count_per_player, alpha, beta, gamma, 0.0, 0);
}

VectorDCFR VectorDCFR::with_init_noise(
    const BettingTree& tree,
    std::array<std::size_t, 2> hand_count_per_player,
    double alpha,
    double beta,
    double gamma,
    double regret_init_noise,
    std::uint64_t rng_seed) {
    return with_init_noise_masked(
        tree,
        hand_count_per_player,
        alpha,
        beta,
        gamma,
        regret_init_noise,
        rng_seed,
        {});
}

VectorDCFR VectorDCFR::with_init_noise_masked(
    const BettingTree& tree,
    std::array<std::size_t, 2> hand_count_per_player,
    double alpha_in,
    double beta_in,
    double gamma_in,
    double regret_init_noise,
    std::uint64_t rng_seed,
    const std::vector<bool>& skip_mask) {
    validate_alpha(alpha_in);
    if (!skip_mask.empty() && skip_mask.size() != tree.nodes.size()) {
        throw std::invalid_argument("skip_mask must be empty or node-aligned");
    }

    PcsRng init_rng(rng_seed);
    VectorDCFR solver;
    solver.alpha = alpha_in;
    solver.beta = beta_in;
    solver.gamma = gamma_in;
    solver.iteration = 0;
    solver.infosets.reserve(tree.nodes.size());
    solver.has_chance_template.assign(tree.nodes.size(), false);
    solver.chance_depth.assign(tree.nodes.size(), 0);

    for (std::size_t node_idx = 0; node_idx < tree.nodes.size(); ++node_idx) {
        const auto& node = tree.nodes[node_idx];
        if (node.tag == FlatNodeTag::Decision && !skip_mask.empty() && skip_mask[node_idx]) {
            solver.infosets.push_back(std::nullopt);
            continue;
        }
        if (node.tag == FlatNodeTag::Decision) {
            const auto action_count = node.actions.size();
            const auto hand_count = hand_count_per_player.at(node.player);
            VectorInfosetData info(action_count, hand_count);
            if (regret_init_noise > 0.0) {
                for (auto& slot : info.regret) {
                    slot = regret_init_noise * init_rng.next_f64_signed();
                }
            }
            solver.infosets.push_back(std::move(info));
        } else {
            solver.infosets.push_back(std::nullopt);
        }
    }

    return solver;
}

void VectorDCFR::compute_strategy(const VectorInfosetData& info, std::vector<double>& out) {
    const auto total = info.hand_count * info.action_count;
    out.assign(total, 0.0);
    for (std::size_t h = 0; h < info.hand_count; ++h) {
        const auto offset = h * info.action_count;
        compute_strategy_row(
            info.regret.data() + offset,
            out.data() + offset,
            info.action_count);
    }
}

void VectorDCFR::compute_avg_strategy(const VectorInfosetData& info, std::vector<double>& out) {
    const auto total = info.hand_count * info.action_count;
    out.assign(total, 0.0);
    for (std::size_t h = 0; h < info.hand_count; ++h) {
        const auto offset = h * info.action_count;
        double normalizing = 0.0;
        for (std::size_t a = 0; a < info.action_count; ++a) {
            normalizing += info.strategy_sum[offset + a];
        }
        if (normalizing > 0.0) {
            for (std::size_t a = 0; a < info.action_count; ++a) {
                out[offset + a] = info.strategy_sum[offset + a] / normalizing;
            }
        } else if (info.action_count > 0) {
            const auto prob = 1.0 / static_cast<double>(info.action_count);
            for (std::size_t a = 0; a < info.action_count; ++a) {
                out[offset + a] = prob;
            }
        }
    }
}

void VectorDCFR::discount(VectorInfosetData& info, std::uint32_t t, double alpha_in, double beta_in, double gamma_in) {
    if (info.last_discount_iter >= t) {
        return;
    }
    for (std::uint32_t tt = info.last_discount_iter + 1; tt <= t; ++tt) {
        const auto tt_f = static_cast<double>(tt);
        const auto ta = std::pow(tt_f, alpha_in);
        const auto tb = std::pow(tt_f, beta_in);
        const auto pos_scale = ta / (ta + 1.0);
        const auto neg_scale = tb / (tb + 1.0);
        const auto strat_scale = std::pow(tt_f / (tt_f + 1.0), gamma_in);
        discount_regrets(info.regret.data(), info.regret.size(), pos_scale, neg_scale);
        discount_strategy_sum(info.strategy_sum.data(), info.strategy_sum.size(), strat_scale);
    }
    info.last_discount_iter = t;
}

}  // namespace core
