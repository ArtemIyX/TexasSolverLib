#pragma once

#include "core/game.hpp"

#include <array>
#include <memory>

namespace core {

inline constexpr ActionId PASS = 0;
inline constexpr ActionId BET = 1;

class KuhnState final : public Game {
public:
    std::array<int, 2> cards;
    std::vector<ActionId> history;
    std::uint8_t chance_phase;

    KuhnState();

    static KuhnState initial();

    std::string history_string() const;
    bool is_terminal() const override;
    std::vector<Value> utility() const override;
    PlayerId current_player() const override;
    std::vector<ChanceOutcome> chance_outcomes() const override;
    std::vector<ActionId> legal_actions() const override;
    KuhnState next_state(ActionId action) const;
    std::string infoset_key(PlayerId player) const override;

    std::size_t num_players() const override;
    std::unique_ptr<Game> clone() const override;
    std::unique_ptr<Game> apply(ActionId action) const override;
};

}  // namespace core
