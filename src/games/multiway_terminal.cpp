#include "games/multiway_terminal.hpp"

#include "games/multiway_fixed.hpp"
#include "games/multiway_rules.hpp"

#include <algorithm>
#include <cstdint>
#include <limits>
#include <numeric>
#include <stdexcept>

namespace texas::games::multiway {

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
    MultiwayFixedTerminalInput fixed_input;
    fixed_input.seat_count = static_cast<std::uint8_t>(input.contributions.size());
    fixed_input.odd_chip_first_seat = input.odd_chip_first_seat;
    fixed_input.rake_policy = input.rake_policy;
    fixed_input.flop_seen = input.flop_seen;
    for (std::size_t seat = 0; seat < input.contributions.size(); ++seat) {
        fixed_input.contributions[seat] = input.contributions[seat];
        fixed_input.folded[seat] = input.folded[seat];
        fixed_input.strengths[seat] = input.strengths[seat];
    }
    MultiwayFixedTerminalScratch scratch;
    MultiwayFixedTerminalResult fixed_result;
    settle_multiway_terminal_fixed(fixed_input, scratch, fixed_result);

    MultiwayTerminalResult result;
    result.refunds.assign(fixed_result.refunds.begin(), fixed_result.refunds.begin() + fixed_result.seat_count);
    result.payouts.assign(fixed_result.payouts.begin(), fixed_result.payouts.begin() + fixed_result.seat_count);
    result.utilities.assign(fixed_result.utilities.begin(), fixed_result.utilities.begin() + fixed_result.seat_count);
    result.rake_taken = fixed_result.rake_taken;
    for (std::size_t index = 0; index < fixed_result.pot_count; ++index) {
        const auto& fixed_pot = fixed_result.pots[index];
        MultiwaySidePot pot;
        pot.amount = fixed_pot.amount;
        pot.contribution_cap = fixed_pot.contribution_cap;
        pot.eligible_players.assign(
            fixed_pot.eligible_players.begin(),
            fixed_pot.eligible_players.begin() + fixed_pot.eligible_count);
        result.pots.push_back(std::move(pot));
    }
    return result;
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
    return settle_multiway_terminal(input);
}

MultiwayTerminalResult settle_multiway_terminal(
    const MultiwayTerminalInput& input,
    const MultiwayGameRules& rules) {
    rules.validate();
    if (input.contributions.size() != rules.player_count) {
        throw std::invalid_argument("multiway terminal input does not match rule seat count");
    }
    auto ruled_input = input;
    ruled_input.rake_policy = rules.rake_policy;
    return settle_multiway_terminal(ruled_input);
}

}  // namespace texas::games::multiway
