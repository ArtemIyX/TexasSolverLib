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

constexpr std::uint64_t kMaxFeasibilityNodes = 1'000'000U;

bool find_compatible_deal(
    const std::vector<std::vector<MultiwayWeightedHole>>& ranges,
    const std::array<std::size_t, 6>& order,
    std::size_t depth,
    std::array<bool, 64>& used,
    std::uint64_t& visited) {
    if (++visited > kMaxFeasibilityNodes) {
        throw std::runtime_error("multiway private range feasibility search exceeded its bounded budget");
    }
    if (depth == ranges.size()) return true;
    const auto seat = order[depth];
    for (const auto& entry : ranges[seat]) {
        if (entry.weight <= 0.0 || overlaps(entry.hole, used)) continue;
        mark(entry.hole, used);
        if (find_compatible_deal(ranges, order, depth + 1U, used, visited)) return true;
        used[entry.hole[0]] = false;
        used[entry.hole[1]] = false;
    }
    return false;
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
    PcsRng rng(seed);
    std::vector<std::vector<double>> weights(config.ranges.size());
    for (std::size_t seat = 0; seat < config.ranges.size(); ++seat) {
        weights[seat].reserve(config.ranges[seat].size());
        for (const auto& entry : config.ranges[seat]) weights[seat].push_back(entry.weight);
    }
    for (std::uint32_t attempt = 1; attempt <= config.max_rejection_attempts; ++attempt) {
        std::array<bool, 64> used = {};
        for (const auto card : config.board) used[card] = true;
        MultiwayJointPrivateSample sample;
        sample.holes.resize(config.ranges.size());
        sample.attempts = attempt;
        bool compatible = true;
        for (std::size_t seat = 0; seat < config.ranges.size(); ++seat) {
            const auto selected = rng.sample_weighted(weights[seat]).first;
            const auto hole = config.ranges[seat][selected].hole;
            if (overlaps(hole, used)) {
                compatible = false;
                break;
            }
            sample.holes[seat] = hole;
            mark(hole, used);
        }
        if (compatible) return sample;
    }
    throw std::runtime_error("unable to sample compatible multiway private hands within rejection limit");
}

MultiwayCompiledPrivateRanges::MultiwayCompiledPrivateRanges(const MultiwayPrivateConfig& config)
    : board_(config.board), max_rejection_attempts_(config.max_rejection_attempts) {
    config.validate();
    ranges_.resize(config.ranges.size());
    cumulative_weights_.resize(config.ranges.size());
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
    }

    std::array<std::size_t, 6> order = {};
    for (std::size_t seat = 0; seat < ranges_.size(); ++seat) order[seat] = seat;
    std::sort(order.begin(), order.begin() + ranges_.size(),
              [this](std::size_t lhs, std::size_t rhs) {
                  return ranges_[lhs].size() < ranges_[rhs].size();
              });
    std::array<bool, 64> used = {};
    for (const auto card : board_) used[card] = true;
    std::uint64_t visited = 0;
    if (!find_compatible_deal(ranges_, order, 0, used, visited)) {
        throw std::invalid_argument("multiway private ranges have no compatible joint deal");
    }
}

bool MultiwayCompiledPrivateRanges::try_sample_into(
    std::uint64_t seed,
    MultiwayPrivateWorkerScratch& scratch) const noexcept {
    PcsRng rng(seed);
    for (std::uint32_t attempt = 1; attempt <= max_rejection_attempts_; ++attempt) {
        scratch.used.fill(false);
        for (const auto card : board_) scratch.used[card] = true;
        bool compatible = true;
        for (std::size_t seat = 0; seat < ranges_.size(); ++seat) {
            const auto& hole = ranges_[seat][sample_cumulative(cumulative_weights_[seat], rng)].hole;
            if (overlaps(hole, scratch.used)) { compatible = false; break; }
            scratch.holes[seat] = hole;
            mark(hole, scratch.used);
        }
        if (compatible) {
            scratch.seat_count = static_cast<std::uint8_t>(ranges_.size());
            scratch.attempts = attempt;
            return true;
        }
    }
    return false;
}

void MultiwayCompiledPrivateRanges::sample_into(
    std::uint64_t seed,
    MultiwayPrivateWorkerScratch& scratch) const {
    if (try_sample_into(seed, scratch)) return;
    throw std::runtime_error("unable to sample compatible compiled multiway private hands within rejection limit");
}

std::size_t MultiwayCompiledPrivateRanges::seat_count() const noexcept { return ranges_.size(); }

void MultiwayShowdownInput::validate() const {
    const auto count = holes.size();
    if (count < 2U || count > 6U || board.size() != 5U || contributions.size() != count || folded.size() != count ||
        odd_chip_first_seat < 0 || static_cast<std::size_t>(odd_chip_first_seat) >= count ||
        !are_valid_and_distinct_cards(board.data(), board.size())) {
        throw std::invalid_argument("MultiwayShowdownInput has invalid dimensions or board");
    }
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
