#pragma once

#include "core/fingerprint.hpp"
#include "solver/multiway/engine/multiway_solver.hpp"
#include "games/multiway_fixed.hpp"
#include "solver/multiway/abstraction/multiway_action_abstraction.hpp"
#include "solver/multiway/abstraction/multiway_public_builder.hpp"
#include "util/tracy.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <limits>
#include <sstream>
#include <string>
#include <stdexcept>
#include <utility>

namespace texas::solver::multiway
{
	namespace
	{
#pragma pack(push, 8)
		struct PublicStateValidationMetadata
		{
			std::uint64_t canonical_menu_id = 0U;
			std::uint8_t action_count = 0U;
			std::uint8_t street = 0U;
			core::PlayerId current_player = -1;
			MultiwayNextNodeKind next_node_kind = MultiwayNextNodeKind::BettingDecision;
		};
#pragma pack(pop)

		[[maybe_unused]] bool valid_action(MultiwayAction action) noexcept
		{
			return action == MultiwayAction::Fold || action == MultiwayAction::Check || action == MultiwayAction::Call || action == MultiwayAction::Bet || action == MultiwayAction::Raise || action == MultiwayAction::AllIn;
		}

		[[maybe_unused]] bool valid_public_parent_edge_kind(MultiwayPublicParentEdgeKind kind) noexcept
		{
			return kind == MultiwayPublicParentEdgeKind::None || kind == MultiwayPublicParentEdgeKind::BettingAction || kind == MultiwayPublicParentEdgeKind::BoardChance || kind == MultiwayPublicParentEdgeKind::StreetTransition;
		}

		[[maybe_unused]] bool valid_odd_chip_rule(MultiwayOddChipRule rule) noexcept
		{
			return rule == MultiwayOddChipRule::AscendingSeatIdFromFirstSeat;
		}

		[[maybe_unused]] std::uint8_t expected_board_card_count(Street street)
		{
			switch (street)
			{
				case Street::Preflop:
					return 0;
				case Street::Flop:
					return 3;
				case Street::Turn:
					return 4;
				case Street::River:
					return 5;
				case Street::Showdown:
					break;
			}
			throw std::invalid_argument("multiway public state has an invalid board street");
		}

		[[maybe_unused]] bool same_card_set(const std::vector<std::uint8_t>& left, const std::vector<std::uint8_t>& right) noexcept
		{
			if (left.size() != right.size())
				return false;
			for (const auto card : left)
			{
				if (std::find(right.begin(), right.end(), card) == right.end())
					return false;
			}
			return true;
		}

		[[maybe_unused]] bool same_history_action(
			const MultiwayActionDescriptor& left,
			const MultiwayActionDescriptor& right) noexcept
		{
			return left.action == right.action && left.target_street_contribution == right.target_street_contribution && left.action_menu_id == right.action_menu_id;
		}

		[[maybe_unused]] bool board_contains(
			const std::vector<std::uint8_t>& complete,
			const std::vector<std::uint8_t>& subset) noexcept
		{
			return std::includes(complete.begin(), complete.end(), subset.begin(), subset.end());
		}

		[[maybe_unused]] bool matches_added_board_cards(
			const std::vector<std::uint8_t>& parent,
			const std::vector<std::uint8_t>& child,
			const std::vector<std::uint8_t>& dealt_cards) noexcept
		{
			if (child.size() <= parent.size() || dealt_cards.size() > 5U)
				return false;
			std::array<std::uint8_t, 5U> added = {};
			std::size_t count = 0U;
			for (const auto card : child)
			{
				if (!std::binary_search(parent.begin(), parent.end(), card))
					added[count++] = card;
			}
			if (child.size() != parent.size() + count || count != dealt_cards.size())
				return false;
			for (std::size_t index = 0U; index < count; ++index)
			{
				if (added[index] != dealt_cards[index])
					return false;
			}
			return true;
		}

		[[maybe_unused]] bool same_betting_snapshot(
			const MultiwayBettingSnapshot& left,
			const MultiwayBettingSnapshot& right) noexcept
		{
			return left.stacks == right.stacks && left.contributions == right.contributions && left.street_contributions == right.street_contributions && left.folded == right.folded && left.all_in == right.all_in && left.may_raise == right.may_raise && left.pending == right.pending && left.has_acted == right.has_acted && left.bet_faced_when_acted == right.bet_faced_when_acted && left.current_player == right.current_player && left.last_aggressor == right.last_aggressor && left.current_bet == right.current_bet && left.last_full_raise_size == right.last_full_raise_size && left.big_blind == right.big_blind && left.street == right.street;
		}

