#include "games/multiway_rules.hpp"

#include <stdexcept>
#include <vector>

namespace core {
namespace {

constexpr std::uint64_t kFnvOffset = 14695981039346656037ULL;
constexpr std::uint64_t kFnvPrime = 1099511628211ULL;

void append_u64(std::uint64_t& hash, std::uint64_t value) noexcept {
    for (std::uint8_t byte = 0; byte < 8U; ++byte) {
        hash ^= (value >> (byte * 8U)) & 0xffU;
        hash *= kFnvPrime;
    }
}

}  // namespace

void MultiwayGameRules::validate() const {
    if (profile_version == 0U || player_count < 2U || player_count > 6U ||
        initial_stack_chips <= 0 || small_blind_chips <= 0 || big_blind_chips <= 0 ||
        small_blind_chips > big_blind_chips || initial_stack_chips < big_blind_chips ||
        ante_chips != 0 || straddle_chips != 0 || rebuys_enabled) {
        throw std::invalid_argument("multiway game rules contain an unsupported profile");
    }
    rake_policy.validate();
}

std::uint64_t MultiwayGameRules::identity() const noexcept {
    auto hash = kFnvOffset;
    append_u64(hash, profile_version);
    append_u64(hash, player_count);
    append_u64(hash, static_cast<std::uint64_t>(initial_stack_chips));
    append_u64(hash, static_cast<std::uint64_t>(small_blind_chips));
    append_u64(hash, static_cast<std::uint64_t>(big_blind_chips));
    append_u64(hash, static_cast<std::uint64_t>(ante_chips));
    append_u64(hash, static_cast<std::uint64_t>(straddle_chips));
    append_u64(hash, rake_policy.identity());
    append_u64(hash, rebuys_enabled ? 1U : 0U);
    return hash == 0U ? 1U : hash;
}

MultiwayGameConfig MultiwayGameRules::make_initial_game_config(PlayerId first_player) const {
    validate();

    if (first_player < 0 || first_player >= static_cast<PlayerId>(player_count)) {
        throw std::invalid_argument("multiway rules first player is out of range");
    }

    MultiwayGameConfig config;
    config.starting_stacks.assign(player_count, initial_stack_chips);
    config.initial_contributions.assign(player_count, 0);
    config.initial_street_contributions.assign(player_count, 0);
    config.first_player = first_player;
    config.big_blind = big_blind_chips;
    config.street = Street::Preflop;
    config.rake_policy = rake_policy;
    // first_player is the first voluntary preflop actor. The big blind and
    // small blind are therefore immediately before it in the table ring.
    const auto small_blind_seat = static_cast<std::size_t>(
        (first_player + static_cast<PlayerId>(player_count) - 2) % player_count);
    const auto big_blind_seat = static_cast<std::size_t>(
        (first_player + static_cast<PlayerId>(player_count) - 1) % player_count);
    config.initial_contributions[small_blind_seat] = small_blind_chips;
    config.initial_contributions[big_blind_seat] = big_blind_chips;
    config.initial_street_contributions = config.initial_contributions;
    config.validate();
    return config;
}

}  // namespace core
