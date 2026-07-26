#include "games/multiway_terminal.hpp"

#include <algorithm>
#include <cstdint>
#include <limits>
#include <numeric>
#include <stdexcept>

namespace core {

void MultiwayRakePolicy::validate() const {
    if (mode == MultiwayRakeMode::ExplicitZero) {
        if (basis_points != 0U || cap != 0 || !no_flop_no_drop) {
            throw std::invalid_argument("explicit zero rake must have zero rate, cap, and no-drop setting");
        }
        return;
    }
    if (mode != MultiwayRakeMode::PercentageOfContestedPot ||
        basis_points == 0U || basis_points > 10'000U || cap <= 0) {
        throw std::invalid_argument("multiway rake policy has an invalid percentage or cap");
    }
}

int MultiwayRakePolicy::rake_for_contested_pot(int contested_pot, bool flop_seen) const {
    validate();
    if (contested_pot < 0) throw std::invalid_argument("multiway rake contested pot must be non-negative");
    if (mode == MultiwayRakeMode::ExplicitZero || (no_flop_no_drop && !flop_seen)) return 0;
    const auto raw = static_cast<std::int64_t>(contested_pot) * basis_points / 10'000;
    return static_cast<int>(std::min<std::int64_t>(raw, cap));
}

std::uint64_t MultiwayRakePolicy::identity() const noexcept {
    return static_cast<std::uint64_t>(mode) |
        (static_cast<std::uint64_t>(no_flop_no_drop) << 2U) |
        (static_cast<std::uint64_t>(basis_points) << 3U) |
        (static_cast<std::uint64_t>(static_cast<std::uint32_t>(cap)) << 17U);
}

void MultiwayTerminalInput::validate() const {
    const auto count = contributions.size();
    if (count < 2U || count > 6U || folded.size() != count || strengths.size() != count) {
        throw std::invalid_argument("MultiwayTerminalInput requires equal two-through-six seat vectors");
    }
    if (odd_chip_first_seat < 0 || static_cast<std::size_t>(odd_chip_first_seat) >= count) {
        throw std::invalid_argument("MultiwayTerminalInput odd-chip seat is out of range");
    }
    if (std::none_of(folded.begin(), folded.end(), [](bool value) { return !value; })) {
        throw std::invalid_argument("MultiwayTerminalInput requires a non-folded player");
    }
    if (std::any_of(contributions.begin(), contributions.end(), [](int amount) { return amount < 0; })) {
        throw std::invalid_argument("MultiwayTerminalInput contributions must be non-negative");
    }
    const auto total = std::accumulate(contributions.begin(), contributions.end(), std::int64_t{0});
    if (total > std::numeric_limits<int>::max()) {
        throw std::invalid_argument("MultiwayTerminalInput total contributions exceed supported chip range");
    }
    rake_policy.validate();
}

MultiwayPotLayout build_multiway_pot_layout(
    const std::vector<int>& contributions,
    const std::vector<bool>& folded) {
    const auto count = contributions.size();
    if (count < 2U || count > 6U || folded.size() != count ||
        std::any_of(contributions.begin(), contributions.end(), [](int amount) { return amount < 0; }) ||
        std::none_of(folded.begin(), folded.end(), [](bool value) { return !value; })) {
        throw std::invalid_argument("invalid multiway pot layout input");
    }
    const auto total = std::accumulate(contributions.begin(), contributions.end(), std::int64_t{0});
    if (total > std::numeric_limits<int>::max()) {
        throw std::invalid_argument("multiway pot layout exceeds supported chip range");
    }
    MultiwayPotLayout layout;
    layout.refunds.assign(count, 0);

    std::vector<int> levels = contributions;
    std::sort(levels.begin(), levels.end());
    levels.erase(std::unique(levels.begin(), levels.end()), levels.end());

    int previous_level = 0;
    for (const auto level : levels) {
        if (level == 0) continue;
        std::vector<PlayerId> contributors;
        std::vector<PlayerId> eligible;
        for (std::size_t seat = 0; seat < count; ++seat) {
            if (contributions[seat] >= level) {
                contributors.push_back(static_cast<PlayerId>(seat));
                if (!folded[seat]) eligible.push_back(static_cast<PlayerId>(seat));
            }
        }
        const auto layer_amount = (level - previous_level) * static_cast<int>(contributors.size());
        previous_level = level;
        if (contributors.empty() || layer_amount == 0) continue;
        if (contributors.size() == 1U) {
            layout.refunds[static_cast<std::size_t>(contributors.front())] += layer_amount;
            continue;
        }
        if (eligible.empty()) {
            throw std::invalid_argument("side-pot layer has no eligible non-folded player");
        }
        layout.pots.push_back({layer_amount, level, std::move(eligible)});
    }
    return layout;
}

MultiwayTerminalResult settle_multiway_terminal(const MultiwayTerminalInput& input) {
    input.validate();
    return settle_multiway_terminal(input, build_multiway_pot_layout(input.contributions, input.folded));
}

MultiwayTerminalResult settle_multiway_terminal(
    const MultiwayTerminalInput& input,
    const MultiwayPotLayout& layout) {
    input.validate();
    if (layout.refunds.size() != input.contributions.size()) {
        throw std::invalid_argument("multiway pot layout does not match terminal input");
    }
    const auto expected = build_multiway_pot_layout(input.contributions, input.folded);
    if (layout.refunds != expected.refunds || layout.pots.size() != expected.pots.size()) {
        throw std::invalid_argument("multiway pot layout does not match contribution and fold signature");
    }
    for (std::size_t pot_index = 0; pot_index < layout.pots.size(); ++pot_index) {
        const auto& actual = layout.pots[pot_index];
        const auto& expected_pot = expected.pots[pot_index];
        if (actual.amount != expected_pot.amount ||
            actual.contribution_cap != expected_pot.contribution_cap ||
            actual.eligible_players != expected_pot.eligible_players) {
            throw std::invalid_argument("multiway pot layout does not match contribution and fold signature");
        }
    }
    std::int64_t layout_total = 0;
    for (std::size_t seat = 0; seat < layout.refunds.size(); ++seat) {
        if (layout.refunds[seat] < 0) throw std::invalid_argument("multiway pot layout has a negative refund");
        layout_total += layout.refunds[seat];
    }
    for (const auto& pot : layout.pots) {
        if (pot.amount <= 0 || pot.eligible_players.empty()) {
            throw std::invalid_argument("multiway pot layout has an invalid pot");
        }
        layout_total += pot.amount;
        for (const auto player : pot.eligible_players) {
            if (player < 0 || static_cast<std::size_t>(player) >= input.contributions.size() ||
                input.folded[static_cast<std::size_t>(player)]) {
                throw std::invalid_argument("multiway pot layout has an invalid eligible player");
            }
        }
    }
    const auto input_total = std::accumulate(input.contributions.begin(), input.contributions.end(), std::int64_t{0});
    if (layout_total != input_total) {
        throw std::invalid_argument("multiway pot layout does not conserve contributions");
    }
    MultiwayTerminalResult result;
    const auto count = input.contributions.size();
    result.pots = layout.pots;
    result.refunds = layout.refunds;
    result.payouts.assign(count, 0);
    result.utilities.assign(count, 0.0);

    const auto contested_total = std::accumulate(
        result.pots.begin(), result.pots.end(), std::int64_t{0},
        [](std::int64_t total, const MultiwaySidePot& pot) { return total + pot.amount; });
    if (contested_total > std::numeric_limits<int>::max()) {
        throw std::invalid_argument("multiway contested pot exceeds supported chip range");
    }
    result.rake_taken = input.rake_policy.rake_for_contested_pot(
        static_cast<int>(contested_total), input.flop_seen);
    auto remaining_rake = result.rake_taken;
    for (auto& pot : result.pots) {
        const auto taken = std::min(pot.amount, remaining_rake);
        pot.amount -= taken;
        remaining_rake -= taken;
        if (remaining_rake == 0) break;
    }
    if (remaining_rake != 0) {
        throw std::logic_error("multiway rake exceeds contested pot");
    }

    for (const auto& pot : result.pots) {
        if (pot.amount == 0) continue;
        Strength best = input.strengths[static_cast<std::size_t>(pot.eligible_players.front())];
        for (const auto player : pot.eligible_players) {
            best = std::max(best, input.strengths[static_cast<std::size_t>(player)]);
        }
        std::vector<PlayerId> winners;
        for (const auto player : pot.eligible_players) {
            if (input.strengths[static_cast<std::size_t>(player)] == best) winners.push_back(player);
        }
        const auto share = pot.amount / static_cast<int>(winners.size());
        auto remainder = pot.amount % static_cast<int>(winners.size());
        std::sort(winners.begin(), winners.end(), [&input, count](PlayerId lhs, PlayerId rhs) {
            const auto seat_count = static_cast<PlayerId>(count);
            const auto lhs_order = static_cast<std::size_t>(
                (lhs - input.odd_chip_first_seat + seat_count) % seat_count);
            const auto rhs_order = static_cast<std::size_t>(
                (rhs - input.odd_chip_first_seat + seat_count) % seat_count);
            return lhs_order < rhs_order;
        });
        for (const auto winner : winners) {
            result.payouts[static_cast<std::size_t>(winner)] += share;
            if (remainder > 0) {
                ++result.payouts[static_cast<std::size_t>(winner)];
                --remainder;
            }
        }
    }

    for (std::size_t seat = 0; seat < count; ++seat) {
        result.utilities[seat] = static_cast<Value>(
            result.payouts[seat] + result.refunds[seat] - input.contributions[seat]);
    }
    return result;
}

}  // namespace core