		[[maybe_unused]] bool same_fixed_betting_snapshot(
			const MultiwayFixedState& left,
			const MultiwayBettingSnapshot& right) noexcept
		{
			if (right.stacks.size() != left.seat_count || right.contributions.size() != left.seat_count || right.street_contributions.size() != left.seat_count || right.folded.size() != left.seat_count || right.all_in.size() != left.seat_count || right.may_raise.size() != left.seat_count || right.pending.size() != left.seat_count || right.has_acted.size() != left.seat_count || right.bet_faced_when_acted.size() != left.seat_count || right.current_player != left.current_player || right.last_aggressor != left.last_aggressor || right.current_bet != left.current_bet || right.last_full_raise_size != left.last_full_raise_size || right.big_blind != left.big_blind || right.street != left.street)
				return false;
			for (std::size_t seat = 0U; seat < left.seat_count; ++seat)
			{
				if (right.stacks[seat] != left.stacks[seat] || right.contributions[seat] != left.contributions[seat] || right.street_contributions[seat] != left.street_contributions[seat] || right.folded[seat] != left.folded[seat] || right.all_in[seat] != left.all_in[seat] || right.may_raise[seat] != left.may_raise[seat] || right.pending[seat] != left.pending[seat] || right.has_acted[seat] != left.has_acted[seat] || right.bet_faced_when_acted[seat] != left.bet_faced_when_acted[seat])
					return false;
			}
			return true;
		}

		[[maybe_unused]] bool same_public_state_descriptor(
			const MultiwayPublicStateDescriptor& left,
			const MultiwayPublicStateDescriptor& right) noexcept
		{
			return left.id == right.id && left.parent_id == right.parent_id && left.incoming_edge.kind == right.incoming_edge.kind && left.incoming_edge.action == right.incoming_edge.action && left.incoming_edge.dealt_card == right.incoming_edge.dealt_card && left.incoming_edge.dealt_cards == right.incoming_edge.dealt_cards && left.incoming_edge.transition_board == right.incoming_edge.transition_board && left.canonical_history_id == right.canonical_history_id && same_betting_snapshot(left.betting, right.betting) && left.board == right.board && left.board_runout == right.board_runout && left.history == right.history && left.legal_actions == right.legal_actions;
		}

		[[maybe_unused]] bool same_public_state_payload(
			const MultiwayPublicStateDescriptor& left,
			const MultiwayPublicStateDescriptor& right) noexcept
		{
			return left.id == right.id && left.canonical_history_id == right.canonical_history_id && same_betting_snapshot(left.betting, right.betting) && left.board == right.board && left.board_runout == right.board_runout && left.history == right.history && left.legal_actions == right.legal_actions;
		}

		[[maybe_unused]] bool equivalent_chance_transition_edge(
			const MultiwayPublicStateDescriptor& left,
			const MultiwayPublicStateDescriptor& right) noexcept
		{
			const auto chance_or_transition = [](MultiwayPublicParentEdgeKind kind) {
				return kind == MultiwayPublicParentEdgeKind::BoardChance || kind == MultiwayPublicParentEdgeKind::StreetTransition;
			};
			return chance_or_transition(left.incoming_edge.kind) && chance_or_transition(right.incoming_edge.kind);
		}

		[[maybe_unused]] const char* public_parent_edge_kind_name(MultiwayPublicParentEdgeKind kind) noexcept
		{
			switch (kind)
			{
				case MultiwayPublicParentEdgeKind::None:
					return "none";
				case MultiwayPublicParentEdgeKind::BettingAction:
					return "betting";
				case MultiwayPublicParentEdgeKind::BoardChance:
					return "board-chance";
				case MultiwayPublicParentEdgeKind::StreetTransition:
					return "street-transition";
			}
			return "invalid";
		}

