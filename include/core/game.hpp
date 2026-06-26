#pragma once

#include "core/types.hpp"

#include <memory>
#include <vector>

namespace core {

class Game {
public:
    virtual ~Game() = default;

    virtual std::size_t num_players() const = 0;
    virtual bool is_terminal() const = 0;
    virtual PlayerId current_player() const = 0;
    virtual std::vector<ActionId> legal_actions() const = 0;
    virtual std::vector<ChanceOutcome> chance_outcomes() const = 0;
    virtual std::unique_ptr<Game> clone() const = 0;
    virtual std::unique_ptr<Game> apply(ActionId action) const = 0;
    virtual std::string infoset_key(PlayerId player) const = 0;
    virtual std::vector<Value> utility() const = 0;
};

}  // namespace core


