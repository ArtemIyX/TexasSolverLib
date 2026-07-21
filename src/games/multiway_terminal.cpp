#include "games/multiway_terminal.hpp"

#include <algorithm>
#include <limits>
#include <numeric>
#include <stdexcept>

namespace core {

void MultiwayTerminalInput::validate() const {
    const auto count = contributions.size();
    if (count < 2U || count > 6U || folded.size() != count || strengths.size() != count) {
        throw std::invalid_argument("MultiwayTerminalInput requires equal two-through-six seat vectors");
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

    for (const auto& pot : result.pots) {
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