		template <typename T>
		[[maybe_unused]] void append_bounded_values(std::ostringstream& output, const std::vector<T>& values)
		{
			constexpr std::size_t limit = 8U;
			output << '[';
			for (std::size_t index = 0U; index < values.size() && index < limit; ++index)
			{
				if (index != 0U)
					output << ',';
				output << +values[index];
			}
			if (values.size() > limit)
				output << ",...";
			output << ']';
		}

		[[maybe_unused]] void append_conflicting_public_state_descriptor(
			std::ostringstream& output,
			const char* label,
			const MultiwayPublicStateDescriptor& state)
		{
			output << ' ' << label
				   << "{id=" << state.id.value
				   << ",parent=" << state.parent_id.value
				   << ",history=" << state.canonical_history_id
				   << ",board=";
			append_bounded_values(output, state.board);
			output << ",runout={remaining=" << +state.board_runout.remaining_board_cards
				   << ",chance_only=" << state.board_runout.chance_only_runout
				   << "},betting={street=" << +static_cast<std::uint8_t>(state.betting.street)
				   << ",current=" << state.betting.current_player
				   << ",bet=" << state.betting.current_bet
				   << ",stacks=";
			append_bounded_values(output, state.betting.stacks);
			output << ",contributions=";
			append_bounded_values(output, state.betting.contributions);
			output << ",street_contributions=";
			append_bounded_values(output, state.betting.street_contributions);
			output << ",folded=";
			append_bounded_values(output, state.betting.folded);
			output << ",all_in=";
			append_bounded_values(output, state.betting.all_in);
			output << ",may_raise=";
			append_bounded_values(output, state.betting.may_raise);
			output << ",pending=";
			append_bounded_values(output, state.betting.pending);
			output << ",has_acted=";
			append_bounded_values(output, state.betting.has_acted);
			output << ",bet_faced=";
			append_bounded_values(output, state.betting.bet_faced_when_acted);
			output << "},menu=";
			for (std::size_t index = 0U; index < state.legal_actions.size() && index < 8U; ++index)
			{
				const auto& action = state.legal_actions[index];
				if (index != 0U)
					output << ',';
				output << '{' << action.action_index
					   << ':' << +static_cast<std::uint8_t>(action.action)
					   << ':' << action.target_street_contribution
					   << ':' << action.action_menu_id << '}';
			}
			if (state.legal_actions.size() > 8U)
				output << ",...";
			output << ",edge={kind=" << public_parent_edge_kind_name(state.incoming_edge.kind)
				   << ",action={" << +static_cast<std::uint8_t>(state.incoming_edge.action.action)
				   << ':' << state.incoming_edge.action.target_street_contribution
				   << ':' << state.incoming_edge.action.action_menu_id << "}"
				   << ",dealt_card=" << +state.incoming_edge.dealt_card
				   << ",cards=";
			append_bounded_values(output, state.incoming_edge.dealt_cards);
			output << ",transition_board=";
			append_bounded_values(output, state.incoming_edge.transition_board);
			output << "}}";
		}

		[[maybe_unused]] [[noreturn]] void throw_conflicting_public_state_descriptors(
			const MultiwayPublicStateDescriptor& existing,
			const MultiwayPublicStateDescriptor& candidate)
		{
			std::ostringstream output;
			output << "multiway public state id was admitted with conflicting data:";
			append_conflicting_public_state_descriptor(output, "existing", existing);
			append_conflicting_public_state_descriptor(output, "candidate", candidate);
			throw std::invalid_argument(output.str());
		}

