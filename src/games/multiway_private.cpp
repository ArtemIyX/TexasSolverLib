#include "games/multiway_private.hpp"

#include "util/pcs.hpp"

#include <array>
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace core {

namespace {

bool overlaps(const std::array<std::uint8_t, 2>& hole, const std::array<bool, 64>& used) {
    return used[hole[0]] || used[hole[1]];
}

void mark(const std::array<std::uint8_t, 2>& hole, std::array<bool, 64>& used) {
    used[hole[0]] = true;
    used[hole[1]] = true;
}

std::size_t sample_cumulative(const std::vector<double>& cumulative, PcsRng& rng) {
    const auto draw = rng.next_unit_f64() * cumulative.back();
    return static_cast<std::size_t>(std::lower_bound(cumulative.begin(), cumulative.end(), draw) - cumulative.begin());
}

MultiwayPrivateRangeFeasibilityStatus find_compatible_deal(
    const std::vector<std::vector<MultiwayWeightedHole>>& ranges,
    const std::array<std::size_t, 6>& order,
    std::size_t depth,
    std::array<bool, 64>& used,
    std::uint64_t& visited,
    std::uint64_t node_budget) {
    if (visited >= node_budget) return MultiwayPrivateRangeFeasibilityStatus::SearchBudgetExhausted;
    ++visited;
    if (depth == ranges.size()) return MultiwayPrivateRangeFeasibilityStatus::Feasible;
    const auto seat = order[depth];
    for (const auto& entry : ranges[seat]) {
        if (entry.weight <= 0.0 || overlaps(entry.hole, used)) continue;
        mark(entry.hole, used);
        const auto status = find_compatible_deal(ranges, order, depth + 1U, used, visited, node_budget);
        if (status != MultiwayPrivateRangeFeasibilityStatus::Infeasible) return status;
        used[entry.hole[0]] = false;
        used[entry.hole[1]] = false;
    }
    return MultiwayPrivateRangeFeasibilityStatus::Infeasible;
}

}  // namespace

void MultiwayPrivateConfig::validate() const {
    if (ranges.size() < 2U || ranges.size() > 6U || board.size() > 5U || max_rejection_attempts == 0U ||
        !are_valid_and_distinct_cards(board.data(), board.size())) {
        throw std::invalid_argument("MultiwayPrivateConfig has invalid seats, board, or sampling limit");
    }
    std::array<bool, 64> board_used = {};
    for (const auto card : board) board_used[card] = true;
    for (const auto& range : ranges) {
        double total = 0.0;
        if (range.empty()) throw std::invalid_argument("multiway private range must not be empty");
        for (const auto& entry : range) {
            if (!are_valid_and_distinct_cards(entry.hole.data(), entry.hole.size()) || overlaps(entry.hole, board_used) ||
                !std::isfinite(entry.weight) || entry.weight < 0.0) {
                throw std::invalid_argument("multiway private range has an invalid hand or weight");
            }
            total += entry.weight;
        }
        if (!std::isfinite(total) || total <= 0.0) {
            throw std::invalid_argument("multiway private range must have positive finite mass");
        }
    }
}

MultiwayJointPrivateSample sample_multiway_private_hands(
    const MultiwayPrivateConfig& config,
    std::uint64_t seed) {
    config.validate();
    MultiwayCompiledPrivateRanges compiled = [&config] {
        try {
            return MultiwayCompiledPrivateRanges(config);
        } catch (const std::invalid_argument&) {
            // This compatibility API historically reports an impossible
            // joint draw as rejection exhaustion. The compiled API keeps the
            // stronger preflight distinction for coordinator use.
            throw std::runtime_error(
                "unable to sample compatible multiway private hands within rejection limit");
        }
    }();
    MultiwayPrivateWorkerScratch scratch;
    compiled.sample_into(seed, scratch);

    MultiwayJointPrivateSample sample;
    sample.holes.assign(scratch.holes.begin(), scratch.holes.begin() + scratch.seat_count);
    sample.attempts = scratch.attempts;
    sample.chance_reach = scratch.chance_reach;
    sample.conditional_deal_probability = scratch.conditional_deal_probability;
    sample.proposal_reach = scratch.proposal_reach;
    sample.inclusion_reach = scratch.inclusion_reach;
    sample.accepted_trajectories = scratch.accepted_trajectories;
    sample.rejected_trajectories = scratch.rejected_trajectories;
    sample.discarded_trajectories = scratch.discarded_trajectories;
    return sample;
}

MultiwayPrivateRangeFeasibilityResult preflight_multiway_private_range_feasibility(
    const MultiwayPrivateConfig& config,
    std::uint64_t node_budget) {
    config.validate();

    std::array<std::size_t, 6> order = {};
    for (std::size_t seat = 0; seat < config.ranges.size(); ++seat) order[seat] = seat;
    std::sort(order.begin(), order.begin() + config.ranges.size(),
              [&config](std::size_t lhs, std::size_t rhs) {
                  return config.ranges[lhs].size() < config.ranges[rhs].size();
              });
    std::array<bool, 64> used = {};
    for (const auto card : config.board) used[card] = true;

    MultiwayPrivateRangeFeasibilityResult result;
    result.node_budget = node_budget;
    result.status = find_compatible_deal(config.ranges, order, 0, used, result.visited_nodes, node_budget);
    switch (result.status) {
    case MultiwayPrivateRangeFeasibilityStatus::Feasible:
        result.reason = "compatible joint deal found";
        break;
    case MultiwayPrivateRangeFeasibilityStatus::Infeasible:
        result.reason = "no compatible joint deal exists";
        break;
    case MultiwayPrivateRangeFeasibilityStatus::SearchBudgetExhausted:
        result.reason = "feasibility search exhausted its node budget";
        break;
    }
    return result;
}

MultiwayCompiledPrivateRanges::MultiwayCompiledPrivateRanges(const MultiwayPrivateConfig& config)
    : board_(config.board) {
    config.validate();
    const auto feasibility = preflight_multiway_private_range_feasibility(config);
    if (feasibility.status == MultiwayPrivateRangeFeasibilityStatus::Infeasible) {
        throw std::invalid_argument("multiway private ranges have no compatible joint deal");
    }
    if (feasibility.status == MultiwayPrivateRangeFeasibilityStatus::SearchBudgetExhausted) {
        throw std::runtime_error("multiway private range feasibility preflight exhausted its node budget");
    }
    ranges_.resize(config.ranges.size());
    cumulative_weights_.resize(config.ranges.size());
    range_totals_.resize(config.ranges.size(), 0.0);
    for (std::size_t seat = 0; seat < config.ranges.size(); ++seat) {
        auto entries = config.ranges[seat];
        for (auto& entry : entries) {
            if (entry.hole[1] < entry.hole[0]) std::swap(entry.hole[0], entry.hole[1]);
        }
        std::sort(entries.begin(), entries.end(), [](const auto& lhs, const auto& rhs) {
            return lhs.hole < rhs.hole;
        });
        for (const auto& entry : entries) {
            if (entry.weight == 0.0) continue;
            if (!ranges_[seat].empty() && ranges_[seat].back().hole == entry.hole) {
                ranges_[seat].back().weight += entry.weight;
            } else {
                ranges_[seat].push_back(entry);
            }
        }
        double total = 0.0;
        cumulative_weights_[seat].reserve(ranges_[seat].size());
        for (const auto& entry : ranges_[seat]) {
            total += entry.weight;
            cumulative_weights_[seat].push_back(total);
        }
        if (ranges_[seat].empty()) {
            throw std::invalid_argument("multiway compiled private range has no positive-mass hand");
        }
        range_totals_[seat] = total;
    }

}

bool MultiwayCompiledPrivateRanges::try_sample_into(
    std::uint64_t seed,
    MultiwayPrivateWorkerScratch& scratch) const noexcept {
    scratch.seat_count = 0;
    scratch.attempts = 0;
    scratch.holes = {};
    scratch.chance_reach = 0.0;
    scratch.conditional_deal_probability = 0.0;
    scratch.proposal_reach = 0.0;
    scratch.inclusion_reach = 1.0;
    scratch.accepted_trajectories = 0;
    scratch.rejected_trajectories = 0;
    scratch.discarded_trajectories = 0;
    PcsRng rng(seed);
    scratch.used.fill(false);
    for (const auto card : board_) scratch.used[card] = true;
    bool compatible = true;
    double chance_reach = 1.0;
    for (std::size_t seat = 0; seat < ranges_.size(); ++seat) {
        const auto selected = sample_cumulative(cumulative_weights_[seat], rng);
        const auto& entry = ranges_[seat][selected];
        const auto& hole = entry.hole;
        chance_reach *= entry.weight / range_totals_[seat];
        if (overlaps(hole, scratch.used)) {
            compatible = false;
            break;
        }
        scratch.holes[seat] = hole;
        mark(hole, scratch.used);
    }
    scratch.attempts = 1U;
    if (compatible) {
        scratch.seat_count = static_cast<std::uint8_t>(ranges_.size());
        scratch.chance_reach = chance_reach;
        scratch.conditional_deal_probability = chance_reach;
        scratch.proposal_reach = chance_reach;
        scratch.accepted_trajectories = 1;
        return true;
    }
    scratch.holes = {};
    scratch.rejected_trajectories = 1U;
    scratch.discarded_trajectories = 1;
    return false;
}

void MultiwayCompiledPrivateRanges::sample_into(
    std::uint64_t seed,
    MultiwayPrivateWorkerScratch& scratch) const {
    if (try_sample_into(seed, scratch)) return;
    throw std::runtime_error("independent multiway private proposal collided");
}

std::size_t MultiwayCompiledPrivateRanges::seat_count() const noexcept { return ranges_.size(); }

void MultiwayShowdownInput::validate() const {
    const auto count = holes.size();
    if (count < 2U || count > 6U || board.size() != 5U || contributions.size() != count || folded.size() != count ||
        odd_chip_first_seat < 0 || static_cast<std::size_t>(odd_chip_first_seat) >= count ||
        !are_valid_and_distinct_cards(board.data(), board.size())) {
        throw std::invalid_argument("MultiwayShowdownInput has invalid dimensions or board");
    }
    rake_policy.validate();
    std::array<bool, 64> used = {};
    for (const auto card : board) used[card] = true;
    for (const auto& hole : holes) {
        if (!are_valid_and_distinct_cards(hole.data(), hole.size()) || overlaps(hole, used)) {
            throw std::invalid_argument("MultiwayShowdownInput has duplicate or invalid private cards");
        }
        mark(hole, used);
    }
}

MultiwayTerminalResult evaluate_multiway_showdown(const MultiwayShowdownInput& input) {
    input.validate();
    MultiwayTerminalInput terminal;
    terminal.contributions = input.contributions;
    terminal.folded = input.folded;
    terminal.odd_chip_first_seat = input.odd_chip_first_seat;
    terminal.rake_policy = input.rake_policy;
    terminal.flop_seen = input.flop_seen;
    terminal.strengths.resize(input.holes.size());
    for (std::size_t seat = 0; seat < input.holes.size(); ++seat) {
        std::array<std::uint8_t, 7> cards = {};
        for (std::size_t card = 0; card < input.board.size(); ++card) cards[card] = input.board[card];
        cards[5] = input.holes[seat][0];
        cards[6] = input.holes[seat][1];
        terminal.strengths[seat] = Strength::evaluate_7(cards);
    }
    return settle_multiway_terminal(terminal);
}

}  // namespace core
