#pragma once

#include "core/game.hpp"

#include <array>
#include <cstdint>
#include <memory>
#include <optional>

namespace core {

inline constexpr ActionId LEDUC_FOLD = 0;
inline constexpr ActionId LEDUC_CALL = 1;
inline constexpr ActionId LEDUC_RAISE = 2;

class LeducState final : public Game {
public:
    std::vector<int> private_cards;
    std::optional<int> public_card;
    std::vector<ActionId> round1_history;
    std::vector<ActionId> round2_history;
    std::array<int, 2> ante;
    std::array<bool, 2> folded;
    std::uint8_t round_num;
    std::uint8_t num_raises;
    std::uint8_t num_calls;
    int stakes;
    PlayerId cur_player;

    LeducState();

    static LeducState initial();

    int pot() const;
    bool round_complete() const;
    std::string round_string(const std::vector<ActionId>& history) const;

    bool is_terminal() const override;
    std::vector<Value> utility() const override;
    PlayerId current_player() const override;
    std::vector<ChanceOutcome> chance_outcomes() const override;
    std::vector<ActionId> legal_actions() const override;
    LeducState next_state(ActionId action) const;
    std::string infoset_key(PlayerId player) const override;

    std::size_t num_players() const override;
    std::unique_ptr<Game> clone() const override;
    std::unique_ptr<Game> apply(ActionId action) const override;

private:
    LeducState apply_chance(ActionId action) const;
    LeducState apply_player(ActionId action) const;
    static PlayerId first_non_folded(const std::array<bool, 2>& folded);
    static PlayerId next_player(PlayerId player, const std::array<bool, 2>& folded);
};

}  // namespace core