		[[maybe_unused]] void validate_public_state_child_transition(
			const MultiwayRootSnapshot& root,
			const MultiwayPublicStateDescriptor& parent,
			const MultiwayPublicStateDescriptor& child,
			MultiwayAdmissionDiagnostics* diagnostics = nullptr,
			const MultiwayFixedState* prevalidated_parent_state = nullptr)
		{
			TEXASSOLVER_TRACY_ZONE_SCOPED("mw.coordinator.validate_parent_transition");
			const auto fixed_state_started = std::chrono::steady_clock::now();
			MultiwayFixedState reconstructed_parent_state;
			const auto* parent_state = prevalidated_parent_state;
			if (parent_state == nullptr)
			{
				reconstructed_parent_state = make_multiway_fixed_state(parent.betting);
				parent_state = &reconstructed_parent_state;
			}
			const auto fixed_state_nanoseconds = static_cast<std::uint64_t>(
				std::chrono::duration_cast<std::chrono::nanoseconds>(
					std::chrono::steady_clock::now() - fixed_state_started)
					.count());
			switch (child.incoming_edge.kind)
			{
				case MultiwayPublicParentEdgeKind::BettingAction:
				{
					TEXASSOLVER_TRACY_ZONE_SCOPED("mw.coordinator.validate_parent_transition.action");
					const auto edge_started = std::chrono::steady_clock::now();
					if (child.history.size() != parent.history.size() + 1U)
					{
						throw std::invalid_argument("multiway child action state does not preserve parent public data");
					}
					const auto history_prefix_elements = static_cast<std::uint64_t>(parent.history.size());
					if (!std::equal(parent.history.begin(), parent.history.end(), child.history.begin()) || child.canonical_history_id == parent.canonical_history_id || child.board != parent.board || child.board_runout.remaining_board_cards != parent.board_runout.remaining_board_cards)
					{
						throw std::invalid_argument("multiway child action state does not preserve parent public data");
					}
					const auto& appended = child.history.back();
					const auto parent_next_node_kind = parent_state->next_node_kind();
					const bool actor_matches = parent_next_node_kind == MultiwayNextNodeKind::BettingDecision && appended.actor == parent.betting.current_player;
					const bool action_matches = actor_matches && same_history_action(appended.action, child.incoming_edge.action);
					std::uint64_t menu_probes = 0U;
					bool menu_contains_action = false;
					if (action_matches)
					{
						const auto menu_it = std::find_if(parent.legal_actions.begin(), parent.legal_actions.end(), [&appended, &menu_probes](const MultiwayActionDescriptor& action) {
							++menu_probes;
							return same_history_action(action, appended.action);
						});
						menu_contains_action = menu_it != parent.legal_actions.end();
					}
					if (parent_next_node_kind != MultiwayNextNodeKind::BettingDecision || !actor_matches || !action_matches || !menu_contains_action)
					{
						throw std::invalid_argument("multiway child history action is not in the parent decision menu");
					}
					const auto expected_betting = parent_state->apply(
						appended.action.action, appended.action.target_street_contribution);
					if (!same_fixed_betting_snapshot(expected_betting, child.betting))
					{
						throw std::invalid_argument("multiway child betting snapshot does not match its parent action");
					}
					if (diagnostics != nullptr)
					{
						diagnostics->parent_transition_fixed_state_nanoseconds += fixed_state_nanoseconds;
						diagnostics->parent_transition_action_edge_nanoseconds += static_cast<std::uint64_t>(
							std::chrono::duration_cast<std::chrono::nanoseconds>(
								std::chrono::steady_clock::now() - edge_started)
								.count());
						++diagnostics->parent_transition_action_edges;
						diagnostics->parent_transition_action_history_prefix_elements += history_prefix_elements;
						diagnostics->parent_transition_action_menu_probes += menu_probes;
					}
					return;
				}
				case MultiwayPublicParentEdgeKind::BoardChance:
				{
					TEXASSOLVER_TRACY_ZONE_SCOPED("mw.coordinator.validate_parent_transition.board_chance");
					const auto edge_started = std::chrono::steady_clock::now();
					const auto parent_kind = parent_state->next_node_kind();
					if ((parent_kind != MultiwayNextNodeKind::BoardRunout && parent_kind != MultiwayNextNodeKind::StreetTransition) || child.history != parent.history || !same_betting_snapshot(child.betting, parent.betting) || child.board.size() <= parent.board.size() || !board_contains(child.board, parent.board) || !are_valid_and_distinct_cards(child.board.data(), child.board.size()))
					{
						throw std::invalid_argument("multiway child board chance does not match its parent state");
					}
					if (!matches_added_board_cards(parent.board, child.board, child.incoming_edge.dealt_cards) || child.incoming_edge.dealt_card != child.incoming_edge.dealt_cards.front() || !std::is_sorted(child.incoming_edge.dealt_cards.begin(), child.incoming_edge.dealt_cards.end()))
					{
						throw std::invalid_argument("multiway child board chance has an invalid dealt-card descriptor");
					}
					const auto chance_only = parent_kind == MultiwayNextNodeKind::BoardRunout;
					if (child.board_runout.remaining_board_cards != 5U - child.board.size() || child.board_runout.chance_only_runout != chance_only)
					{
						throw std::invalid_argument("multiway child board chance has inconsistent runout metadata");
					}
					if (diagnostics != nullptr)
					{
						diagnostics->parent_transition_fixed_state_nanoseconds += fixed_state_nanoseconds;
						diagnostics->parent_transition_board_chance_edge_nanoseconds += static_cast<std::uint64_t>(
							std::chrono::duration_cast<std::chrono::nanoseconds>(
								std::chrono::steady_clock::now() - edge_started)
								.count());
						++diagnostics->parent_transition_board_chance_edges;
					}
					return;
				}
				case MultiwayPublicParentEdgeKind::StreetTransition:
				{
					TEXASSOLVER_TRACY_ZONE_SCOPED("mw.coordinator.validate_parent_transition.street");
					const auto edge_started = std::chrono::steady_clock::now();
					if (parent_state->next_node_kind() != MultiwayNextNodeKind::StreetTransition || child.history != parent.history || child.board != child.incoming_edge.transition_board || child.board.size() != expected_board_card_count(static_cast<Street>(static_cast<std::uint8_t>(parent.betting.street) + 1U)) || child.board.size() < parent.board.size() || !board_contains(child.board, parent.board) || child.board_runout.remaining_board_cards != 5U - child.board.size() || child.board_runout.chance_only_runout)
					{
						throw std::invalid_argument("multiway child street transition has inconsistent public state");
					}
					const auto next_street = static_cast<Street>(static_cast<std::uint8_t>(parent.betting.street) + 1U);
					const auto expected_betting = parent_state->begin_next_street(
						next_street, root.next_street_first_seat);
					if (!same_fixed_betting_snapshot(expected_betting, child.betting))
					{
						throw std::invalid_argument("multiway child street transition does not match root seat semantics");
					}
					if (diagnostics != nullptr)
					{
						diagnostics->parent_transition_fixed_state_nanoseconds += fixed_state_nanoseconds;
						diagnostics->parent_transition_street_edge_nanoseconds += static_cast<std::uint64_t>(
							std::chrono::duration_cast<std::chrono::nanoseconds>(
								std::chrono::steady_clock::now() - edge_started)
								.count());
						++diagnostics->parent_transition_street_edges;
					}
					return;
				}
				case MultiwayPublicParentEdgeKind::None:
					break;
			}
			throw std::invalid_argument("multiway child public state requires a typed parent edge");
		}

