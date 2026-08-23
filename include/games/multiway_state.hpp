#pragma once

#include "core/namespaces.hpp"
#include "core/poker.hpp"

#include "games/multiway_rake.hpp"

#include <cstdint>
#include <vector>

namespace texas::games::multiway {

struct MultiwayGameRules;

// This layer owns multiway betting progression only. Pot construction,
// private cards, and terminal utilities remain separate multiway subsystems.
enum class MultiwayAction : std::uint8_t {
    Fold,
    Check,
    Call,
    Bet,
    Raise,
    AllIn,
};

enum class MultiwayNextNodeKind : std::uint8_t {
    BettingDecision,
    StreetTransition,
    BoardRunout,
    FoldTerminal,
    ShowdownTerminal,
};

struct MultiwayGameConfig {
    // Each entry is the seat's total stack before initial_contributions.
    // This module supports 2 through 6 seats.
    std::vector<int> starting_stacks;
    std::vector<int> initial_contributions;
    std::vector<int> initial_street_contributions;
    PlayerId first_player = 0;
    int big_blind = 100;
    Street street = Street::Flop;
    MultiwayRakePolicy rake_policy = MultiwayRakePolicy::explicit_zero();

    void validate() const;
};

// Complete betting-round state required to begin a bounded live subgame
// without replaying hidden action history.  The caller supplies canonical
// action/abstraction metadata separately for infoset lookup.
struct MultiwayBettingSnapshot {
    std::vector<int> stacks;
    std::vector<int> contributions;
    std::vector<int> street_contributions;
    std::vector<bool> folded;
    std::vector<bool> all_in;
    std::vector<bool> may_raise;
    std::vector<bool> pending;
    std::vector<bool> has_acted;
    std::vector<int> bet_faced_when_acted;
    PlayerId current_player = -1;
    PlayerId last_aggressor = -1;
    int current_bet = 0;
    int last_full_raise_size = 0;
    int big_blind = 0;
    Street street = Street::Flop;

    void validate() const;
};

class MultiwayState {
public:
    static MultiwayState initial(const MultiwayGameConfig& config);
    // Constructs the standard preflop root for a validated rules profile,
    // including its forced blind posts.
    static MultiwayState initial(const MultiwayGameRules& rules, PlayerId first_player = 0);
    static MultiwayState from_snapshot(const MultiwayBettingSnapshot& snapshot);
    [[nodiscard]] MultiwayBettingSnapshot snapshot() const;

    const std::vector<int>& stacks() const noexcept { return stacks_; }
    const std::vector<int>& contributions() const noexcept { return contributions_; }
    const std::vector<int>& street_contributions() const noexcept { return street_contributions_; }
    const std::vector<bool>& folded() const noexcept { return folded_; }
    const std::vector<bool>& all_in() const noexcept { return all_in_; }
    const std::vector<bool>& may_raise() const noexcept { return may_raise_; }
    PlayerId current_player() const noexcept { return current_player_; }
    PlayerId last_aggressor() const noexcept { return last_aggressor_; }
    int current_bet() const noexcept { return current_bet_; }
    int last_full_raise_size() const noexcept { return last_full_raise_size_; }
    Street street() const noexcept { return street_; }

    bool is_hand_over() const noexcept;
    bool is_terminal() const noexcept;
    [[nodiscard]] MultiwayNextNodeKind next_node_kind() const noexcept;
    // Betting is closed but public cards must still run out before showdown.
    bool requires_board_runout() const noexcept;
    bool is_betting_round_complete() const noexcept;
    bool requires_street_transition() const noexcept;
    std::vector<MultiwayAction> legal_actions() const;

    // target_street_contribution is required for Bet and Raise and is the
    // acting seat's total contribution on this street after the action.
    MultiwayState apply(MultiwayAction action, int target_street_contribution = 0) const;
    MultiwayState begin_next_street(Street next_street, PlayerId first_player) const;

private:
    std::vector<int> stacks_;
    std::vector<int> contributions_;
    std::vector<int> street_contributions_;
    std::vector<bool> folded_;
    std::vector<bool> all_in_;
    std::vector<bool> may_raise_;
    std::vector<bool> pending_;
    std::vector<bool> has_acted_;
    std::vector<int> bet_faced_when_acted_;
    PlayerId current_player_ = -1;
    PlayerId last_aggressor_ = -1;
    int current_bet_ = 0;
    int last_full_raise_size_ = 0;
    int big_blind_ = 0;
    Street street_ = Street::Flop;

    bool is_actionable(PlayerId player) const noexcept;
    std::size_t live_player_count() const noexcept;
    std::size_t actionable_player_count() const noexcept;
    PlayerId next_pending_after(PlayerId player) const noexcept;
    void select_next_player();
    void refresh_round_completion();
    void reset_pending_after_full_raise(PlayerId aggressor);
    void refresh_raise_rights_after_short_raise(PlayerId aggressor);
};

}  // namespace texas::games::multiway
