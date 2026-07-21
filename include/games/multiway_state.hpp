#pragma once

#include "games/hunl.hpp"

#include <cstdint>
#include <vector>

namespace core {

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

struct MultiwayGameConfig {
    // Each entry is the seat's total stack before initial_contributions.
    // This module supports 2 through 6 seats.
    std::vector<int> starting_stacks;
    std::vector<int> initial_contributions;
    std::vector<int> initial_street_contributions;
    PlayerId first_player = 0;
    int big_blind = 100;
    Street street = Street::Flop;

    void validate() const;
};

class MultiwayState {
public:
    static MultiwayState initial(const MultiwayGameConfig& config);

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
    bool is_terminal() const noexcept { return is_hand_over(); }
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
    PlayerId current_player_ = -1;
    PlayerId last_aggressor_ = -1;
    int current_bet_ = 0;
    int last_full_raise_size_ = 0;
    int big_blind_ = 0;
    Street street_ = Street::Flop;

    bool is_actionable(PlayerId player) const noexcept;
    std::size_t live_player_count() const noexcept;
    PlayerId next_pending_after(PlayerId player) const noexcept;
    void select_next_player();
    void refresh_round_completion();
    void reset_pending_after_full_raise(PlayerId aggressor);
};

}  // namespace core