		[[maybe_unused]] bool delta_less(const MultiwayWorkerDelta& left, const MultiwayWorkerDelta& right) noexcept
		{
			if (left.infoset.seat != right.infoset.seat)
				return left.infoset.seat < right.infoset.seat;
			if (left.infoset.public_state != right.infoset.public_state)
			{
				return left.infoset.public_state < right.infoset.public_state;
			}
			if (left.action != right.action)
				return left.action < right.action;
			if (left.bucket != right.bucket)
				return left.bucket < right.bucket;
			if (left.trajectory_id != right.trajectory_id)
				return left.trajectory_id < right.trajectory_id;
			std::uint64_t left_regret = 0U;
			std::uint64_t right_regret = 0U;
			std::uint64_t left_strategy_sum = 0U;
			std::uint64_t right_strategy_sum = 0U;
			std::memcpy(&left_regret, &left.regret, sizeof(left_regret));
			std::memcpy(&right_regret, &right.regret, sizeof(right_regret));
			if (left_regret != right_regret)
				return left_regret < right_regret;
			std::memcpy(&left_strategy_sum, &left.strategy_sum, sizeof(left_strategy_sum));
			std::memcpy(&right_strategy_sum, &right.strategy_sum, sizeof(right_strategy_sum));
			return left_strategy_sum < right_strategy_sum;
		}

		[[maybe_unused]] bool delta_is_finite(const MultiwayWorkerDelta& delta) noexcept
		{
			return std::isfinite(delta.regret) && std::isfinite(delta.strategy_sum);
		}

		[[maybe_unused]] void hash_u64(std::uint64_t value, std::uint64_t& hash) noexcept
		{
			texas::core::fingerprint::append_u64(hash, value);
		}

