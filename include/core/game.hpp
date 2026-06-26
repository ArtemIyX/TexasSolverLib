#pragma once

#include "core/types.hpp"

#include <memory>
#include <vector>

namespace core {

/**
 * @brief Common game-tree interface used by DCFR and exploitability walks.
 */
class Game {
public:
    virtual ~Game() = default;

    /// @return Number of players in the game.
    virtual std::size_t num_players() const = 0;
    /// @return True when the state is terminal.
    virtual bool is_terminal() const = 0;
    /// @return Active player index, or -1 for chance.
    virtual PlayerId current_player() const = 0;
    /// @return Legal action IDs for the current node.
    virtual std::vector<ActionId> legal_actions() const = 0;
    /// @return Chance outcomes for the current node, if any.
    virtual std::vector<ChanceOutcome> chance_outcomes() const = 0;
    /// @return Heap-allocated copy of the current game state.
    virtual std::unique_ptr<Game> clone() const = 0;
    /// @return Successor state after applying an action.
    virtual std::unique_ptr<Game> apply(ActionId action) const = 0;
    /// @return Information-set key for the given player.
    virtual std::string infoset_key(PlayerId player) const = 0;
    /// @return Terminal utility vector.
    virtual std::vector<Value> utility() const = 0;
};

}  // namespace core


