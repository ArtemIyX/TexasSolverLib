#pragma once

#include "core/core.hpp"

#include <vector>

namespace core {

class Game {
public:
    virtual ~Game() = default;

    virtual bool is_terminal() const = 0;
    virtual PlayerId current_player() const = 0;
    virtual std::vector<ActionId> legal_actions() const = 0;
    virtual std::vector<ChanceOutcome> chance_outcomes() const = 0;
    virtual std::string infoset_key(PlayerId player) const = 0;
    virtual std::vector<Value> utility() const = 0;
};

}  // namespace core