		[[maybe_unused]] std::uint64_t admission_batch_fingerprint(
			std::uint64_t batch_number,
			const std::vector<MultiwayPublicStateDescriptor>& states,
			const std::vector<std::uint8_t>& new_state_flags,
			const std::vector<MultiwaySparseRowShape>& rows) noexcept
		{
			std::uint64_t hash = 1469598103934665603ULL;
			hash_u64(batch_number, hash);
			hash_u64(states.size(), hash);
			for (std::size_t index = 0U; index < states.size(); ++index)
			{
				hash_u64(states[index].id.value, hash);
				hash_u64(states[index].parent_id.value, hash);
				hash_u64(new_state_flags[index], hash);
			}
			hash_u64(rows.size(), hash);
			for (const auto& row : rows)
			{
				hash_u64(row.infoset.public_state.value, hash);
				hash_u64(static_cast<std::uint64_t>(static_cast<std::int64_t>(row.infoset.seat)), hash);
				hash_u64(row.bucket_count, hash);
				hash_u64(row.action_count, hash);
			}
			return hash == 0U ? 1U : hash;
		}

		[[maybe_unused]] void validate_public_state_descriptor(
			const MultiwayPublicStateDescriptor& state,
			PublicStateValidationMetadata* metadata = nullptr,
			MultiwayAdmissionDiagnostics* diagnostics = nullptr,
			MultiwayFixedState* validated_fixed_state = nullptr)
		{
			TEXASSOLVER_TRACY_ZONE_SCOPED("mw.coordinator.validate_public_state");
			const auto betting_board_started = std::chrono::steady_clock::now();
			state.betting.validate();
			if (state.id.value == 0 || state.canonical_history_id == 0 || !valid_public_parent_edge_kind(state.incoming_edge.kind))
			{
				throw std::invalid_argument("multiway public state requires stable identities");
			}
			const auto known_board_cards = expected_board_card_count(state.betting.street);
			const auto betting_state = make_multiway_fixed_state(state.betting);
			if (validated_fixed_state != nullptr)
				*validated_fixed_state = betting_state;
			const auto next_node_kind = betting_state.next_node_kind();
			const auto valid_board_size = state.board_runout.chance_only_runout
				? state.board.size() >= known_board_cards && state.board.size() <= 5U
				: next_node_kind == MultiwayNextNodeKind::StreetTransition
				? state.board.size() >= known_board_cards && state.board.size() <= expected_board_card_count(static_cast<Street>(static_cast<std::uint8_t>(state.betting.street) + 1U))
				: state.board.size() == known_board_cards;
			if (!valid_board_size || !std::is_sorted(state.board.begin(), state.board.end()) || state.board_runout.remaining_board_cards != 5U - state.board.size() || !are_valid_and_distinct_cards(state.board.data(), state.board.size()))
			{
				throw std::invalid_argument("multiway public state has an inconsistent board/runout state");
			}

			if (state.board_runout.chance_only_runout != (next_node_kind == MultiwayNextNodeKind::BoardRunout))
			{
				throw std::invalid_argument("multiway public state has an inconsistent chance-only runout flag");
			}
			if (diagnostics != nullptr)
				diagnostics->public_state_betting_board_validation_nanoseconds += static_cast<std::uint64_t>(
					std::chrono::duration_cast<std::chrono::nanoseconds>(
						std::chrono::steady_clock::now() - betting_board_started)
						.count());

			const auto menu_validation_started = std::chrono::steady_clock::now();
			const auto available_actions = betting_state.legal_actions();
			std::array<bool, kMultiwayFixedMaxActions> covered_actions{};
			const auto expected_menu_id = MultiwayPublicBuilder::stable_action_menu_id(state.legal_actions);
			for (std::size_t index = 0; index < state.legal_actions.size(); ++index)
			{
				const auto& action = state.legal_actions[index];
				if (!valid_action(action.action) || action.action_index != index || action.action_menu_id == 0 || action.target_street_contribution < 0 || action.action_menu_id != expected_menu_id || (index != 0 && (static_cast<std::uint8_t>(action.action) < static_cast<std::uint8_t>(state.legal_actions[index - 1U].action) || (action.action == state.legal_actions[index - 1U].action && action.target_street_contribution <= state.legal_actions[index - 1U].target_street_contribution))))
				{
					throw std::invalid_argument("multiway public state action menu is not a stable descriptor sequence");
				}
				std::size_t available_index = available_actions.count;
				for (std::size_t candidate = 0U; candidate < available_actions.count; ++candidate)
				{
					if (available_actions.actions[candidate] == action.action)
					{
						available_index = candidate;
						break;
					}
				}
				if (available_index == available_actions.count)
				{
					throw std::invalid_argument("multiway public state action is unavailable in its betting snapshot");
				}
				covered_actions[available_index] = true;
				for (std::size_t prior = 0; prior < index; ++prior)
				{
					if (state.legal_actions[prior].action == action.action && state.legal_actions[prior].target_street_contribution == action.target_street_contribution)
					{
						throw std::invalid_argument("multiway public state action menu contains a duplicate target");
					}
				}
				try
				{
					const auto successor = betting_state.apply(action.action, action.target_street_contribution);
					const auto acting_seat = static_cast<std::size_t>(betting_state.current_player);
					if (successor.street_contributions[acting_seat] != action.target_street_contribution)
					{
						throw std::invalid_argument("action descriptor does not encode its exact resulting contribution");
					}
				}
				catch (const std::exception&)
				{
					throw std::invalid_argument("multiway public state action descriptor is not executable");
				}
			}
			for (std::size_t action_index = 0U; action_index < available_actions.count; ++action_index)
			{
				if (!covered_actions[action_index])
				{
					throw std::invalid_argument(
						"multiway public state action menu omits legal action kind " + std::to_string(static_cast<std::uint8_t>(available_actions.actions[action_index])) + " street " + std::to_string(static_cast<std::uint8_t>(state.betting.street)) + " current_bet " + std::to_string(state.betting.current_bet));
				}
			}
			if (diagnostics != nullptr)
				diagnostics->public_state_menu_validation_nanoseconds += static_cast<std::uint64_t>(
					std::chrono::duration_cast<std::chrono::nanoseconds>(
						std::chrono::steady_clock::now() - menu_validation_started)
						.count());

			const auto history_identity_started = std::chrono::steady_clock::now();
			MultiwayPublicIdentityInspection identity;
			if (!MultiwayPublicBuilder::inspect_schema_v2_identity(
					state.betting, state.board, state.history, state.legal_actions, identity))
			{
				throw std::invalid_argument("multiway public state history contains an invalid action descriptor");
			}
			if (state.canonical_history_id != identity.canonical_history_id || state.id.value != identity.public_state_id.value)
			{
				throw std::invalid_argument("multiway public state descriptor does not match its schema-v2 fingerprint");
			}
			if (metadata != nullptr)
			{
				const auto menu_key_started = std::chrono::steady_clock::now();
				metadata->canonical_menu_id = state.legal_actions.empty() ? 0U : state.legal_actions.front().action_menu_id;
				if (diagnostics != nullptr)
					diagnostics->catalog_menu_key_nanoseconds += static_cast<std::uint64_t>(
						std::chrono::duration_cast<std::chrono::nanoseconds>(
							std::chrono::steady_clock::now() - menu_key_started)
							.count());
				metadata->action_count = static_cast<std::uint8_t>(state.legal_actions.size());
				metadata->street = static_cast<std::uint8_t>(state.betting.street);
				metadata->current_player = state.betting.current_player;
				metadata->next_node_kind = next_node_kind;
			}
			if (diagnostics != nullptr)
				diagnostics->public_state_history_identity_validation_nanoseconds += static_cast<std::uint64_t>(
					std::chrono::duration_cast<std::chrono::nanoseconds>(
						std::chrono::steady_clock::now() - history_identity_started)
						.count());
		}

		[[maybe_unused]] std::size_t checked_value_count(const MultiwaySparseRowShape& shape)
		{
			if (shape.bucket_count == 0U || shape.action_count == 0U)
			{
				throw std::invalid_argument("multiway sparse row requires non-zero buckets and actions");
			}
			const auto actions = static_cast<std::size_t>(shape.action_count);
			if (static_cast<std::size_t>(shape.bucket_count) > std::numeric_limits<std::size_t>::max() / actions)
			{
				throw std::overflow_error("multiway sparse row value count overflows size_t");
			}
			return static_cast<std::size_t>(shape.bucket_count) * actions;
		}
	} // namespace
} // namespace texas::solver::multiway
