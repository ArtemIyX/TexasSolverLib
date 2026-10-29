#include "solver/multiway/engine/multiway_solver.hpp"
#include "solver/multiway/engine/multiway_memory.hpp"
#include "solver/multiway/engine/multiway_compact_storage.hpp"
#include "solver/multiway/engine/multiway_external_storage.hpp"
#include "solver/multiway/engine/multiway_traversal.hpp"
#include "solver/multiway/abstraction/multiway_public_builder.hpp"
#include "solver/multiway/abstraction/multiway_bucket_model.hpp"
#include "test_harness.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <limits>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace
{
	class ExternalCoordinatorDirectory
	{
	public:
		ExternalCoordinatorDirectory()
		{
			const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
			path_ = std::filesystem::temp_directory_path() / ("texassolver_external_coordinator_" + std::to_string(stamp));
			std::error_code error;
			owned_ = std::filesystem::create_directory(path_, error);
			if (error || !owned_)
				throw std::runtime_error("external coordinator test directory cannot be created");
		}

		~ExternalCoordinatorDirectory()
		{
			if (owned_)
			{
				std::error_code ignored;
				std::filesystem::remove_all(path_, ignored);
			}
		}

		[[nodiscard]] const std::filesystem::path& path() const noexcept { return path_; }

	private:
		std::filesystem::path path_;
		bool owned_ = false;
	};

	constexpr std::uint8_t card(std::uint8_t rank, std::uint8_t suit)
	{
		return texas::card_to_int(rank, suit);
	}

	texas::MultiwayPublicStateDescriptor root_public_state()
	{
		texas::MultiwayGameConfig game;
		game.starting_stacks = { 1000, 1000 };
		game.initial_contributions = { 0, 0 };
		game.initial_street_contributions = { 0, 0 };
		game.first_player = 0;
		const auto betting = texas::MultiwayState::initial(game).snapshot();

		const std::vector<texas::MultiwayActionDescriptor> actions = {
			{ texas::MultiwayAction::Check, 0, 0, 9001 },
			{ texas::MultiwayAction::Bet, 1, 100, 9001 },
			{ texas::MultiwayAction::AllIn, 2, 1000, 9001 },
		};
		return texas::MultiwayPublicBuilder::make_root(
			betting, { card(2, 0), card(7, 1), card(9, 2) }, actions);
	}

	std::vector<texas::MultiwayActionDescriptor> action_descriptors(const texas::MultiwayState& state)
	{
		std::vector<texas::MultiwayActionDescriptor> descriptors;
		const auto actions = state.legal_actions();
		descriptors.reserve(actions.size());
		for (std::size_t index = 0; index < actions.size(); ++index)
		{
			const auto action = actions[index];
			const auto seat = static_cast<std::size_t>(state.current_player());
			const auto current = state.street_contributions()[seat];
			const auto target = action == texas::MultiwayAction::Bet || action == texas::MultiwayAction::Raise
				? state.current_bet() + state.last_full_raise_size()
				: action == texas::MultiwayAction::AllIn
				? current + state.stacks()[seat]
				: action == texas::MultiwayAction::Call
				? std::min(state.current_bet(), current + state.stacks()[seat])
				: current;
			descriptors.push_back({ action, static_cast<std::uint32_t>(index), target, 0U });
		}
		return texas::MultiwayPublicBuilder::canonicalize_action_menu(state.snapshot(), std::move(descriptors));
	}

	texas::MultiwayPublicStateDescriptor action_child_public_state(
		const texas::MultiwayPublicStateDescriptor& parent,
		std::uint64_t,
		std::uint64_t,
		std::size_t action_index)
	{
		const auto action = parent.legal_actions.at(action_index);
		const auto state = texas::MultiwayState::from_snapshot(parent.betting)
							   .apply(action.action, action.target_street_contribution);
		return texas::MultiwayPublicBuilder::make_action_child(
			parent, action_index, action_descriptors(state));
	}

	texas::MultiwayPublicStateDescriptor checked_action_child_public_state()
	{
		return action_child_public_state(root_public_state(), 2, 102, 0);
	}

	const texas::MultiwayPublicBoardChanceEdge& board_edge_dealing(
		const std::vector<texas::MultiwayPublicBoardChanceEdge>& edges,
		std::uint8_t dealt_card)
	{
		const auto found = std::find_if(edges.begin(), edges.end(), [dealt_card](const auto& edge) {
			return edge.chance.dealt_cards.size() == 1U && edge.chance.dealt_cards.front() == dealt_card;
		});
		if (found == edges.end())
			throw std::logic_error("board chance edge is unavailable");
		return *found;
	}

	texas::MultiwayPrivateConfig root_ranges()
	{
		texas::MultiwayPrivateConfig ranges;
		ranges.board = { card(2, 0), card(7, 1), card(9, 2) };
		texas::MultiwayWeightedHole first;
		first.hole = { card(14, 0), card(13, 0) };
		first.weight = 1.0;
		texas::MultiwayWeightedHole second;
		second.hole = { card(12, 0), card(11, 0) };
		second.weight = 1.0;
		ranges.ranges = {
			{ first },
			{ second },
		};
		return ranges;
	}

	texas::MultiwayRootSnapshot valid_root()
	{
		texas::MultiwayRootSnapshot root;
		root.public_state = root_public_state();
		root.root_infoset = { root.public_state.id, 0 };
		root.root_bucket = 0;
		root.seat_order = { 0, 1 };
		root.odd_chip_first_seat = 0;
		root.private_ranges = root_ranges();
		root.action_abstraction_version = 7;
		root.leaf_model_version = 11;
		return root;
	}

	texas::MultiwaySolverLimits valid_limits()
	{
		texas::MultiwaySolverLimits limits;
		limits.worker_count = 2;
		limits.trajectories_per_batch = 8;
		limits.max_public_states = 6;
		limits.max_sparse_rows = 3;
		limits.max_sparse_values = 12;
		limits.max_worker_delta_entries = 4;
		return limits;
	}

	texas::MultiwaySolveRequest valid_request()
	{
		texas::MultiwayCFRConfig cfr;
		cfr.player_count = 2;
		return texas::MultiwaySolveRequest(valid_root(), cfr, valid_limits());
	}

	texas::MultiwayInfosetId root_infoset()
	{
		return { root_public_state().id, 0 };
	}

	texas::MultiwaySparseRowShape root_row(std::uint32_t buckets = 1, std::uint8_t actions = 3)
	{
		return { root_infoset(), buckets, actions };
	}

	void expect_public_state_descriptor_equal(
		const texas::MultiwayPublicStateDescriptor& expected,
		const texas::MultiwayPublicStateDescriptor& actual)
	{
		EXPECT_EQ(actual.id, expected.id);
		EXPECT_EQ(actual.parent_id, expected.parent_id);
		EXPECT_EQ(actual.incoming_edge.kind, expected.incoming_edge.kind);
		EXPECT_EQ(actual.incoming_edge.action, expected.incoming_edge.action);
		EXPECT_EQ(actual.incoming_edge.dealt_card, expected.incoming_edge.dealt_card);
		EXPECT_EQ(actual.incoming_edge.dealt_cards, expected.incoming_edge.dealt_cards);
		EXPECT_EQ(actual.incoming_edge.transition_board, expected.incoming_edge.transition_board);
		EXPECT_EQ(actual.canonical_history_id, expected.canonical_history_id);
		EXPECT_EQ(actual.betting.stacks, expected.betting.stacks);
		EXPECT_EQ(actual.betting.contributions, expected.betting.contributions);
		EXPECT_EQ(actual.betting.street_contributions, expected.betting.street_contributions);
		EXPECT_EQ(actual.betting.folded, expected.betting.folded);
		EXPECT_EQ(actual.betting.all_in, expected.betting.all_in);
		EXPECT_EQ(actual.betting.may_raise, expected.betting.may_raise);
		EXPECT_EQ(actual.betting.pending, expected.betting.pending);
		EXPECT_EQ(actual.betting.has_acted, expected.betting.has_acted);
		EXPECT_EQ(actual.betting.bet_faced_when_acted, expected.betting.bet_faced_when_acted);
		EXPECT_EQ(actual.betting.current_player, expected.betting.current_player);
		EXPECT_EQ(actual.betting.last_aggressor, expected.betting.last_aggressor);
		EXPECT_EQ(actual.betting.current_bet, expected.betting.current_bet);
		EXPECT_EQ(actual.betting.last_full_raise_size, expected.betting.last_full_raise_size);
		EXPECT_EQ(actual.betting.big_blind, expected.betting.big_blind);
		EXPECT_EQ(actual.betting.street, expected.betting.street);
		EXPECT_EQ(actual.board, expected.board);
		EXPECT_EQ(actual.board_runout, expected.board_runout);
		EXPECT_EQ(actual.history, expected.history);
		EXPECT_EQ(actual.legal_actions, expected.legal_actions);
	}

	texas::MultiwayWorkerDelta delta(
		std::uint8_t action,
		double regret,
		double strategy_sum,
		std::uint64_t trajectory_id = 0)
	{
		texas::MultiwayWorkerDelta result;
		result.infoset = root_infoset();
		result.action = action;
		result.regret = regret;
		result.strategy_sum = strategy_sum;
		result.trajectory_id = trajectory_id;
		return result;
	}

	void expect_checkpoints_equal(
		const texas::MultiwayCoordinatorCheckpoint& expected,
		const texas::MultiwayCoordinatorCheckpoint& actual)
	{
		EXPECT_EQ(actual.public_states.size(), expected.public_states.size());
		EXPECT_EQ(actual.storage.shapes.size(), expected.storage.shapes.size());
		for (std::size_t index = 0U; index < expected.storage.shapes.size(); ++index)
		{
			EXPECT_EQ(actual.storage.shapes[index].infoset, expected.storage.shapes[index].infoset);
			EXPECT_EQ(actual.storage.shapes[index].bucket_count, expected.storage.shapes[index].bucket_count);
			EXPECT_EQ(actual.storage.shapes[index].action_count, expected.storage.shapes[index].action_count);
		}
		EXPECT_EQ(actual.storage.regrets, expected.storage.regrets);
		EXPECT_EQ(actual.storage.strategy_sums, expected.storage.strategy_sums);
		EXPECT_EQ(actual.storage.compact_regrets, expected.storage.compact_regrets);
		EXPECT_EQ(actual.storage.compact_strategy_masses, expected.storage.compact_strategy_masses);
	}

	std::vector<texas::MultiwayWorkerDeltaStream> make_differential_delta_streams(std::size_t worker_count)
	{
		std::vector<texas::MultiwayWorkerDeltaStream> streams;
		streams.reserve(worker_count);
		for (std::size_t worker = 0U; worker < worker_count; ++worker)
			streams.emplace_back(worker, 8U);
		for (std::size_t worker = 0U; worker < worker_count; ++worker)
		{
			for (std::uint64_t entry = 0U; entry < 5U; ++entry)
			{
				const auto sequence = static_cast<std::uint64_t>(worker * 5U + entry);
				EXPECT_TRUE(streams[worker].try_append(delta(
					static_cast<std::uint8_t>((sequence * 7U + 1U) % 3U),
					static_cast<double>((sequence * 11U) % 17U) - 8.0,
					static_cast<double>((sequence * 13U) % 19U) + 0.25,
					sequence + 1U)));
			}
			streams[worker].sort_fixed_order();
		}
		return streams;
	}

} // namespace

using texas::solver::multiway::MultiwayDeltaMergePath;

TEST_CASE(multiway_admission_diagnostics_accumulates_variance_fields)
{
	texas::MultiwayAdmissionDiagnostics first;
	texas::MultiwayAdmissionDiagnostics second;
	const auto seed = [](texas::MultiwayAdmissionDiagnostics& diagnostics, std::uint64_t base) {
		diagnostics.discovery_public_state_merge_nanoseconds = base + 1U;
		diagnostics.discovery_row_merge_nanoseconds = base + 2U;
		diagnostics.discovery_public_state_source_elements = base + 3U;
		diagnostics.discovery_row_source_elements = base + 4U;
		diagnostics.discovery_public_state_duplicate_candidates = base + 5U;
		diagnostics.discovery_row_duplicate_candidates = base + 6U;
		diagnostics.discovery_single_stream_batches = base + 7U;
		diagnostics.discovery_one_stream_fast_path_uses = base + 8U;
		diagnostics.parent_transition_fixed_state_nanoseconds = base + 9U;
		diagnostics.parent_transition_action_edge_nanoseconds = base + 10U;
		diagnostics.parent_transition_board_chance_edge_nanoseconds = base + 11U;
		diagnostics.parent_transition_street_edge_nanoseconds = base + 12U;
		diagnostics.parent_transition_action_edges = base + 13U;
		diagnostics.parent_transition_board_chance_edges = base + 14U;
		diagnostics.parent_transition_street_edges = base + 15U;
		diagnostics.parent_transition_action_history_prefix_elements = base + 16U;
		diagnostics.parent_transition_action_menu_probes = base + 17U;
		diagnostics.discovery_public_descriptor_copy_count = base + 18U;
		diagnostics.discovery_public_descriptor_copy_bytes = base + 19U;
		diagnostics.discovery_nested_descriptor_copy_count = base + 20U;
		diagnostics.discovery_nested_descriptor_copy_bytes = base + 21U;
		diagnostics.discovery_output_capacity_reuse_events = base + 22U;
		diagnostics.discovery_output_capacity_reuse_bytes = base + 23U;
		diagnostics.catalog_output_capacity_reuse_events = base + 24U;
		diagnostics.catalog_output_capacity_reuse_bytes = base + 25U;
		diagnostics.catalog_public_descriptor_copy_count = base + 26U;
		diagnostics.catalog_public_descriptor_copy_bytes = base + 27U;
		diagnostics.catalog_nested_descriptor_copy_count = base + 28U;
		diagnostics.catalog_nested_descriptor_copy_bytes = base + 29U;
	};
	seed(first, 0U);
	seed(second, 100U);

	first.accumulate(second);

	EXPECT_EQ(first.discovery_public_state_merge_nanoseconds, 102U);
	EXPECT_EQ(first.discovery_row_merge_nanoseconds, 104U);
	EXPECT_EQ(first.discovery_public_state_source_elements, 106U);
	EXPECT_EQ(first.discovery_row_source_elements, 108U);
	EXPECT_EQ(first.discovery_public_state_duplicate_candidates, 110U);
	EXPECT_EQ(first.discovery_row_duplicate_candidates, 112U);
	EXPECT_EQ(first.discovery_single_stream_batches, 114U);
	EXPECT_EQ(first.discovery_one_stream_fast_path_uses, 116U);
	EXPECT_EQ(first.parent_transition_fixed_state_nanoseconds, 118U);
	EXPECT_EQ(first.parent_transition_action_edge_nanoseconds, 120U);
	EXPECT_EQ(first.parent_transition_board_chance_edge_nanoseconds, 122U);
	EXPECT_EQ(first.parent_transition_street_edge_nanoseconds, 124U);
	EXPECT_EQ(first.parent_transition_action_edges, 126U);
	EXPECT_EQ(first.parent_transition_board_chance_edges, 128U);
	EXPECT_EQ(first.parent_transition_street_edges, 130U);
	EXPECT_EQ(first.parent_transition_action_history_prefix_elements, 132U);
	EXPECT_EQ(first.parent_transition_action_menu_probes, 134U);
	EXPECT_EQ(first.discovery_public_descriptor_copy_count, 136U);
	EXPECT_EQ(first.discovery_public_descriptor_copy_bytes, 138U);
	EXPECT_EQ(first.discovery_nested_descriptor_copy_count, 140U);
	EXPECT_EQ(first.discovery_nested_descriptor_copy_bytes, 142U);
	EXPECT_EQ(first.discovery_output_capacity_reuse_events, 144U);
	EXPECT_EQ(first.discovery_output_capacity_reuse_bytes, 146U);
	EXPECT_EQ(first.catalog_output_capacity_reuse_events, 148U);
	EXPECT_EQ(first.catalog_output_capacity_reuse_bytes, 150U);
	EXPECT_EQ(first.catalog_public_descriptor_copy_count, 152U);
	EXPECT_EQ(first.catalog_public_descriptor_copy_bytes, 154U);
	EXPECT_EQ(first.catalog_nested_descriptor_copy_count, 156U);
	EXPECT_EQ(first.catalog_nested_descriptor_copy_bytes, 158U);
}

TEST_CASE(multiway_solver_request_copies_the_immutable_root_snapshot)
{
	auto root = valid_root();
	texas::MultiwayCFRConfig cfr;
	cfr.player_count = 2;
	const texas::MultiwaySolveRequest request(root, cfr, valid_limits());
	root.public_state.legal_actions[0].action_menu_id = 77;
	root.private_ranges.board[0] = card(3, 0);
	root.public_state.board_runout.remaining_board_cards = 1;
	root.next_street_first_seat = 1;

	EXPECT_EQ(request.root().public_state.legal_actions[0].action_menu_id, valid_root().action_menu_id());
	EXPECT_EQ(request.root().private_ranges.board[0], card(2, 0));
	EXPECT_EQ(request.root().public_state.board_runout.remaining_board_cards, 2U);
	EXPECT_EQ(request.root().next_street_first_seat, 0);
}

TEST_CASE(multiway_solver_request_rejects_wrong_seat_count_cfr)
{
	auto cfr = texas::MultiwayCFRConfig{};
	cfr.player_count = 2;
	cfr.player_count = 3;
	EXPECT_THROW(texas::MultiwaySolveRequest(valid_root(), cfr, valid_limits()), std::invalid_argument);
}

TEST_CASE(multiway_solver_root_rejects_inconsistent_board_or_nonacting_root_seat)
{
	auto root = valid_root();
	root.private_ranges.board[0] = card(3, 0);
	EXPECT_THROW(root.validate(), std::invalid_argument);

	root = valid_root();
	root.root_infoset.seat = 1;
	EXPECT_THROW(root.validate(), std::invalid_argument);
}

TEST_CASE(multiway_solver_root_accepts_a_complete_valid_public_contract)
{
	const auto root = valid_root();
	root.validate();

	const auto request = valid_request();
	EXPECT_EQ(request.root().public_state.board.size(), std::size_t{ 3 });
	EXPECT_EQ(request.root().public_state.board_runout.remaining_board_cards, 2U);
	EXPECT_TRUE(!request.root().public_state.board_runout.chance_only_runout);
	EXPECT_EQ(request.root().action_abstraction_identity().menu_id, valid_root().action_menu_id());
	EXPECT_EQ(request.root().action_abstraction_identity().version, 7U);
}

TEST_CASE(multiway_solver_root_rejects_inconsistent_board_runout_metadata)
{
	auto root = valid_root();
	root.public_state.board_runout.remaining_board_cards = 1;
	EXPECT_THROW(root.validate(), std::invalid_argument);

	root = valid_root();
	root.public_state.board_runout.chance_only_runout = true;
	EXPECT_THROW(root.validate(), std::invalid_argument);

	root = valid_root();
	root.public_state.board.pop_back();
	EXPECT_THROW(root.validate(), std::invalid_argument);
}

TEST_CASE(multiway_board_runout_state_compares_const_values)
{
	const texas::MultiwayBoardRunoutState turn{ 1U, false };
	const texas::MultiwayBoardRunoutState same_turn{ 1U, false };
	const texas::MultiwayBoardRunoutState all_in_turn{ 1U, true };
	EXPECT_TRUE(turn == same_turn);
	EXPECT_TRUE(!(turn != same_turn));
	EXPECT_TRUE(turn != all_in_turn);
}

TEST_CASE(multiway_action_descriptor_compares_const_values)
{
	const texas::MultiwayActionDescriptor call{ texas::MultiwayAction::Call, 1U, 2, 9001U };
	const texas::MultiwayActionDescriptor same_call{ texas::MultiwayAction::Call, 1U, 2, 9001U };
	const texas::MultiwayActionDescriptor raise{ texas::MultiwayAction::Raise, 1U, 4, 9001U };
	EXPECT_TRUE(call == same_call);
	EXPECT_TRUE(!(call != same_call));
	EXPECT_TRUE(call != raise);
}

TEST_CASE(multiway_solver_root_rejects_action_descriptors_not_legal_or_executable_for_snapshot)
{
	auto root = valid_root();
	root.public_state.legal_actions[0].action = texas::MultiwayAction::Fold;
	EXPECT_THROW(root.validate(), std::invalid_argument);

	root = valid_root();
	root.public_state.legal_actions[1].target_street_contribution = 99;
	EXPECT_THROW(root.validate(), std::invalid_argument);

	root = valid_root();
	root.public_state.legal_actions[2].action_index = 1;
	EXPECT_THROW(root.validate(), std::invalid_argument);
}

TEST_CASE(multiway_solver_action_descriptors_require_exact_resulting_contributions)
{
	auto root = valid_root();
	root.public_state.legal_actions[0].target_street_contribution = 1;
	texas::MultiwayCFRConfig cfr;
	cfr.player_count = 2;
	EXPECT_THROW(texas::MultiwaySolveRequest(root, cfr, valid_limits()), std::invalid_argument);

	root = valid_root();
	root.public_state.legal_actions[2].target_street_contribution = 999;
	EXPECT_THROW(texas::MultiwaySolveRequest(root, cfr, valid_limits()), std::invalid_argument);
}

TEST_CASE(multiway_solver_root_rejects_invalid_deterministic_next_street_and_odd_chip_metadata)
{
	auto root = valid_root();
	root.next_street_first_seat = -1;
	EXPECT_THROW(root.validate(), std::invalid_argument);

	root = valid_root();
	root.odd_chip_first_seat = 2;
	EXPECT_THROW(root.validate(), std::invalid_argument);

	root = valid_root();
	root.odd_chip_rule = static_cast<texas::MultiwayOddChipRule>(255);
	EXPECT_THROW(root.validate(), std::invalid_argument);

	root = valid_root();
	root.seat_order = { 0, 0 };
	EXPECT_THROW(root.validate(), std::invalid_argument);
}

TEST_CASE(multiway_solver_limits_require_all_bounded_capacities)
{
	auto limits = valid_limits();
	limits.max_worker_delta_entries = 0;
	EXPECT_THROW(limits.validate(), std::invalid_argument);
	limits = valid_limits();
	limits.max_sparse_rows = 0;
	EXPECT_THROW(limits.validate(), std::invalid_argument);
}

TEST_CASE(multiway_solver_sparse_admission_is_idempotent_and_action_major)
{
	texas::MultiwaySolverCoordinator coordinator(valid_request());
	coordinator.admit_infoset_row(root_row(2));
	coordinator.admit_infoset_row(root_row(2));

	EXPECT_EQ(coordinator.storage().row_count(), std::size_t{ 1 });
	EXPECT_EQ(coordinator.storage().value_count(), std::size_t{ 6 });
	const auto* metadata = coordinator.storage().metadata(root_infoset());
	EXPECT_TRUE(metadata != nullptr);
	EXPECT_EQ(metadata->regret_offset, std::size_t{ 0 });
	EXPECT_EQ(metadata->strategy_sum_offset, std::size_t{ 0 });
	EXPECT_EQ(coordinator.diagnostics().sparse_rows_admitted, 1U);

	auto bucket_one = delta(0, 0.0, 1.0);
	bucket_one.bucket = 1;
	auto bucket_zero = delta(1, 0.0, 2.0);
	bucket_zero.bucket = 0;
	texas::MultiwayWorkerDeltaStream first(0, 2);
	texas::MultiwayWorkerDeltaStream second(1, 2);
	first.try_append(bucket_one);
	second.try_append(bucket_zero);
	first.sort_fixed_order();
	second.sort_fixed_order();
	coordinator.merge_worker_streams({ first, second });
	const auto at_bucket_zero = coordinator.storage().average_strategy(root_infoset(), 0);
	const auto at_bucket_one = coordinator.storage().average_strategy(root_infoset(), 1);
	EXPECT_NEAR(at_bucket_zero[0], 0.0, 1e-12);
	EXPECT_NEAR(at_bucket_zero[1], 1.0, 1e-12);
	EXPECT_NEAR(at_bucket_one[0], 1.0, 1e-12);
	EXPECT_NEAR(at_bucket_one[1], 0.0, 1e-12);
}

TEST_CASE(multiway_solver_sparse_merge_rejects_lost_nonzero_float64_updates_transactionally)
{
	texas::MultiwaySolverCoordinator coordinator(valid_request());
	coordinator.admit_infoset_row(root_row());
	const auto large = std::ldexp(1.0, 53);

	texas::MultiwayWorkerDeltaStream first_large(0, 1);
	texas::MultiwayWorkerDeltaStream second_large(1, 1);
	EXPECT_TRUE(first_large.try_append(delta(0, large, large, 0U)));
	first_large.sort_fixed_order();
	second_large.sort_fixed_order();
	coordinator.merge_worker_streams({ first_large, second_large });

	texas::MultiwayWorkerDeltaStream first_lost(0, 1);
	texas::MultiwayWorkerDeltaStream second_lost(1, 1);
	EXPECT_TRUE(first_lost.try_append(delta(0, 1.0, 1.0, 1U)));
	first_lost.sort_fixed_order();
	second_lost.sort_fixed_order();
	EXPECT_THROW(coordinator.merge_worker_streams({ first_lost, second_lost }), std::overflow_error);
	const auto after_rejection = coordinator.storage().average_strategy(root_infoset(), 0U);
	EXPECT_NEAR(after_rejection[0], 1.0, 1e-12);

	texas::MultiwayWorkerDeltaStream first_representable(0, 1);
	texas::MultiwayWorkerDeltaStream second_representable(1, 1);
	EXPECT_TRUE(first_representable.try_append(delta(0, 2.0, 2.0, 2U)));
	first_representable.sort_fixed_order();
	second_representable.sort_fixed_order();
	coordinator.merge_worker_streams({ first_representable, second_representable });
	EXPECT_NEAR(coordinator.storage().average_strategy(root_infoset(), 0U)[0], 1.0, 1e-12);
}

TEST_CASE(multiway_solver_sparse_merge_grows_bounded_pending_cell_scratch)
{
	auto limits = valid_limits();
	limits.max_worker_delta_entries = 300U;
	texas::MultiwayCFRConfig cfr;
	cfr.player_count = 2U;
	texas::MultiwaySolverCoordinator coordinator(
		texas::MultiwaySolveRequest(valid_root(), cfr, limits));
	coordinator.admit_infoset_row(root_row());
	texas::MultiwayWorkerDeltaStream first(0U, 150U);
	texas::MultiwayWorkerDeltaStream second(1U, 150U);
	for (std::uint64_t trajectory = 0U; trajectory < 300U; ++trajectory)
	{
		auto update = delta(0U, 0.0, 1.0, trajectory);
		EXPECT_TRUE((trajectory % 2U == 0U ? first : second).try_append(update));
	}
	first.sort_fixed_order();
	second.sort_fixed_order();
	coordinator.merge_worker_streams({ first, second });
	EXPECT_NEAR(coordinator.storage().average_strategy(root_infoset(), 0U)[0], 1.0, 1e-12);
}

TEST_CASE(multiway_solver_sparse_admission_enforces_row_and_value_caps)
{
	auto limits = valid_limits();
	limits.max_sparse_rows = 1;
	limits.max_sparse_values = 3;
	texas::MultiwayCFRConfig cfr;
	cfr.player_count = 2;
	texas::MultiwaySolverCoordinator coordinator(
		texas::MultiwaySolveRequest(valid_root(), cfr, limits));
	coordinator.admit_infoset_row(root_row());

	auto second = root_row();
	second.infoset = { root_public_state().id, 1 };
	EXPECT_THROW(coordinator.admit_infoset_row(second), std::length_error);

	auto too_wide = root_row(2);
	texas::MultiwaySolverCoordinator values_limited(
		texas::MultiwaySolveRequest(valid_root(), cfr, limits));
	EXPECT_THROW(values_limited.admit_infoset_row(too_wide), std::length_error);
}

TEST_CASE(multiway_solver_root_bucket_must_fit_its_admitted_sparse_row)
{
	auto root = valid_root();
	root.root_bucket = 1;
	texas::MultiwayCFRConfig cfr;
	cfr.player_count = 2;
	texas::MultiwaySolverCoordinator coordinator(
		texas::MultiwaySolveRequest(root, cfr, valid_limits()));

	EXPECT_THROW(coordinator.admit_infoset_row(root_row(1)), std::invalid_argument);

	coordinator.admit_infoset_row(root_row(2));
	const auto policy = coordinator.export_root_policy();
	EXPECT_EQ(policy.bucket, 1U);
	EXPECT_EQ(policy.actions.size(), std::size_t{ 3 });
	for (const auto& action : policy.actions)
	{
		EXPECT_NEAR(action.probability, 1.0 / 3.0, 1e-12);
	}
}

TEST_CASE(multiway_solver_compact_backend_routes_admission_merge_and_export)
{
	auto limits = valid_limits();
	limits.storage_backend = texas::MultiwaySolverLimits::StorageBackend::CompactInt32;
	limits.max_sparse_values = 4096U;
	texas::MultiwayCFRConfig cfr;
	cfr.player_count = 2;
	texas::MultiwaySolverCoordinator coordinator(
		texas::MultiwaySolveRequest(valid_root(), cfr, limits));
	coordinator.admit_infoset_row(root_row());
	auto reference_limits = limits;
	reference_limits.storage_backend = texas::MultiwaySolverLimits::StorageBackend::Float64Reference;
	texas::MultiwaySolverCoordinator reference(
		texas::MultiwaySolveRequest(valid_root(), cfr, reference_limits));
	reference.admit_infoset_row(root_row());
	EXPECT_TRUE(coordinator.memory_snapshot().merge_scratch.retained_capacity_bytes < limits.max_sparse_values * texas::MultiwaySolverCoordinator::pending_merge_cell_size());
	texas::MultiwayWorkerDeltaStream first(0, 1U);
	texas::MultiwayWorkerDeltaStream second(1, 1U);
	EXPECT_TRUE(first.try_append(delta(0U, 1.0, 2.0)));
	first.sort_fixed_order();
	second.sort_fixed_order();
	coordinator.merge_worker_streams({ first, second });
	reference.merge_worker_streams({ first, second });
	EXPECT_TRUE(coordinator.compact_storage() != nullptr);
	EXPECT_NEAR(coordinator.export_root_policy().actions[0].probability, 1.0, 1e-12);
	const auto checkpoint = coordinator.checkpoint();
	const auto reference_checkpoint = reference.checkpoint();
	EXPECT_EQ(checkpoint.storage.shapes.size(), std::size_t{ 1 });
	EXPECT_TRUE(checkpoint.storage.regrets[0] > 0.0);
	EXPECT_EQ(checkpoint.storage.shapes.size(), reference_checkpoint.storage.shapes.size());
	EXPECT_EQ(checkpoint.storage.shapes[0].infoset, reference_checkpoint.storage.shapes[0].infoset);
	EXPECT_EQ(checkpoint.storage.regrets, reference_checkpoint.storage.regrets);
	EXPECT_EQ(checkpoint.storage.strategy_sums, reference_checkpoint.storage.strategy_sums);
}

TEST_CASE(multiway_solver_request_accepts_external_storage_configuration)
{
	auto limits = valid_limits();
	limits.storage_backend = texas::MultiwaySolverLimits::StorageBackend::ExternalCompactInt32;
	limits.external_storage.directory = "external_training_fixture";
	limits.external_storage.public_catalog_cache_bytes = 4096U;
	limits.external_storage.row_page_cache_bytes = 4096U;
	limits.external_storage.maximum_wal_bytes = 4096U;
	limits.external_storage.segment_target_bytes = 4096U;
	limits.external_storage.compaction_fan_in = 2U;
	limits.external_storage.compaction_scratch_bytes = 4096U;
	limits.external_storage.disk_reserve_bytes = 4096U;
	texas::MultiwayCFRConfig cfr;
	cfr.player_count = 2;
	texas::MultiwaySolveRequest request(valid_root(), cfr, limits);
	EXPECT_EQ(request.limits().storage_backend,
		texas::MultiwaySolverLimits::StorageBackend::ExternalCompactInt32);
}

TEST_CASE(multiway_solver_external_coordinator_admits_merges_and_rolls_back)
{
	using texas::MultiwayCFRConfig;
	using texas::MultiwaySolverCoordinator;
	using texas::MultiwaySolveRequest;
	using texas::MultiwaySolverLimits;
	using texas::MultiwayWorkerDeltaStream;
	ExternalCoordinatorDirectory directory;
	auto limits = valid_limits();
	limits.storage_backend = MultiwaySolverLimits::StorageBackend::ExternalCompactInt32;
	limits.external_storage.directory = directory.path();
	limits.external_storage.public_catalog_cache_bytes = 65536U;
	limits.external_storage.row_page_cache_bytes = 65536U;
	limits.external_storage.maximum_wal_bytes = 65536U;
	limits.external_storage.segment_target_bytes = 65536U;
	limits.external_storage.compaction_fan_in = 2U;
	limits.external_storage.compaction_scratch_bytes = 65536U;
	limits.external_storage.disk_reserve_bytes = 1024U * 1024U;
	limits.external_storage.synchronous_durability = false;
	MultiwayCFRConfig cfr;
	cfr.player_count = 2;
	MultiwaySolverCoordinator coordinator(MultiwaySolveRequest(valid_root(), cfr, limits));
	auto reference_limits = valid_limits();
	reference_limits.storage_backend = MultiwaySolverLimits::StorageBackend::CompactInt32;
	reference_limits.compact_page_width = limits.compact_page_width;
	MultiwaySolverCoordinator reference(MultiwaySolveRequest(valid_root(), cfr, reference_limits));
	coordinator.admit_infoset_row(root_row());
	reference.admit_infoset_row(root_row());
	EXPECT_TRUE(coordinator.has_infoset_row(root_infoset()));
	coordinator.admit_infoset_row(root_row());
	std::array<double, 3U> strategy{};
	coordinator.regret_matched_strategy_into(root_infoset(), 0U, strategy.data(), strategy.size());
	EXPECT_EQ(strategy, (std::array<double, 3U>{ 1.0 / 3.0, 1.0 / 3.0, 1.0 / 3.0 }));

	MultiwayWorkerDeltaStream first(0U, 2U);
	MultiwayWorkerDeltaStream second(1U, 2U);
	EXPECT_TRUE(first.try_append(delta(0U, 1.0, 1.0, 0U)));
	EXPECT_TRUE(second.try_append(delta(1U, 2.0, 2.0, 1U)));
	first.sort_fixed_order();
	second.sort_fixed_order();
	reference.merge_worker_streams({ first, second });
	coordinator.merge_worker_streams({ first, second });
	coordinator.regret_matched_strategy_into(root_infoset(), 0U, strategy.data(), strategy.size());
	std::array<double, 3U> reference_strategy{};
	reference.regret_matched_strategy_into(root_infoset(), 0U, reference_strategy.data(), reference_strategy.size());
	EXPECT_EQ(strategy, reference_strategy);
	EXPECT_NEAR(strategy[0], 1.0 / 3.0, 0.0);
	EXPECT_NEAR(strategy[1], 2.0 / 3.0, 0.0);
	EXPECT_EQ(strategy[2], 0.0);

	MultiwayWorkerDeltaStream valid(0U, 1U);
	MultiwayWorkerDeltaStream invalid(1U, 1U);
	EXPECT_TRUE(valid.try_append(delta(0U, 3.0, 3.0, 2U)));
	EXPECT_TRUE(invalid.try_append(delta(3U, 4.0, 4.0, 3U)));
	valid.sort_fixed_order();
	invalid.sort_fixed_order();
	EXPECT_THROW(coordinator.merge_worker_streams({ valid, invalid }), std::invalid_argument);
	coordinator.regret_matched_strategy_into(root_infoset(), 0U, strategy.data(), strategy.size());
	EXPECT_NEAR(strategy[0], 1.0 / 3.0, 0.0);
	EXPECT_NEAR(strategy[1], 2.0 / 3.0, 0.0);
	EXPECT_EQ(strategy[2], 0.0);
}

TEST_CASE(multiway_solver_external_coordinator_reopens_committed_rows_and_states)
{
	using texas::MultiwayCFRConfig;
	using texas::MultiwaySolverCoordinator;
	using texas::MultiwaySolveRequest;
	using texas::MultiwaySolverLimits;
	using texas::MultiwayWorkerDeltaStream;
	ExternalCoordinatorDirectory directory;
	auto limits = valid_limits();
	limits.storage_backend = MultiwaySolverLimits::StorageBackend::ExternalCompactInt32;
	limits.external_storage.directory = directory.path();
	limits.external_storage.public_catalog_cache_bytes = 65536U;
	limits.external_storage.row_page_cache_bytes = 65536U;
	limits.external_storage.maximum_wal_bytes = 65536U;
	limits.external_storage.segment_target_bytes = 65536U;
	limits.external_storage.compaction_fan_in = 2U;
	limits.external_storage.compaction_scratch_bytes = 65536U;
	limits.external_storage.disk_reserve_bytes = 1024U * 1024U;
	limits.external_storage.synchronous_durability = false;
	MultiwayCFRConfig cfr;
	cfr.player_count = 2U;
	const auto root = valid_root();
	const auto child = checked_action_child_public_state();
	const texas::MultiwaySparseRowShape child_row = {
		{ child.id, child.betting.current_player }, 2U,
		static_cast<std::uint8_t>(child.legal_actions.size())
	};
	{
		MultiwaySolverCoordinator coordinator(MultiwaySolveRequest(root, cfr, limits));
		coordinator.admit_public_state(child);
		coordinator.admit_infoset_row(root_row());
		coordinator.admit_infoset_row(child_row);
		MultiwayWorkerDeltaStream stream(0U, 2U);
		MultiwayWorkerDeltaStream empty_stream(1U, 2U);
		EXPECT_TRUE(stream.try_append(delta(0U, 1.0, 2.0, 0U)));
		EXPECT_TRUE(stream.try_append({ child_row.infoset, 1U, 0U, 2.0, 3.0, 1U }));
		stream.sort_fixed_order();
		empty_stream.sort_fixed_order();
		coordinator.merge_worker_streams({ stream, empty_stream });
	}
	{
		MultiwaySolverCoordinator reopened(MultiwaySolveRequest(root, cfr, limits));
		EXPECT_TRUE(reopened.has_public_state(child.id));
		EXPECT_TRUE(reopened.has_infoset_row(root_infoset()));
		EXPECT_TRUE(reopened.has_infoset_row(child_row.infoset));
		const auto* reconstructed = reopened.find_public_state(child.id);
		EXPECT_TRUE(reconstructed != nullptr);
		EXPECT_EQ(reconstructed->history, child.history);
		std::array<double, 3U> root_strategy{};
		reopened.regret_matched_strategy_into(root_infoset(), 0U,
			root_strategy.data(), root_strategy.size());
		EXPECT_EQ(root_strategy, (std::array<double, 3U>{ 1.0, 0.0, 0.0 }));
		std::vector<double> child_strategy(child.legal_actions.size());
		reopened.regret_matched_strategy_into(child_row.infoset, 1U,
			child_strategy.data(), child_strategy.size());
		EXPECT_EQ(child_strategy[0], 1.0);
		for (std::size_t action = 1U; action < child_strategy.size(); ++action)
			EXPECT_EQ(child_strategy[action], 0.0);
	}
}

TEST_CASE(multiway_solver_external_merge_batches_multiple_pages_deterministically)
{
	using namespace texas;
	ExternalCoordinatorDirectory directory;
	auto limits = valid_limits();
	limits.storage_backend = MultiwaySolverLimits::StorageBackend::ExternalCompactInt32;
	limits.compact_page_width = 1U;
	limits.external_storage.directory = directory.path();
	limits.external_storage.public_catalog_cache_bytes = 65536U;
	limits.external_storage.row_page_cache_bytes = 65536U;
	limits.external_storage.maximum_wal_bytes = 65536U;
	limits.external_storage.segment_target_bytes = 65536U;
	limits.external_storage.compaction_fan_in = 2U;
	limits.external_storage.compaction_scratch_bytes = 65536U;
	limits.external_storage.disk_reserve_bytes = 1024U * 1024U;
	limits.external_storage.synchronous_durability = false;
	MultiwayCFRConfig cfr;
	cfr.player_count = 2U;
	MultiwaySolverCoordinator external(MultiwaySolveRequest(valid_root(), cfr, limits));
	auto reference_limits = valid_limits();
	reference_limits.storage_backend = MultiwaySolverLimits::StorageBackend::CompactInt32;
	reference_limits.compact_page_width = 1U;
	MultiwaySolverCoordinator reference(MultiwaySolveRequest(valid_root(), cfr, reference_limits));
	external.admit_infoset_row(root_row(4U));
	reference.admit_infoset_row(root_row(4U));
	MultiwayWorkerDeltaStream updates(0U, 4U), other(1U, 4U);
	for (std::uint32_t bucket = 0U; bucket < 4U; ++bucket)
	{
		EXPECT_TRUE(updates.try_append({ root_infoset(), bucket, 0U, 1.0 + bucket, 1.0, bucket }));
		EXPECT_TRUE(other.try_append({ root_infoset(), bucket, 1U, 1.0, 2.0, bucket }));
	}
	updates.sort_fixed_order();
	other.sort_fixed_order();
	reference.merge_worker_streams({ updates, other });
	external.merge_worker_streams({ updates, other });
	{
		auto lease = external.acquire_export_lease();
		const auto stats = lease.external_storage()->telemetry(false).pages;
		EXPECT_EQ(stats.publications, 1U);
		EXPECT_EQ(stats.published_records, 4U);
		EXPECT_EQ(stats.run_count, 1U);
	}
	for (std::uint32_t bucket = 0U; bucket < 4U; ++bucket)
	{
		std::array<double, 3U> expected{}, actual{};
		reference.regret_matched_strategy_into(root_infoset(), bucket, expected.data(), expected.size());
		external.regret_matched_strategy_into(root_infoset(), bucket, actual.data(), actual.size());
		EXPECT_EQ(actual, expected);
		EXPECT_NEAR(actual[0], static_cast<double>(bucket + 1U) / (bucket + 2U), 1e-12);
	}
}

TEST_CASE(multiway_solver_external_bounded_page_merge_rolls_back_flushed_subbatches)
{
	using texas::MultiwayCFRConfig;
	using texas::MultiwaySolverCoordinator;
	using texas::MultiwaySolveRequest;
	using texas::MultiwaySolverLimits;
	using texas::MultiwayWorkerDeltaStream;
	ExternalCoordinatorDirectory directory;
	auto limits = valid_limits();
	limits.storage_backend = MultiwaySolverLimits::StorageBackend::ExternalCompactInt32;
	limits.compact_page_width = 1U;
	limits.external_storage.directory = directory.path();
	limits.external_storage.public_catalog_cache_bytes = 65536U;
	// The 3 KB budget holds at most two pages; the root plus four child pages force a flush.
	limits.external_storage.row_page_cache_bytes = 3000U;
	limits.max_sparse_values = 16U;
	limits.external_storage.maximum_wal_bytes = 65536U;
	limits.external_storage.segment_target_bytes = 65536U;
	limits.external_storage.compaction_fan_in = 2U;
	limits.external_storage.compaction_scratch_bytes = 65536U;
	limits.external_storage.disk_reserve_bytes = 1024U * 1024U;
	limits.external_storage.synchronous_durability = false;
	MultiwayCFRConfig cfr;
	cfr.player_count = 2U;
	const auto child = checked_action_child_public_state();
	const texas::MultiwaySparseRowShape child_row = {
		{ child.id, child.betting.current_player }, 4U,
		static_cast<std::uint8_t>(child.legal_actions.size())
	};
	MultiwaySolverCoordinator coordinator(MultiwaySolveRequest(valid_root(), cfr, limits));
	coordinator.admit_public_state(child);
	coordinator.admit_infoset_row(root_row());
	coordinator.admit_infoset_row(child_row);
	// Establish a committed nonempty generation so a later aborted batch proves
	// that aggregate rollback restores real page data, not only an empty store.
	MultiwayWorkerDeltaStream seed(0U, 2U);
	MultiwayWorkerDeltaStream seed_empty(1U, 2U);
	EXPECT_TRUE(seed.try_append(delta(0U, 1.0, 1.0, 0U)));
	EXPECT_TRUE(seed.try_append({ child_row.infoset, 0U, 0U, 1.0, 1.0, 1U }));
	seed.sort_fixed_order();
	seed_empty.sort_fixed_order();
	coordinator.merge_worker_streams({ seed, seed_empty });
	std::uint64_t page_publications = 0U;
	coordinator.set_external_publication_observer([](void* context, std::uint32_t component,
													  std::uint64_t, std::uint64_t, std::uint64_t) {
		if (component == 2U)
			++*static_cast<std::uint64_t*>(context);
	},
		&page_publications);
	MultiwayWorkerDeltaStream valid(0U, 3U);
	MultiwayWorkerDeltaStream later(1U, 3U);
	EXPECT_TRUE(valid.try_append(delta(1U, 1.0, 1.0, 2U)));
	EXPECT_TRUE(valid.try_append({ child_row.infoset, 0U, 0U, 1.0, 1.0, 3U }));
	EXPECT_TRUE(valid.try_append({ child_row.infoset, 0U, 1U, 1.0, 1.0, 4U }));
	EXPECT_TRUE(later.try_append({ child_row.infoset, 0U, 2U, 1.0, 1.0, 5U }));
	EXPECT_TRUE(later.try_append({ child_row.infoset, 0U, 3U, 1.0, 1.0, 6U }));
	// Action 255 sorts after the valid action-0 pages, so rejection follows the forced flush.
	EXPECT_TRUE(later.try_append({ child_row.infoset, 1U, 255U, 1.0, 1.0, 2U }));
	valid.sort_fixed_order();
	later.sort_fixed_order();
	EXPECT_THROW(coordinator.merge_worker_streams({ valid, later }), std::invalid_argument);
	EXPECT_TRUE(page_publications > 0U);
	std::array<double, 3U> root_strategy{};
	coordinator.regret_matched_strategy_into(root_infoset(), 0U,
		root_strategy.data(), root_strategy.size());
	EXPECT_EQ(root_strategy, (std::array<double, 3U>{ 1.0, 0.0, 0.0 }));
	std::vector<double> child_strategy(child.legal_actions.size());
	coordinator.regret_matched_strategy_into(child_row.infoset, 0U,
		child_strategy.data(), child_strategy.size());
	EXPECT_EQ(child_strategy[0], 1.0);
	for (std::size_t action = 1U; action < child_strategy.size(); ++action)
		EXPECT_EQ(child_strategy[action], 0.0);
}

TEST_CASE(multiway_public_catalog_packs_external_node_without_catalog_state)
{
	const auto state = valid_root().public_state;
	texas::MultiwayPublicCatalog catalog(1U);
	catalog.admit(state);
	const auto* in_memory_node = catalog.checkpoint_node_at(0U);
	EXPECT_TRUE(in_memory_node != nullptr);
	if (in_memory_node == nullptr)
		return;
	const auto external_node = texas::MultiwayPublicCatalog::pack_node(
		state, in_memory_node->legal_menu_handle);
	std::vector<std::uint8_t> expected_bytes;
	std::vector<std::uint8_t> external_bytes;
	texas::encode_external_public_node(*in_memory_node, expected_bytes);
	texas::encode_external_public_node(external_node, external_bytes);
	EXPECT_EQ(external_bytes, expected_bytes);
}

TEST_CASE(multiway_solver_compact_checkpoint_matches_float64_for_multiple_canonical_rows)
{
	const auto child = checked_action_child_public_state();
	auto compact_limits = valid_limits();
	compact_limits.storage_backend = texas::MultiwaySolverLimits::StorageBackend::CompactInt32;
	compact_limits.compact_page_width = 1U;
	auto reference_limits = compact_limits;
	reference_limits.storage_backend = texas::MultiwaySolverLimits::StorageBackend::Float64Reference;
	texas::MultiwayCFRConfig cfr;
	cfr.player_count = 2;
	texas::MultiwaySolverCoordinator compact(
		texas::MultiwaySolveRequest(valid_root(), cfr, compact_limits));
	texas::MultiwaySolverCoordinator reference(
		texas::MultiwaySolveRequest(valid_root(), cfr, reference_limits));
	ExternalCoordinatorDirectory external_directory;
	auto external_limits = valid_limits();
	external_limits.storage_backend = texas::MultiwaySolverLimits::StorageBackend::ExternalCompactInt32;
	external_limits.compact_page_width = 1U;
	external_limits.external_storage.directory = external_directory.path();
	external_limits.external_storage.public_catalog_cache_bytes = 65536U;
	external_limits.external_storage.row_page_cache_bytes = 3000U;
	external_limits.external_storage.maximum_wal_bytes = 65536U;
	external_limits.external_storage.segment_target_bytes = 65536U;
	external_limits.external_storage.compaction_fan_in = 2U;
	external_limits.external_storage.compaction_scratch_bytes = 65536U;
	external_limits.external_storage.disk_reserve_bytes = 1024U * 1024U;
	external_limits.external_storage.synchronous_durability = false;
	texas::MultiwaySolverCoordinator external(
		texas::MultiwaySolveRequest(valid_root(), cfr, external_limits));
	const texas::MultiwaySparseRowShape child_row = {
		{ child.id, child.betting.current_player }, 2U,
		static_cast<std::uint8_t>(child.legal_actions.size())
	};
	compact.admit_public_state(child);
	reference.admit_public_state(child);
	external.admit_public_state(child);
	compact.admit_infoset_row(root_row());
	reference.admit_infoset_row(root_row());
	external.admit_infoset_row(root_row());
	compact.admit_infoset_row(child_row);
	reference.admit_infoset_row(child_row);
	external.admit_infoset_row(child_row);
	const auto* external_child = external.find_public_state(child.id);
	EXPECT_TRUE(external_child != nullptr);
	EXPECT_EQ(external_child->id, child.id);
	EXPECT_EQ(external_child->history, child.history);

	texas::MultiwayWorkerDeltaStream first(0U, 2U);
	texas::MultiwayWorkerDeltaStream second(1U, 2U);
	EXPECT_TRUE(first.try_append(delta(0U, 1.0, 2.0, 0U)));
	EXPECT_TRUE(first.try_append({ { child.id, child.betting.current_player }, 1U, 0U, -0.5, 3.0, 1U }));
	first.sort_fixed_order();
	second.sort_fixed_order();
	compact.merge_worker_streams({ first, second });
	reference.merge_worker_streams({ first, second });
	external.merge_worker_streams({ first, second });

	const auto compact_checkpoint = compact.checkpoint();
	const auto reference_checkpoint = reference.checkpoint();
	EXPECT_EQ(compact_checkpoint.storage.shapes.size(), reference_checkpoint.storage.shapes.size());
	for (std::size_t index = 0U; index < reference_checkpoint.storage.shapes.size(); ++index)
	{
		EXPECT_EQ(compact_checkpoint.storage.shapes[index].infoset, reference_checkpoint.storage.shapes[index].infoset);
		EXPECT_EQ(compact_checkpoint.storage.shapes[index].bucket_count, reference_checkpoint.storage.shapes[index].bucket_count);
		EXPECT_EQ(compact_checkpoint.storage.shapes[index].action_count, reference_checkpoint.storage.shapes[index].action_count);
	}
	EXPECT_EQ(compact_checkpoint.storage.regrets, reference_checkpoint.storage.regrets);
	EXPECT_EQ(compact_checkpoint.storage.strategy_sums, reference_checkpoint.storage.strategy_sums);
	std::array<double, 3U> compact_strategy{};
	std::array<double, 3U> external_strategy{};
	compact.regret_matched_strategy_into(root_infoset(), 0U, compact_strategy.data(), compact_strategy.size());
	external.regret_matched_strategy_into(root_infoset(), 0U, external_strategy.data(), external_strategy.size());
	EXPECT_EQ(external_strategy, compact_strategy);
	std::vector<double> compact_child_strategy(child.legal_actions.size());
	std::vector<double> external_child_strategy(child.legal_actions.size());
	compact.regret_matched_strategy_into(child_row.infoset, 1U,
		compact_child_strategy.data(), compact_child_strategy.size());
	external.regret_matched_strategy_into(child_row.infoset, 1U,
		external_child_strategy.data(), external_child_strategy.size());
	EXPECT_EQ(external_child_strategy, compact_child_strategy);
	EXPECT_THROW(external.scale_regrets(0.0), std::invalid_argument);
	EXPECT_THROW(external.scale_and_prune_regrets(0.5, 0.1, 0.0), std::invalid_argument);
	compact.scale_regrets(0.5);
	external.scale_regrets(0.5);
	compact.regret_matched_strategy_into(root_infoset(), 0U, compact_strategy.data(), compact_strategy.size());
	external.regret_matched_strategy_into(root_infoset(), 0U, external_strategy.data(), external_strategy.size());
	EXPECT_EQ(external_strategy, compact_strategy);
	compact.regret_matched_strategy_into(child_row.infoset, 1U,
		compact_child_strategy.data(), compact_child_strategy.size());
	external.regret_matched_strategy_into(child_row.infoset, 1U,
		external_child_strategy.data(), external_child_strategy.size());
	EXPECT_EQ(external_child_strategy, compact_child_strategy);
	EXPECT_EQ(external.scale_and_prune_regrets(0.5, 0.0, 0.0),
		compact.scale_and_prune_regrets(0.5, 0.0, 0.0));
	compact.regret_matched_strategy_into(root_infoset(), 0U, compact_strategy.data(), compact_strategy.size());
	external.regret_matched_strategy_into(root_infoset(), 0U, external_strategy.data(), external_strategy.size());
	EXPECT_EQ(external_strategy, compact_strategy);
	compact.regret_matched_strategy_into(child_row.infoset, 1U,
		compact_child_strategy.data(), compact_child_strategy.size());
	external.regret_matched_strategy_into(child_row.infoset, 1U,
		external_child_strategy.data(), external_child_strategy.size());
	EXPECT_EQ(external_child_strategy, compact_child_strategy);
	EXPECT_THROW(external.prune_negative_regrets(0.3, 0.05), std::invalid_argument);
	EXPECT_EQ(external.prune_negative_regrets(0.0, 0.05),
		compact.prune_negative_regrets(0.0, 0.05));
	compact.regret_matched_strategy_into(root_infoset(), 0U, compact_strategy.data(), compact_strategy.size());
	external.regret_matched_strategy_into(root_infoset(), 0U, external_strategy.data(), external_strategy.size());
	EXPECT_EQ(external_strategy, compact_strategy);
	compact.regret_matched_strategy_into(child_row.infoset, 1U,
		compact_child_strategy.data(), compact_child_strategy.size());
	external.regret_matched_strategy_into(child_row.infoset, 1U,
		external_child_strategy.data(), external_child_strategy.size());
	EXPECT_EQ(external_child_strategy, compact_child_strategy);
	texas::MultiwayWorkerDeltaStream empty_first(0U, 1U);
	texas::MultiwayWorkerDeltaStream empty_second(1U, 1U);
	empty_first.sort_fixed_order();
	empty_second.sort_fixed_order();
	external.merge_worker_streams({ empty_first, empty_second });
	external.regret_matched_strategy_into(root_infoset(), 0U, external_strategy.data(), external_strategy.size());
	compact.regret_matched_strategy_into(root_infoset(), 0U, compact_strategy.data(), compact_strategy.size());
	EXPECT_EQ(external_strategy, compact_strategy);
}

TEST_CASE(multiway_solver_live_root_uses_distinct_exact_private_hand_rows)
{
	const auto first = std::array<std::uint8_t, 2>{ card(14, 0), card(13, 0) };
	const auto second = std::array<std::uint8_t, 2>{ card(12, 0), card(11, 0) };
	const auto first_id = texas::solver::multiway::MultiwayBucketTable::hole_index(first);
	const auto second_id = texas::solver::multiway::MultiwayBucketTable::hole_index(second);
	EXPECT_TRUE(first_id != second_id);
	EXPECT_TRUE(first_id < texas::solver::multiway::MULTIWAY_HOLE_COMBINATION_COUNT);
	EXPECT_TRUE(second_id < texas::solver::multiway::MULTIWAY_HOLE_COMBINATION_COUNT);

	auto root = valid_root();
	root.root_uses_exact_private_hand = true;
	root.root_bucket = static_cast<std::uint32_t>(first_id);
	texas::MultiwayCFRConfig cfr;
	cfr.player_count = 2;
	texas::MultiwaySolverCoordinator coordinator(
		texas::MultiwaySolveRequest(root, cfr, valid_limits()));
	coordinator.admit_infoset_row(root_row(
		static_cast<std::uint32_t>(texas::solver::multiway::MULTIWAY_HOLE_COMBINATION_COUNT)));
	EXPECT_EQ(coordinator.export_root_policy().bucket, static_cast<std::uint32_t>(first_id));

	root.root_bucket = static_cast<std::uint32_t>(second_id);
	texas::MultiwaySolverCoordinator second_coordinator(
		texas::MultiwaySolveRequest(root, cfr, valid_limits()));
	second_coordinator.admit_infoset_row(root_row(
		static_cast<std::uint32_t>(texas::solver::multiway::MULTIWAY_HOLE_COMBINATION_COUNT)));
	EXPECT_EQ(second_coordinator.export_root_policy().bucket, static_cast<std::uint32_t>(second_id));
}

TEST_CASE(multiway_solver_same_street_exact_descendants_cannot_raise_the_value_cap)
{
	auto root = valid_root();
	root.root_uses_exact_private_hand = true;
	auto limits = valid_limits();
	limits.max_sparse_values = 1U;
	limits.max_sparse_rows = 2U;
	texas::MultiwayCFRConfig cfr;
	cfr.player_count = 2;
	texas::MultiwaySolverCoordinator coordinator(
		texas::MultiwaySolveRequest(root, cfr, limits));
	const auto admitted_capacity = coordinator.storage().value_capacity_limit();
	coordinator.admit_infoset_row(root_row(
		static_cast<std::uint32_t>(texas::MULTIWAY_HOLE_COMBINATION_COUNT)));

	const auto child = checked_action_child_public_state();
	coordinator.admit_public_state(child);
	const texas::MultiwaySparseRowShape child_row = {
		{ child.id, child.betting.current_player },
		static_cast<std::uint32_t>(texas::MULTIWAY_HOLE_COMBINATION_COUNT),
		static_cast<std::uint8_t>(child.legal_actions.size()),
	};
	coordinator.admit_infoset_row(child_row);
	EXPECT_EQ(coordinator.storage().row_count(), std::size_t{ 2U });
	EXPECT_EQ(coordinator.storage().value_capacity_limit(), admitted_capacity);
}

TEST_CASE(multiway_solver_sparse_rows_require_matching_public_state_shapes)
{
	texas::MultiwaySolverCoordinator coordinator(valid_request());
	const auto root_shape = root_row();
	coordinator.admit_infoset_row(root_shape);
	coordinator.admit_infoset_row(root_shape);
	EXPECT_EQ(coordinator.storage().row_count(), std::size_t{ 1 });
	EXPECT_EQ(coordinator.diagnostics().sparse_rows_admitted, 1U);

	auto conflicting_bucket_shape = root_shape;
	conflicting_bucket_shape.bucket_count = 2;
	EXPECT_THROW(coordinator.admit_infoset_row(conflicting_bucket_shape), std::invalid_argument);

	auto conflicting_action_shape = root_shape;
	conflicting_action_shape.action_count = 2;
	EXPECT_THROW(coordinator.admit_infoset_row(conflicting_action_shape), std::invalid_argument);

	const texas::MultiwaySparseRowShape unknown_state_shape{ { { 99 }, 0 }, 1, 1 };
	EXPECT_THROW(coordinator.admit_infoset_row(unknown_state_shape), std::invalid_argument);

	auto child = checked_action_child_public_state();
	coordinator.admit_public_state(child);

	const texas::MultiwaySparseRowShape child_shape{ { child.id, 1 }, 1, 3 };
	EXPECT_EQ(child.betting.current_player, child_shape.infoset.seat);
	coordinator.admit_infoset_row(child_shape);
	EXPECT_TRUE(coordinator.storage().has_row(child_shape.infoset));

	const texas::MultiwaySparseRowShape nonacting_child_shape{ { child.id, 0 }, 1, 3 };
	EXPECT_THROW(coordinator.admit_infoset_row(nonacting_child_shape), std::invalid_argument);

	auto too_few_actions = child_shape;
	too_few_actions.action_count = 2;
	EXPECT_THROW(coordinator.admit_infoset_row(too_few_actions), std::invalid_argument);

	auto too_many_actions = child_shape;
	too_many_actions.action_count = 4;
	EXPECT_THROW(coordinator.admit_infoset_row(too_many_actions), std::invalid_argument);

	const auto all_in = action_child_public_state(root_public_state(), 3, 103, 2);
	coordinator.admit_public_state(all_in);
	const auto runout = action_child_public_state(all_in, 4, 104, 1);
	coordinator.admit_public_state(runout);
	const texas::MultiwaySparseRowShape nondecision_shape{ { runout.id, 0 }, 1, 0 };
	EXPECT_THROW(coordinator.admit_infoset_row(nondecision_shape), std::invalid_argument);
}

TEST_CASE(multiway_solver_public_state_admission_requires_parent_and_conflict_free_identity)
{
	texas::MultiwaySolverCoordinator coordinator(valid_request());
	auto child = checked_action_child_public_state();
	child.parent_id = { 99 };
	EXPECT_THROW(coordinator.admit_public_state(child), std::invalid_argument);

	child.parent_id = root_public_state().id;
	coordinator.admit_public_state(child);
	EXPECT_EQ(coordinator.diagnostics().public_states_admitted, 2U);
	const auto original_id = child.id;
	child.id = { original_id.value + 1U };
	EXPECT_THROW(coordinator.admit_public_state(child), std::invalid_argument);
	child.id = original_id;
	child.canonical_history_id = 103;
	EXPECT_THROW(coordinator.admit_public_state(child), std::invalid_argument);
}

TEST_CASE(multiway_solver_public_state_lookup_returns_admitted_and_rejects_missing_ids)
{
	texas::MultiwaySolverCoordinator coordinator(valid_request());
	const auto child = checked_action_child_public_state();
	const auto* root_pointer = coordinator.find_public_state(root_public_state().id);
	EXPECT_TRUE(root_pointer != nullptr);
	coordinator.admit_public_state(child);

	EXPECT_TRUE(coordinator.find_public_state(root_public_state().id) == root_pointer);
	EXPECT_EQ(root_pointer->id, root_public_state().id);
	EXPECT_EQ(coordinator.find_public_state(child.id)->id, child.id);
	EXPECT_TRUE(coordinator.find_public_state({ 999 }) == nullptr);
}

TEST_CASE(multiway_solver_checkpoint_canonicalizes_public_states_in_place)
{
	texas::MultiwaySolverCoordinator coordinator(valid_request());
	coordinator.admit_public_state(checked_action_child_public_state());
	const auto checkpoint = coordinator.checkpoint();
	EXPECT_EQ(checkpoint.public_states.front().id, root_public_state().id);
	EXPECT_TRUE(checkpoint.public_states.size() >= 2U);
}

TEST_CASE(multiway_solver_child_admission_requires_a_replayed_parent_action_history)
{
	texas::MultiwaySolverCoordinator coordinator(valid_request());
	const auto child = checked_action_child_public_state();

	auto missing_history = child;
	missing_history.id = { 3 };
	missing_history.canonical_history_id = 103;
	missing_history.history.clear();
	EXPECT_THROW(coordinator.admit_public_state(missing_history), std::invalid_argument);

	auto wrong_actor = child;
	wrong_actor.id = { 3 };
	wrong_actor.canonical_history_id = 103;
	wrong_actor.history.back().actor = 1;
	EXPECT_THROW(coordinator.admit_public_state(wrong_actor), std::invalid_argument);

	auto wrong_action_menu = child;
	wrong_action_menu.id = { 3 };
	wrong_action_menu.canonical_history_id = 103;
	wrong_action_menu.history.back().action.action_menu_id = 9002;
	EXPECT_THROW(coordinator.admit_public_state(wrong_action_menu), std::invalid_argument);

	auto replay_mismatched_betting = child;
	replay_mismatched_betting.id = { 3 };
	replay_mismatched_betting.canonical_history_id = 103;
	replay_mismatched_betting.betting = root_public_state().betting;
	EXPECT_THROW(coordinator.admit_public_state(replay_mismatched_betting), std::invalid_argument);

	auto altered_board = child;
	altered_board.id = { 3 };
	altered_board.canonical_history_id = 103;
	altered_board.board[0] = card(3, 0);
	EXPECT_THROW(coordinator.admit_public_state(altered_board), std::invalid_argument);

	auto altered_runout = child;
	altered_runout.id = { 3 };
	altered_runout.canonical_history_id = 103;
	altered_runout.board_runout.chance_only_runout = true;
	EXPECT_THROW(coordinator.admit_public_state(altered_runout), std::invalid_argument);

	auto reused_parent_history_id = child;
	reused_parent_history_id.id = { 3 };
	reused_parent_history_id.canonical_history_id = 101;
	EXPECT_THROW(coordinator.admit_public_state(reused_parent_history_id), std::invalid_argument);

	coordinator.admit_public_state(child);
	auto nonappended_history = child;
	const auto grandchild_betting = texas::MultiwayState::from_snapshot(child.betting)
										.apply(texas::MultiwayAction::Check);
	nonappended_history.id = { 3 };
	nonappended_history.parent_id = { 2 };
	nonappended_history.canonical_history_id = 103;
	nonappended_history.betting = grandchild_betting.snapshot();
	nonappended_history.history = { { 1, { texas::MultiwayAction::Check, 0, 0, 9001 } } };
	EXPECT_THROW(coordinator.admit_public_state(nonappended_history), std::invalid_argument);
}

TEST_CASE(multiway_solver_public_state_duplicate_id_requires_the_complete_descriptor_to_match)
{
	texas::MultiwaySolverCoordinator coordinator(valid_request());
	auto child = checked_action_child_public_state();
	coordinator.admit_public_state(child);
	coordinator.admit_public_state(child);
	EXPECT_EQ(coordinator.diagnostics().public_states_admitted, 2U);

	auto changed_parent = child;
	changed_parent.parent_id = {};
	EXPECT_THROW(coordinator.admit_public_state(changed_parent), std::invalid_argument);

	auto changed_betting = child;
	changed_betting.betting.last_aggressor = 1;
	EXPECT_THROW(coordinator.admit_public_state(changed_betting), std::invalid_argument);

	auto changed_history = child;
	changed_history.history[0].actor = 1;
	EXPECT_THROW(coordinator.admit_public_state(changed_history), std::invalid_argument);

	auto changed_action_descriptor = child;
	for (auto& action : changed_action_descriptor.legal_actions)
	{
		action.action_menu_id = 9002;
	}
	EXPECT_THROW(coordinator.admit_public_state(changed_action_descriptor), std::invalid_argument);

	texas::MultiwaySolverCoordinator runout_coordinator(valid_request());
	const auto all_in = action_child_public_state(root_public_state(), 3, 103, 2);
	runout_coordinator.admit_public_state(all_in);
	auto runout = action_child_public_state(all_in, 4, 104, 1);
	runout_coordinator.admit_public_state(runout);
	runout_coordinator.admit_public_state(runout);
	EXPECT_EQ(runout_coordinator.diagnostics().public_states_admitted, 3U);

	auto changed_runout = runout;
	changed_runout.board.push_back(card(3, 3));
	changed_runout.board_runout.remaining_board_cards = 1;
	EXPECT_THROW(runout_coordinator.admit_public_state(changed_runout), std::invalid_argument);
}

TEST_CASE(multiway_solver_public_state_admission_enforces_its_capacity)
{
	auto limits = valid_limits();
	limits.max_public_states = 1;
	texas::MultiwayCFRConfig cfr;
	cfr.player_count = 2;
	texas::MultiwaySolverCoordinator coordinator(
		texas::MultiwaySolveRequest(valid_root(), cfr, limits));
	const auto child = checked_action_child_public_state();
	EXPECT_THROW(coordinator.admit_public_state(child), std::length_error);
}

TEST_CASE(multiway_solver_prepared_admission_uses_one_lock_and_filters_epoch_known_entries)
{
	texas::MultiwaySolverCoordinator coordinator(valid_request());
	const auto child = checked_action_child_public_state();
	const std::vector<texas::MultiwayPublicStateDescriptor> states = { child };
	const std::vector<std::size_t> order = { 0U };
	const texas::MultiwaySparseRowShape child_row{
		{ child.id, child.betting.current_player }, 1U,
		static_cast<std::uint8_t>(child.legal_actions.size())
	};
	const std::vector<texas::MultiwaySparseRowShape> rows = { child_row };
	texas::MultiwayAdmissionDiagnostics diagnostics;
	coordinator.set_admission_diagnostics(&diagnostics);

	coordinator.admit_prepared_batch(states, order, rows);
	EXPECT_EQ(diagnostics.lock_acquisitions, 1U);
	EXPECT_TRUE(coordinator.has_public_state_epoch(child.id));
	EXPECT_TRUE(coordinator.has_infoset_row_epoch(child_row.infoset));
	EXPECT_EQ(coordinator.diagnostics().public_states_admitted, 2U);
	EXPECT_EQ(coordinator.diagnostics().sparse_rows_admitted, 1U);
	EXPECT_TRUE(diagnostics.catalog_existing_menu_hits + diagnostics.catalog_new_menus > 0U);
	EXPECT_EQ(diagnostics.parent_transition_action_edges, 1U);
	EXPECT_EQ(diagnostics.parent_transition_board_chance_edges, 0U);
	EXPECT_EQ(diagnostics.parent_transition_street_edges, 0U);
	EXPECT_EQ(diagnostics.parent_transition_action_history_prefix_elements, root_public_state().history.size());
	EXPECT_EQ(diagnostics.parent_transition_action_menu_probes, 1U);
	EXPECT_TRUE(diagnostics.parent_transition_validation_nanoseconds >= diagnostics.parent_transition_fixed_state_nanoseconds + diagnostics.parent_transition_action_edge_nanoseconds);
	EXPECT_TRUE(diagnostics.catalog_menu_index_probes + diagnostics.catalog_menu_equality_checks >= diagnostics.catalog_existing_menu_hits);
	EXPECT_TRUE(diagnostics.catalog_menu_lookup_nanoseconds >= diagnostics.catalog_menu_index_lookup_nanoseconds + diagnostics.catalog_menu_equality_nanoseconds);
	EXPECT_TRUE(diagnostics.catalog_append_nanoseconds >= diagnostics.catalog_node_pack_nanoseconds);
	EXPECT_TRUE(diagnostics.public_state_validation_nanoseconds >= diagnostics.public_state_betting_board_validation_nanoseconds + diagnostics.public_state_menu_validation_nanoseconds + diagnostics.public_state_history_identity_validation_nanoseconds);

	coordinator.admit_prepared_batch(states, order, rows);
	EXPECT_EQ(diagnostics.lock_acquisitions, 2U);
	EXPECT_EQ(coordinator.diagnostics().public_states_admitted, 2U);
	EXPECT_EQ(coordinator.diagnostics().sparse_rows_admitted, 1U);
}

TEST_CASE(multiway_solver_prepared_admission_uses_packed_existing_state_compatibility)
{
	texas::MultiwaySolverCoordinator coordinator(valid_request());
	const auto root = root_public_state();
	const auto child = checked_action_child_public_state();
	std::vector<texas::MultiwayPublicStateDescriptor> states = { root, child };
	std::sort(states.begin(), states.end(), [](const auto& left, const auto& right) { return left.id < right.id; });
	const auto root_index = static_cast<std::size_t>(std::find_if(states.begin(), states.end(), [&](const auto& state) {
		return state.id == root.id;
	}) - states.begin());
	const auto child_index = static_cast<std::size_t>(std::find_if(states.begin(), states.end(), [&](const auto& state) {
		return state.id == child.id;
	}) - states.begin());

	texas::MultiwayAdmissionDiagnostics diagnostics;
	coordinator.set_admission_diagnostics(&diagnostics);
	coordinator.admit_prepared_batch(states, { root_index, child_index }, {});

	EXPECT_EQ(diagnostics.existing_public_state_checks, 1U);
	EXPECT_EQ(diagnostics.packed_public_state_compatibility_checks, 1U);
	EXPECT_EQ(diagnostics.new_public_state_validations, 1U);
	EXPECT_EQ(coordinator.diagnostics().public_states_admitted, 2U);
}

TEST_CASE(multiway_solver_prepared_admission_reports_board_and_street_transition_edges)
{
	{
		texas::MultiwaySolverCoordinator coordinator(valid_request());
		const auto all_in = action_child_public_state(root_public_state(), 2, 102, 2);
		coordinator.admit_public_state(all_in);
		const auto runout = action_child_public_state(all_in, 4, 104, 1);
		coordinator.admit_public_state(runout);
		texas::MultiwayTerminalAdapter adapter(coordinator);
		const auto deal = adapter.sample_private_deal(1U);
		const auto edges = adapter.canonical_public_board_chance_edges(runout.id, deal);
		const auto child = texas::MultiwayPublicBuilder::make_board_chance_child(
			runout, edges.front(), runout.legal_actions);
		texas::MultiwayAdmissionDiagnostics diagnostics;
		coordinator.set_admission_diagnostics(&diagnostics);

		coordinator.admit_prepared_batch({ child }, { 0U }, {});

		EXPECT_EQ(diagnostics.parent_transition_action_edges, 0U);
		EXPECT_EQ(diagnostics.parent_transition_board_chance_edges, 1U);
		EXPECT_EQ(diagnostics.parent_transition_street_edges, 0U);
	}

	{
		texas::MultiwaySolverCoordinator coordinator(valid_request());
		const auto first = action_child_public_state(root_public_state(), 2, 102, 0);
		coordinator.admit_public_state(first);
		const auto second = action_child_public_state(first, 3, 103, 0);
		coordinator.admit_public_state(second);
		texas::MultiwayTerminalAdapter adapter(coordinator);
		const auto deal = adapter.sample_private_deal(1U);
		const auto board_edges = adapter.canonical_public_board_chance_edges(second.id, deal);
		const auto board_child = texas::MultiwayPublicBuilder::make_board_chance_child(
			second, board_edges.front(), {});
		coordinator.admit_public_state(board_child);
		const auto transition = adapter.apply_public_street_transition(board_child.id);
		const auto child = texas::MultiwayPublicBuilder::make_street_transition_child(
			board_child, transition,
			action_descriptors(texas::MultiwayState::from_snapshot(transition.transition.betting)));
		texas::MultiwayAdmissionDiagnostics diagnostics;
		coordinator.set_admission_diagnostics(&diagnostics);

		coordinator.admit_prepared_batch({ child }, { 0U }, {});

		EXPECT_EQ(diagnostics.parent_transition_action_edges, 0U);
		EXPECT_EQ(diagnostics.parent_transition_board_chance_edges, 0U);
		EXPECT_EQ(diagnostics.parent_transition_street_edges, 1U);
	}
}

TEST_CASE(multiway_solver_prepared_admission_accepts_equivalent_chance_runout_parents)
{
	texas::MultiwaySolverCoordinator coordinator(valid_request());
	const auto all_in = action_child_public_state(root_public_state(), 2, 102, 2);
	coordinator.admit_public_state(all_in);
	const auto runout = action_child_public_state(all_in, 4, 104, 1);
	coordinator.admit_public_state(runout);
	texas::MultiwayTerminalAdapter adapter(coordinator);
	const auto deal = adapter.sample_private_deal(1U);
	const auto first_edges = adapter.canonical_public_board_chance_edges(runout.id, deal);
	EXPECT_TRUE(first_edges.size() >= 2U);
	const auto first_turn = texas::MultiwayPublicBuilder::make_board_chance_child(
		runout, first_edges[0U], {});
	const auto second_turn = texas::MultiwayPublicBuilder::make_board_chance_child(
		runout, first_edges[1U], {});
	coordinator.admit_public_state(first_turn);
	coordinator.admit_public_state(second_turn);

	const auto first_river_edges = adapter.canonical_public_board_chance_edges(first_turn.id, deal);
	const auto second_river_edges = adapter.canonical_public_board_chance_edges(second_turn.id, deal);
	const auto first_river = texas::MultiwayPublicBuilder::make_board_chance_child(
		first_turn, board_edge_dealing(first_river_edges, first_edges[1U].chance.dealt_card), {});
	const auto second_river = texas::MultiwayPublicBuilder::make_board_chance_child(
		second_turn, board_edge_dealing(second_river_edges, first_edges[0U].chance.dealt_card), {});
	EXPECT_EQ(first_river.id, second_river.id);
	EXPECT_TRUE(first_river.parent_id != second_river.parent_id);
	EXPECT_EQ(first_river.board, second_river.board);
	EXPECT_EQ(first_river.canonical_history_id, second_river.canonical_history_id);

	coordinator.admit_public_state(first_river);
	coordinator.admit_prepared_batch({ second_river }, { 0U }, {});
	EXPECT_EQ(coordinator.diagnostics().public_states_admitted, 6U);
}

TEST_CASE(multiway_solver_prepared_admission_rejection_does_not_commit_transition_diagnostics)
{
	texas::MultiwaySolverCoordinator coordinator(valid_request());
	const auto before = coordinator.checkpoint();
	auto child = checked_action_child_public_state();
	child.incoming_edge.action.action_menu_id += 1U;
	texas::MultiwayAdmissionDiagnostics diagnostics;
	coordinator.set_admission_diagnostics(&diagnostics);

	EXPECT_THROW(coordinator.admit_prepared_batch({ child }, { 0U }, {}), std::invalid_argument);

	EXPECT_EQ(diagnostics.parent_transition_fixed_state_nanoseconds, 0U);
	EXPECT_EQ(diagnostics.parent_transition_action_edge_nanoseconds, 0U);
	EXPECT_EQ(diagnostics.parent_transition_board_chance_edge_nanoseconds, 0U);
	EXPECT_EQ(diagnostics.parent_transition_street_edge_nanoseconds, 0U);
	EXPECT_EQ(diagnostics.parent_transition_action_edges, 0U);
	EXPECT_EQ(diagnostics.parent_transition_board_chance_edges, 0U);
	EXPECT_EQ(diagnostics.parent_transition_street_edges, 0U);
	EXPECT_EQ(diagnostics.parent_transition_action_history_prefix_elements, 0U);
	EXPECT_EQ(diagnostics.parent_transition_action_menu_probes, 0U);
	EXPECT_EQ(diagnostics.parent_transition_validation_nanoseconds, 0U);
	const auto after = coordinator.checkpoint();
	EXPECT_EQ(after.public_states.size(), before.public_states.size());
	EXPECT_EQ(after.storage.shapes.size(), before.storage.shapes.size());
	EXPECT_EQ(coordinator.diagnostics().public_states_admitted, 1U);
}

TEST_CASE(multiway_solver_direct_admission_does_not_publish_transition_diagnostics)
{
	texas::MultiwaySolverCoordinator coordinator(valid_request());
	texas::MultiwayAdmissionDiagnostics diagnostics;
	coordinator.set_admission_diagnostics(&diagnostics);

	coordinator.admit_public_state(checked_action_child_public_state());

	EXPECT_EQ(diagnostics.parent_transition_fixed_state_nanoseconds, 0U);
	EXPECT_EQ(diagnostics.parent_transition_action_edge_nanoseconds, 0U);
	EXPECT_EQ(diagnostics.parent_transition_action_edges, 0U);
	EXPECT_EQ(diagnostics.parent_transition_validation_nanoseconds, 0U);
}

TEST_CASE(multiway_solver_prepared_admission_rejects_capacity_before_mutation)
{
	auto limits = valid_limits();
	limits.max_public_states = 2U;
	limits.max_sparse_rows = 1U;
	limits.max_sparse_values = 2U;
	texas::MultiwayCFRConfig cfr;
	cfr.player_count = 2;
	texas::MultiwaySolverCoordinator coordinator(
		texas::MultiwaySolveRequest(valid_root(), cfr, limits));
	const auto before = coordinator.checkpoint();
	const auto first_child = action_child_public_state(root_public_state(), 2, 102, 0);
	const auto second_child = action_child_public_state(root_public_state(), 2, 102, 1);
	std::vector<texas::MultiwayPublicStateDescriptor> states = { first_child, second_child };
	std::sort(states.begin(), states.end(), [](const auto& left, const auto& right) { return left.id < right.id; });
	const std::vector<std::size_t> order = { 0U, 1U };
	EXPECT_THROW(coordinator.admit_prepared_batch(states, order, {}), std::length_error);
	EXPECT_EQ(coordinator.diagnostics().public_states_admitted, 1U);
	const auto after_state_rejection = coordinator.checkpoint();
	EXPECT_EQ(after_state_rejection.public_states.size(), before.public_states.size());
	EXPECT_EQ(after_state_rejection.public_states.front().id, before.public_states.front().id);

	const auto child_row = texas::MultiwaySparseRowShape{
		{ first_child.id, first_child.betting.current_player }, 1U,
		static_cast<std::uint8_t>(first_child.legal_actions.size())
	};
	std::vector<texas::MultiwaySparseRowShape> rows = { root_row(), child_row };
	std::sort(rows.begin(), rows.end(), [](const auto& left, const auto& right) { return left.infoset < right.infoset; });
	EXPECT_THROW(coordinator.admit_prepared_batch({ first_child }, { 0U }, rows), std::length_error);
	EXPECT_EQ(coordinator.diagnostics().public_states_admitted, 1U);
	EXPECT_EQ(coordinator.diagnostics().sparse_rows_admitted, 0U);
	const auto after_row_rejection = coordinator.checkpoint();
	EXPECT_EQ(after_row_rejection.public_states.size(), before.public_states.size());
	EXPECT_EQ(after_row_rejection.storage.shapes.size(), before.storage.shapes.size());
}

TEST_CASE(multiway_public_catalog_batch_commit_preserves_sorted_reconstruction)
{
	texas::MultiwayPublicCatalog catalog(8U);
	catalog.admit(root_public_state());
	const auto first = action_child_public_state(root_public_state(), 2, 102, 0);
	const auto second = action_child_public_state(root_public_state(), 2, 102, 1);
	std::vector<texas::MultiwayPublicStateDescriptor> states = { second, first };
	std::sort(states.begin(), states.end(), [](const auto& left, const auto& right) { return left.id < right.id; });
	catalog.admit_batch(states, { 1U, 1U });

	EXPECT_EQ(catalog.size(), 3U);
	EXPECT_TRUE(catalog.contains(first.id));
	EXPECT_TRUE(catalog.contains(second.id));
	std::vector<texas::MultiwayPublicStateDescriptor> exported;
	catalog.export_descriptors(exported);
	EXPECT_EQ(exported.size(), 3U);
	EXPECT_TRUE(std::is_sorted(exported.begin(), exported.end(), [](const auto& left, const auto& right) { return left.id < right.id; }));
	EXPECT_TRUE(std::any_of(exported.begin(), exported.end(), [&](const auto& state) { return state.id == states[0].id; }));
	EXPECT_TRUE(std::any_of(exported.begin(), exported.end(), [&](const auto& state) { return state.id == states[1].id; }));
}

TEST_CASE(multiway_sparse_row_batch_commit_preserves_sorted_offsets)
{
	texas::MultiwaySolverCoordinator coordinator(valid_request());
	const auto first = action_child_public_state(root_public_state(), 2, 102, 0);
	const auto second = action_child_public_state(root_public_state(), 2, 102, 1);
	std::vector<texas::MultiwayPublicStateDescriptor> states = { first, second };
	std::sort(states.begin(), states.end(), [](const auto& left, const auto& right) { return left.id < right.id; });
	std::vector<texas::MultiwaySparseRowShape> rows;
	for (const auto& state : states)
		rows.push_back({ { state.id, state.betting.current_player }, 1U, static_cast<std::uint8_t>(state.legal_actions.size()) });
	std::sort(rows.begin(), rows.end(), [](const auto& left, const auto& right) { return left.infoset < right.infoset; });
	coordinator.admit_prepared_batch(states, { 0U, 1U }, rows);

	const auto& metadata = coordinator.storage().rows();
	EXPECT_EQ(metadata.size(), rows.size());
	EXPECT_TRUE(metadata[0].shape.infoset < metadata[1].shape.infoset);
	EXPECT_EQ(metadata[1].regret_offset, metadata[0].regret_offset + metadata[0].value_count());
	EXPECT_EQ(coordinator.storage().value_count(), metadata[0].value_count() + metadata[1].value_count());
}

TEST_CASE(multiway_compact_storage_batch_commit_initializes_sorted_rows_and_pages)
{
	texas::MultiwayCompactStorage storage(4U, 32U, 2U);
	const texas::MultiwaySparseRowShape first{ { { 20U }, 1 }, 3U, 2U };
	const texas::MultiwaySparseRowShape second{ { { 10U }, 0 }, 3U, 2U };
	storage.admit_rows({ first, second }, { 1U, 1U });

	EXPECT_EQ(storage.row_count(), 2U);
	EXPECT_EQ(storage.page_count(), 2U);
	EXPECT_EQ(storage.value_count(), 8U);
	EXPECT_TRUE(storage.rows()[0].shape.infoset.public_state < storage.rows()[1].shape.infoset.public_state);
	EXPECT_TRUE(storage.has_page(first.infoset, 0U));
	EXPECT_TRUE(storage.has_page(second.infoset, 0U));
}

TEST_CASE(multiway_solver_worker_delta_stream_is_bounded_finite_and_sortable)
{
	texas::MultiwayWorkerDeltaStream stream(0, 2);
	EXPECT_TRUE(stream.try_append(delta(2, 1.0, 1.0, 2)));
	EXPECT_TRUE(stream.try_append(delta(0, 1.0, 1.0, 1)));
	EXPECT_TRUE(!stream.try_append(delta(1, 1.0, 1.0, 3)));
	EXPECT_TRUE(!stream.is_fixed_order());
	stream.sort_fixed_order();
	EXPECT_TRUE(stream.is_fixed_order());
	EXPECT_EQ(stream.deltas()[0].action, 0U);

	texas::MultiwayWorkerDeltaStream finite_stream(1, 1);
	EXPECT_TRUE(!finite_stream.try_append(delta(0, std::numeric_limits<double>::infinity(), 1.0)));
}

TEST_CASE(multiway_solver_stream_capacities_fallback_and_exhaust_independently)
{
	texas::MultiwaySolverLimits legacy = valid_limits();
	legacy.max_discovery_entries = 9U;
	EXPECT_EQ(legacy.regret_delta_capacity(), 4U);
	EXPECT_EQ(legacy.continuation_delta_capacity(), 4U);
	EXPECT_EQ(legacy.discovery_public_state_capacity(), 9U);
	EXPECT_EQ(legacy.discovery_row_capacity(), 9U);

	texas::MultiwaySolverLimits explicit_limits = legacy;
	explicit_limits.max_regret_delta_entries_per_worker = 2U;
	explicit_limits.max_continuation_delta_entries_per_worker = 3U;
	explicit_limits.max_discovery_public_states_per_worker = 5U;
	explicit_limits.max_discovery_rows_per_worker = 7U;
	EXPECT_EQ(explicit_limits.regret_delta_capacity(), 2U);
	EXPECT_EQ(explicit_limits.continuation_delta_capacity(), 3U);
	EXPECT_EQ(explicit_limits.discovery_public_state_capacity(), 5U);
	EXPECT_EQ(explicit_limits.discovery_row_capacity(), 7U);

	texas::MultiwayWorkerDeltaStream regret(0U, 0U);
	EXPECT_TRUE(!regret.try_append({}));
	EXPECT_TRUE(regret.capacity_exhausted());
	texas::MultiwayContinuationDeltaStream continuation(0U, 0U);
	EXPECT_TRUE(!continuation.try_append({}));
	EXPECT_TRUE(continuation.capacity_exhausted());
	texas::MultiwayDiscoveryStream discovery(0U, 1U);
	EXPECT_TRUE(!discovery.try_append_public_state({}));
	EXPECT_TRUE(discovery.public_state_capacity_exhausted());
	EXPECT_TRUE(!discovery.row_capacity_exhausted());
	EXPECT_TRUE(discovery.try_append_row({}));
	texas::MultiwayDiscoveryStream rows(1U, 0U);
	EXPECT_TRUE(!rows.try_append_row({}));
	EXPECT_TRUE(rows.row_capacity_exhausted());
	EXPECT_TRUE(!rows.public_state_capacity_exhausted());
}

TEST_CASE(multiway_solver_stream_capacity_validation_rejects_worker_overflow)
{
	const auto worker_overflow = [] {
		auto limits = valid_limits();
		limits.max_regret_delta_entries_per_worker = std::numeric_limits<std::size_t>::max() / limits.worker_count + 1U;
		return limits;
	}();
	EXPECT_THROW(worker_overflow.validate(), std::overflow_error);

	const auto continuation_overflow = [] {
		auto limits = valid_limits();
		limits.max_continuation_delta_entries_per_worker = std::numeric_limits<std::size_t>::max() / limits.worker_count + 1U;
		return limits;
	}();
	EXPECT_THROW(continuation_overflow.validate(), std::overflow_error);

	const auto discovery_overflow = [] {
		auto limits = valid_limits();
		limits.max_discovery_public_states_per_worker = std::numeric_limits<std::size_t>::max() / limits.worker_count + 1U;
		return limits;
	}();
	EXPECT_THROW(discovery_overflow.validate(), std::overflow_error);
}

TEST_CASE(multiway_solver_bounded_stream_append_is_allocation_free_after_construction)
{
	texas::MultiwayWorkerDeltaStream stream(0U, 2U);
	texas::MultiwayDiscoveryStream discovery(2U, 2U);
	const auto worker_delta = delta(0U, 1.0, 1.0, 1U);
	const auto row = root_row();
	texas::MultiwayAllocationProbe probe;
	{
		texas::MultiwayAllocationProbeScope scope(probe);
		EXPECT_TRUE(stream.try_append(worker_delta));
		EXPECT_TRUE(discovery.try_append_row(row));
	}
	const auto allocations = probe.snapshot();
	EXPECT_EQ(allocations.allocation_count, 0U);
	EXPECT_EQ(allocations.allocated_bytes, 0U);
}

TEST_CASE(multiway_solver_discovery_stream_prepares_sorted_unique_candidates)
{
	texas::MultiwayDiscoveryStream stream(8U);
	const auto first_child = action_child_public_state(root_public_state(), 2, 102, 0);
	const auto second_child = action_child_public_state(root_public_state(), 2, 102, 1);
	EXPECT_TRUE(stream.try_append_public_state(second_child));
	EXPECT_TRUE(stream.try_append_public_state(first_child));
	EXPECT_TRUE(stream.try_append_public_state(first_child));
	EXPECT_TRUE(stream.try_append_row(root_row()));
	EXPECT_TRUE(stream.try_append_row(root_row()));

	stream.prepare_for_merge();
	EXPECT_EQ(stream.public_state_count(), 2U);
	EXPECT_EQ(stream.row_count(), 1U);
	EXPECT_TRUE(stream.public_states()[0].id < stream.public_states()[1].id);
	EXPECT_EQ(stream.rows().front().infoset, root_infoset());
}

TEST_CASE(multiway_solver_discovery_stream_rejects_conflicting_candidates)
{
	const auto child = checked_action_child_public_state();
	texas::MultiwayDiscoveryStream states(4U);
	EXPECT_TRUE(states.try_append_public_state(child));
	auto conflicting_child = child;
	conflicting_child.betting.current_bet += 1;
	EXPECT_TRUE(states.try_append_public_state(conflicting_child));
	EXPECT_THROW(states.prepare_for_merge(), std::invalid_argument);

	texas::MultiwayDiscoveryStream rows(4U);
	EXPECT_TRUE(rows.try_append_row(root_row(1U, 3U)));
	EXPECT_TRUE(rows.try_append_row(root_row(2U, 3U)));
	EXPECT_THROW(rows.prepare_for_merge(), std::invalid_argument);
}

TEST_CASE(multiway_solver_discovery_merge_collapses_cross_worker_duplicates)
{
	texas::MultiwayDiscoveryStream first(8U);
	texas::MultiwayDiscoveryStream second(8U);
	const auto first_child = checked_action_child_public_state();
	const auto second_child = action_child_public_state(root_public_state(), 2, 102, 1);
	EXPECT_TRUE(first.try_append_public_state(first_child));
	EXPECT_TRUE(second.try_append_public_state(second_child));
	EXPECT_TRUE(second.try_append_public_state(first_child));
	EXPECT_TRUE(first.try_append_row(root_row()));
	EXPECT_TRUE(second.try_append_row(root_row()));
	first.prepare_for_merge();
	second.prepare_for_merge();

	const std::vector<const texas::MultiwayDiscoveryStream*> streams = { &first, &second };
	std::vector<texas::MultiwayPublicStateDescriptor> public_states;
	std::vector<texas::MultiwaySparseRowShape> rows;
	std::vector<std::size_t> cursors;
	texas::solver::multiway::detail::merge_multiway_discovery_streams(streams, public_states, rows, cursors);

	EXPECT_EQ(public_states.size(), 2U);
	EXPECT_EQ(rows.size(), 1U);
	EXPECT_TRUE(public_states[0].id < public_states[1].id);

	texas::MultiwayDiscoveryStream conflicting(4U);
	auto conflicting_child = first_child;
	conflicting_child.betting.current_bet += 1;
	EXPECT_TRUE(conflicting.try_append_public_state(conflicting_child));
	conflicting.prepare_for_merge();
	const std::vector<const texas::MultiwayDiscoveryStream*> conflicting_streams = { &first, &conflicting };
	EXPECT_THROW(texas::solver::multiway::detail::merge_multiway_discovery_streams(conflicting_streams, public_states, rows, cursors), std::invalid_argument);
}

TEST_CASE(multiway_solver_discovery_merge_reports_source_and_duplicates)
{
	texas::MultiwayDiscoveryStream first(8U);
	texas::MultiwayDiscoveryStream second(8U);
	const auto first_child = checked_action_child_public_state();
	const auto second_child = action_child_public_state(root_public_state(), 2, 102, 1);
	EXPECT_TRUE(first.try_append_public_state(first_child));
	EXPECT_TRUE(second.try_append_public_state(second_child));
	EXPECT_TRUE(second.try_append_public_state(first_child));
	EXPECT_TRUE(first.try_append_row(root_row()));
	EXPECT_TRUE(second.try_append_row(root_row()));
	first.prepare_for_merge();
	second.prepare_for_merge();

	const std::vector<const texas::MultiwayDiscoveryStream*> streams = { &first, &second };
	std::vector<texas::MultiwayPublicStateDescriptor> public_states;
	std::vector<texas::MultiwaySparseRowShape> rows;
	std::vector<std::size_t> cursors;
	texas::MultiwayAdmissionDiagnostics diagnostics;
	texas::solver::multiway::detail::merge_multiway_discovery_streams(
		streams, public_states, rows, cursors, &diagnostics);

	EXPECT_EQ(public_states.size(), 2U);
	EXPECT_EQ(rows.size(), 1U);
	EXPECT_EQ(diagnostics.discovery_public_state_source_elements, 3U);
	EXPECT_EQ(diagnostics.discovery_row_source_elements, 2U);
	EXPECT_EQ(diagnostics.discovery_public_state_duplicate_candidates, 1U);
	EXPECT_EQ(diagnostics.discovery_row_duplicate_candidates, 1U);
	EXPECT_EQ(diagnostics.discovery_single_stream_batches, 0U);
	EXPECT_EQ(diagnostics.discovery_one_stream_fast_path_uses, 0U);
}

TEST_CASE(multiway_solver_discovery_merge_reports_single_stream_eligibility)
{
	texas::MultiwayDiscoveryStream stream(8U);
	EXPECT_TRUE(stream.try_append_public_state(checked_action_child_public_state()));
	EXPECT_TRUE(stream.try_append_row(root_row()));
	stream.prepare_for_merge();

	const std::vector<const texas::MultiwayDiscoveryStream*> streams = { &stream };
	std::vector<texas::MultiwayPublicStateDescriptor> public_states;
	std::vector<texas::MultiwaySparseRowShape> rows;
	std::vector<std::size_t> cursors;
	texas::MultiwayAdmissionDiagnostics diagnostics;
	texas::solver::multiway::detail::merge_multiway_discovery_streams(
		streams, public_states, rows, cursors, &diagnostics);

	EXPECT_EQ(diagnostics.discovery_single_stream_batches, 1U);
	EXPECT_EQ(diagnostics.discovery_one_stream_fast_path_uses, 1U);
	EXPECT_EQ(diagnostics.discovery_public_state_source_elements, public_states.size());
	EXPECT_EQ(diagnostics.discovery_row_source_elements, rows.size());
	EXPECT_EQ(diagnostics.discovery_public_state_duplicate_candidates, 0U);
	EXPECT_EQ(diagnostics.discovery_row_duplicate_candidates, 0U);
}

TEST_CASE(multiway_solver_discovery_merge_reference_and_tournament_match_duplicate_heavy_streams)
{
	const auto first_child = checked_action_child_public_state();
	const auto second_child = action_child_public_state(root_public_state(), 2, 102, 1);
	std::vector<texas::MultiwayDiscoveryStream> worker_streams;
	worker_streams.reserve(6U);
	for (std::size_t worker = 0U; worker < 6U; ++worker)
	{
		worker_streams.emplace_back(8U, 8U);
		EXPECT_TRUE(worker_streams.back().try_append_public_state(second_child));
		EXPECT_TRUE(worker_streams.back().try_append_public_state(first_child));
		EXPECT_TRUE(worker_streams.back().try_append_row(root_row()));
		EXPECT_TRUE(worker_streams.back().try_append_row({ { first_child.id, first_child.betting.current_player }, 1U, static_cast<std::uint8_t>(first_child.legal_actions.size()) }));
		worker_streams.back().prepare_for_merge();
	}
	std::vector<const texas::MultiwayDiscoveryStream*> streams;
	streams.reserve(worker_streams.size());
	for (const auto& stream : worker_streams)
		streams.push_back(&stream);

	texas::solver::multiway::detail::MultiwayDiscoveryMergeScratch reference_scratch;
	reference_scratch.reserve(streams.size());
	texas::solver::multiway::detail::MultiwayDiscoveryMergeScratch tournament_scratch;
	tournament_scratch.reserve(streams.size());
	std::vector<texas::MultiwayPublicStateDescriptor> reference_states;
	std::vector<texas::MultiwaySparseRowShape> reference_rows;
	std::vector<texas::MultiwayPublicStateDescriptor> tournament_states;
	std::vector<texas::MultiwaySparseRowShape> tournament_rows;
	texas::MultiwayAdmissionDiagnostics reference_diagnostics;
	texas::MultiwayAdmissionDiagnostics tournament_diagnostics;
	texas::solver::multiway::detail::merge_multiway_discovery_streams(
		streams, reference_states, reference_rows, reference_scratch,
		texas::solver::multiway::detail::MultiwayDiscoveryMergePath::Reference,
		&reference_diagnostics);
	texas::solver::multiway::detail::merge_multiway_discovery_streams(
		streams, tournament_states, tournament_rows, tournament_scratch,
		texas::solver::multiway::detail::MultiwayDiscoveryMergePath::Tournament,
		&tournament_diagnostics);

	EXPECT_EQ(reference_states.size(), 2U);
	EXPECT_EQ(reference_rows.size(), 2U);
	EXPECT_EQ(tournament_states.size(), reference_states.size());
	EXPECT_EQ(tournament_rows.size(), reference_rows.size());
	for (std::size_t index = 0U; index < reference_states.size(); ++index)
		expect_public_state_descriptor_equal(reference_states[index], tournament_states[index]);
	for (std::size_t index = 0U; index < reference_rows.size(); ++index)
	{
		EXPECT_EQ(reference_rows[index].infoset, tournament_rows[index].infoset);
		EXPECT_EQ(reference_rows[index].bucket_count, tournament_rows[index].bucket_count);
		EXPECT_EQ(reference_rows[index].action_count, tournament_rows[index].action_count);
	}
	EXPECT_EQ(reference_diagnostics.discovery_public_state_source_elements, 12U);
	EXPECT_EQ(reference_diagnostics.discovery_row_source_elements, 12U);
	EXPECT_EQ(reference_diagnostics.discovery_public_state_duplicate_candidates, 10U);
	EXPECT_EQ(reference_diagnostics.discovery_row_duplicate_candidates, 10U);
	EXPECT_EQ(tournament_diagnostics.discovery_public_state_source_elements, reference_diagnostics.discovery_public_state_source_elements);
	EXPECT_EQ(tournament_diagnostics.discovery_row_source_elements, reference_diagnostics.discovery_row_source_elements);
	EXPECT_EQ(tournament_diagnostics.discovery_public_state_duplicate_candidates, reference_diagnostics.discovery_public_state_duplicate_candidates);
	EXPECT_EQ(tournament_diagnostics.discovery_row_duplicate_candidates, reference_diagnostics.discovery_row_duplicate_candidates);
	EXPECT_EQ(tournament_diagnostics.discovery_public_descriptor_copy_count, reference_diagnostics.discovery_public_descriptor_copy_count);
	EXPECT_EQ(tournament_scratch.cursors.capacity(), streams.size());
	EXPECT_EQ(tournament_scratch.tournament.capacity(), streams.size());
}

TEST_CASE(multiway_solver_discovery_merge_selects_tournament_only_above_threshold)
{
	using texas::solver::multiway::detail::MULTIWAY_DISCOVERY_TOURNAMENT_STREAM_THRESHOLD;
	using texas::solver::multiway::detail::MultiwayDiscoveryMergePath;
	EXPECT_EQ(
		texas::solver::multiway::detail::select_multiway_discovery_merge_path(
			MULTIWAY_DISCOVERY_TOURNAMENT_STREAM_THRESHOLD),
		MultiwayDiscoveryMergePath::Reference);
	EXPECT_EQ(
		texas::solver::multiway::detail::select_multiway_discovery_merge_path(
			MULTIWAY_DISCOVERY_TOURNAMENT_STREAM_THRESHOLD + 1U),
		MultiwayDiscoveryMergePath::Tournament);
}

TEST_CASE(multiway_solver_discovery_merge_tournament_handles_empty_inputs)
{
	const std::vector<const texas::MultiwayDiscoveryStream*> streams;
	texas::solver::multiway::detail::MultiwayDiscoveryMergeScratch scratch;
	scratch.reserve(0U);
	std::vector<texas::MultiwayPublicStateDescriptor> public_states;
	std::vector<texas::MultiwaySparseRowShape> rows;
	texas::MultiwayAdmissionDiagnostics diagnostics;
	texas::solver::multiway::detail::merge_multiway_discovery_streams(
		streams, public_states, rows, scratch,
		texas::solver::multiway::detail::MultiwayDiscoveryMergePath::Tournament,
		&diagnostics);
	EXPECT_TRUE(public_states.empty());
	EXPECT_TRUE(rows.empty());
	EXPECT_TRUE(scratch.cursors.empty());
}

TEST_CASE(multiway_solver_discovery_one_stream_fast_path_matches_generic_merge)
{
	texas::MultiwayDiscoveryStream stream(8U);
	texas::MultiwayDiscoveryStream empty(8U);
	const auto child = checked_action_child_public_state();
	const auto second_child = action_child_public_state(root_public_state(), 2, 102, 1);
	EXPECT_TRUE(stream.try_append_public_state(child));
	EXPECT_TRUE(stream.try_append_public_state(second_child));
	EXPECT_TRUE(stream.try_append_row(root_row()));
	EXPECT_TRUE(stream.try_append_row({ { child.id, child.betting.current_player }, 1U, static_cast<std::uint8_t>(child.legal_actions.size()) }));
	stream.prepare_for_merge();
	empty.prepare_for_merge();

	std::vector<texas::MultiwayPublicStateDescriptor> fast_public_states;
	std::vector<texas::MultiwaySparseRowShape> fast_rows;
	std::vector<std::size_t> fast_cursors;
	texas::MultiwayAdmissionDiagnostics fast_diagnostics;
	texas::solver::multiway::detail::merge_multiway_discovery_streams(
		{ &stream }, fast_public_states, fast_rows, fast_cursors, &fast_diagnostics);

	std::vector<texas::MultiwayPublicStateDescriptor> generic_public_states;
	std::vector<texas::MultiwaySparseRowShape> generic_rows;
	std::vector<std::size_t> generic_cursors;
	texas::MultiwayAdmissionDiagnostics generic_diagnostics;
	texas::solver::multiway::detail::merge_multiway_discovery_streams(
		{ &stream, &empty }, generic_public_states, generic_rows, generic_cursors, &generic_diagnostics);

	EXPECT_EQ(fast_public_states.size(), generic_public_states.size());
	for (std::size_t index = 0U; index < fast_public_states.size(); ++index)
		expect_public_state_descriptor_equal(generic_public_states[index], fast_public_states[index]);
	EXPECT_EQ(fast_rows.size(), generic_rows.size());
	for (std::size_t index = 0U; index < fast_rows.size(); ++index)
	{
		EXPECT_EQ(fast_rows[index].infoset, generic_rows[index].infoset);
		EXPECT_EQ(fast_rows[index].bucket_count, generic_rows[index].bucket_count);
		EXPECT_EQ(fast_rows[index].action_count, generic_rows[index].action_count);
	}
	EXPECT_EQ(fast_cursors.size(), 1U);
	EXPECT_EQ(fast_cursors.front(), fast_rows.size());
	EXPECT_EQ(generic_cursors.size(), 2U);
	EXPECT_EQ(generic_cursors[0], generic_rows.size());
	EXPECT_EQ(generic_cursors[1], 0U);
	EXPECT_EQ(fast_diagnostics.discovery_single_stream_batches, 1U);
	EXPECT_EQ(fast_diagnostics.discovery_one_stream_fast_path_uses, 1U);
	EXPECT_EQ(generic_diagnostics.discovery_single_stream_batches, 0U);
	EXPECT_EQ(generic_diagnostics.discovery_one_stream_fast_path_uses, 0U);

	const auto fast_public_states_before_rewind = fast_public_states;
	const auto fast_rows_before_rewind = fast_rows;
	stream.rewind();
	EXPECT_TRUE(stream.try_append_public_state(second_child));
	EXPECT_TRUE(stream.try_append_row(root_row(2U, 3U)));
	stream.prepare_for_merge();
	EXPECT_EQ(fast_public_states.size(), fast_public_states_before_rewind.size());
	for (std::size_t index = 0U; index < fast_public_states.size(); ++index)
		expect_public_state_descriptor_equal(fast_public_states_before_rewind[index], fast_public_states[index]);
	EXPECT_EQ(fast_rows.size(), fast_rows_before_rewind.size());
	for (std::size_t index = 0U; index < fast_rows.size(); ++index)
	{
		EXPECT_EQ(fast_rows[index].infoset, fast_rows_before_rewind[index].infoset);
		EXPECT_EQ(fast_rows[index].bucket_count, fast_rows_before_rewind[index].bucket_count);
		EXPECT_EQ(fast_rows[index].action_count, fast_rows_before_rewind[index].action_count);
	}

	std::vector<texas::MultiwayPublicStateDescriptor> empty_public_states;
	std::vector<texas::MultiwaySparseRowShape> empty_rows;
	std::vector<std::size_t> empty_cursors;
	texas::MultiwayAdmissionDiagnostics empty_diagnostics;
	texas::solver::multiway::detail::merge_multiway_discovery_streams(
		{ &empty }, empty_public_states, empty_rows, empty_cursors, &empty_diagnostics);
	EXPECT_TRUE(empty_public_states.empty());
	EXPECT_TRUE(empty_rows.empty());
	EXPECT_EQ(empty_cursors.size(), 1U);
	EXPECT_EQ(empty_cursors.front(), 0U);
	EXPECT_EQ(empty_diagnostics.discovery_one_stream_fast_path_uses, 1U);
}

TEST_CASE(multiway_solver_discovery_merge_diagnostics_commit_only_after_both_merges)
{
	texas::MultiwayDiscoveryStream first(8U);
	texas::MultiwayDiscoveryStream second(8U);
	const auto child = checked_action_child_public_state();
	EXPECT_TRUE(first.try_append_public_state(child));
	EXPECT_TRUE(second.try_append_public_state(child));
	EXPECT_TRUE(first.try_append_row(root_row(1U, 3U)));
	EXPECT_TRUE(second.try_append_row(root_row(2U, 3U)));
	first.prepare_for_merge();
	second.prepare_for_merge();

	const std::vector<const texas::MultiwayDiscoveryStream*> streams = { &first, &second };
	std::vector<texas::MultiwayPublicStateDescriptor> public_states;
	std::vector<texas::MultiwaySparseRowShape> rows;
	std::vector<std::size_t> cursors;
	texas::MultiwayAdmissionDiagnostics diagnostics;
	diagnostics.discovery_public_state_merge_nanoseconds = 77U;
	diagnostics.discovery_row_merge_nanoseconds = 77U;
	diagnostics.discovery_public_state_source_elements = 77U;
	diagnostics.discovery_row_source_elements = 77U;
	diagnostics.discovery_public_state_duplicate_candidates = 77U;
	diagnostics.discovery_row_duplicate_candidates = 77U;
	diagnostics.discovery_single_stream_batches = 77U;

	EXPECT_THROW(texas::solver::multiway::detail::merge_multiway_discovery_streams(
					 streams, public_states, rows, cursors, &diagnostics),
		std::invalid_argument);
	EXPECT_EQ(diagnostics.discovery_public_state_merge_nanoseconds, 77U);
	EXPECT_EQ(diagnostics.discovery_row_merge_nanoseconds, 77U);
	EXPECT_EQ(diagnostics.discovery_public_state_source_elements, 77U);
	EXPECT_EQ(diagnostics.discovery_row_source_elements, 77U);
	EXPECT_EQ(diagnostics.discovery_public_state_duplicate_candidates, 77U);
	EXPECT_EQ(diagnostics.discovery_row_duplicate_candidates, 77U);
	EXPECT_EQ(diagnostics.discovery_single_stream_batches, 77U);
}

TEST_CASE(multiway_solver_discovery_parent_order_rejects_cycles_and_unresolved_parents)
{
	texas::MultiwaySolverCoordinator coordinator(valid_request());
	texas::MultiwayAdmissionDiagnostics diagnostics;
	texas::solver::multiway::detail::MultiwayDiscoveryParentOrderScratch scratch;

	std::vector<texas::MultiwayPublicStateDescriptor> ordered(2U);
	ordered[0].id = { 500U };
	ordered[0].parent_id = root_public_state().id;
	ordered[1].id = { 501U };
	ordered[1].parent_id = ordered[0].id;
	texas::solver::multiway::detail::build_multiway_parent_first_order(ordered, coordinator, diagnostics, scratch);
	EXPECT_EQ(scratch.order.size(), 2U);
	EXPECT_EQ(scratch.order[0], 0U);
	EXPECT_EQ(scratch.order[1], 1U);
	EXPECT_EQ(scratch.depths[0], 1U);
	EXPECT_EQ(scratch.depths[1], 2U);

	std::vector<texas::MultiwayPublicStateDescriptor> unresolved(1U);
	unresolved[0].id = { 600U };
	unresolved[0].parent_id = { std::numeric_limits<std::uint64_t>::max() };
	EXPECT_THROW(texas::solver::multiway::detail::build_multiway_parent_first_order(unresolved, coordinator, diagnostics, scratch), std::invalid_argument);

	std::vector<texas::MultiwayPublicStateDescriptor> cycle(2U);
	cycle[0].id = { std::numeric_limits<std::uint64_t>::max() - 2U };
	cycle[0].parent_id = { std::numeric_limits<std::uint64_t>::max() - 1U };
	cycle[1].id = { std::numeric_limits<std::uint64_t>::max() - 1U };
	cycle[1].parent_id = { std::numeric_limits<std::uint64_t>::max() - 2U };
	EXPECT_THROW(texas::solver::multiway::detail::build_multiway_parent_first_order(cycle, coordinator, diagnostics, scratch), std::invalid_argument);
}

TEST_CASE(multiway_solver_merge_rejects_missing_misindexed_or_unsorted_worker_streams)
{
	texas::MultiwaySolverCoordinator coordinator(valid_request());
	coordinator.admit_infoset_row(root_row());
	texas::MultiwayWorkerDeltaStream only_worker(0, 4);
	only_worker.sort_fixed_order();
	EXPECT_THROW(coordinator.merge_worker_streams({ only_worker }), std::invalid_argument);

	texas::MultiwayWorkerDeltaStream misplaced(1, 4);
	texas::MultiwayWorkerDeltaStream worker_one(1, 4);
	misplaced.sort_fixed_order();
	worker_one.sort_fixed_order();
	EXPECT_THROW(coordinator.merge_worker_streams({ misplaced, worker_one }), std::invalid_argument);

	texas::MultiwayWorkerDeltaStream unsorted(0, 4);
	unsorted.try_append(delta(2, 0.0, 1.0));
	unsorted.try_append(delta(0, 0.0, 1.0));
	texas::MultiwayWorkerDeltaStream sorted(1, 4);
	sorted.sort_fixed_order();
	EXPECT_THROW(coordinator.merge_worker_streams({ unsorted, sorted }), std::invalid_argument);
}

TEST_CASE(multiway_solver_merge_is_transactional_when_a_late_cell_is_invalid)
{
	texas::MultiwaySolverCoordinator coordinator(valid_request());
	coordinator.admit_infoset_row(root_row());
	texas::MultiwayWorkerDeltaStream first(0, 2);
	texas::MultiwayWorkerDeltaStream second(1, 2);
	EXPECT_TRUE(first.try_append(delta(0, 1.0, 4.0, 1U)));
	auto invalid = delta(3, 1.0, 1.0, 2U);
	EXPECT_TRUE(second.try_append(invalid));
	first.sort_fixed_order();
	second.sort_fixed_order();

	EXPECT_THROW(coordinator.merge_worker_streams({ first, second }), std::invalid_argument);
	const auto policy = coordinator.export_root_policy();
	for (const auto& action : policy.actions)
	{
		EXPECT_NEAR(action.probability, 1.0 / 3.0, 1e-12);
	}
	EXPECT_EQ(coordinator.diagnostics().worker_delta_entries_merged, 0U);
}

TEST_CASE(multiway_solver_worker_partitions_merge_identical_trajectory_ids_identically)
{
	const auto merged_policy = [](std::size_t workers) {
		auto limits = valid_limits();
		limits.worker_count = static_cast<std::uint32_t>(workers);
		limits.max_worker_delta_entries = 12U;
		texas::MultiwayCFRConfig cfr;
		cfr.player_count = 2;
		texas::MultiwaySolverCoordinator coordinator(
			texas::MultiwaySolveRequest(valid_root(), cfr, limits));
		coordinator.admit_infoset_row(root_row());
		std::vector<texas::MultiwayWorkerDeltaStream> streams;
		streams.reserve(workers);
		for (std::size_t worker = 0; worker < workers; ++worker)
		{
			streams.emplace_back(worker, 12U);
		}
		for (std::uint64_t trajectory = 0; trajectory < 12U; ++trajectory)
		{
			const auto worker = static_cast<std::size_t>(trajectory % workers);
			EXPECT_TRUE(streams[worker].try_append(delta(
				static_cast<std::uint8_t>(trajectory % 3U),
				static_cast<double>(trajectory + 1U),
				static_cast<double>(trajectory + 2U),
				trajectory)));
		}
		for (auto& stream : streams)
			stream.sort_fixed_order();
		coordinator.merge_worker_streams(streams);
		return coordinator.export_root_policy();
	};

	const auto baseline = merged_policy(1U);
	for (std::size_t workers = 2U; workers <= 6U; ++workers)
	{
		const auto policy = merged_policy(workers);
		EXPECT_EQ(policy.actions.size(), baseline.actions.size());
		for (std::size_t action = 0; action < policy.actions.size(); ++action)
		{
			EXPECT_NEAR(policy.actions[action].probability, baseline.actions[action].probability, 1e-12);
		}
	}
}

TEST_CASE(multiway_solver_merge_is_fixed_order_and_exports_only_root_policy)
{
	texas::MultiwaySolverCoordinator coordinator(valid_request());
	coordinator.admit_infoset_row(root_row());
	texas::MultiwayWorkerDeltaStream first(0, 4);
	texas::MultiwayWorkerDeltaStream second(1, 4);
	first.try_append(delta(2, -1.0, 2.0, 3));
	first.try_append(delta(0, 1.0, 1.0, 1));
	second.try_append(delta(1, 2.0, 3.0, 2));
	first.sort_fixed_order();
	second.sort_fixed_order();
	coordinator.merge_worker_streams({ first, second });

	const auto policy = coordinator.export_root_policy();
	EXPECT_EQ(policy.public_state, root_public_state().id);
	EXPECT_EQ(policy.infoset.seat, 0);
	EXPECT_EQ(policy.actions.size(), std::size_t{ 3 });
	EXPECT_NEAR(policy.actions[0].probability, 1.0 / 6.0, 1e-12);
	EXPECT_NEAR(policy.actions[1].probability, 0.5, 1e-12);
	EXPECT_NEAR(policy.actions[2].probability, 1.0 / 3.0, 1e-12);
	EXPECT_EQ(coordinator.diagnostics().worker_delta_entries_merged, 3U);
}

TEST_CASE(multiway_solver_preallocates_and_reuses_bounded_merge_scratch)
{
	texas::MultiwaySolverCoordinator coordinator(valid_request());
	coordinator.admit_infoset_row(root_row());
	EXPECT_EQ(coordinator.merge_scratch_capacity(), 8U);

	for (std::uint64_t trajectory = 0U; trajectory < 2U; ++trajectory)
	{
		texas::MultiwayWorkerDeltaStream first(0U, 4U);
		texas::MultiwayWorkerDeltaStream second(1U, 4U);
		EXPECT_TRUE(first.try_append(delta(0U, 1.0, 1.0, trajectory * 2U)));
		EXPECT_TRUE(second.try_append(delta(1U, 1.0, 1.0, trajectory * 2U + 1U)));
		first.sort_fixed_order();
		second.sort_fixed_order();
		coordinator.merge_worker_streams({ first, second });
		EXPECT_EQ(coordinator.merge_scratch_capacity(), 8U);
	}
}

TEST_CASE(multiway_solver_delta_merge_selects_tournament_only_above_threshold)
{
	using texas::solver::multiway::MULTIWAY_DELTA_TOURNAMENT_STREAM_THRESHOLD;
	using texas::solver::multiway::MultiwayDeltaMergePath;
	EXPECT_EQ(
		texas::solver::multiway::select_multiway_delta_merge_path(MULTIWAY_DELTA_TOURNAMENT_STREAM_THRESHOLD),
		MultiwayDeltaMergePath::Reference);
	EXPECT_EQ(
		texas::solver::multiway::select_multiway_delta_merge_path(MULTIWAY_DELTA_TOURNAMENT_STREAM_THRESHOLD + 1U),
		MultiwayDeltaMergePath::Tournament);
}

TEST_CASE(multiway_solver_reference_and_tournament_delta_merges_are_bitwise_identical)
{
	constexpr std::size_t worker_count = 6U;
	auto limits = valid_limits();
	limits.worker_count = static_cast<std::uint32_t>(worker_count);
	limits.max_worker_delta_entries = 8U;
	texas::MultiwayCFRConfig cfr;
	cfr.player_count = 2;
	texas::MultiwaySolverCoordinator reference(
		texas::MultiwaySolveRequest(valid_root(), cfr, limits));
	texas::MultiwaySolverCoordinator tournament(
		texas::MultiwaySolveRequest(valid_root(), cfr, limits));
	reference.admit_infoset_row(root_row());
	tournament.admit_infoset_row(root_row());
	auto streams = make_differential_delta_streams(worker_count);
	reference.merge_worker_streams(streams, MultiwayDeltaMergePath::Reference);
	tournament.merge_worker_streams(streams, MultiwayDeltaMergePath::Tournament);
	expect_checkpoints_equal(reference.checkpoint(), tournament.checkpoint());
	EXPECT_EQ(
		reference.diagnostics().worker_delta_entries_merged,
		tournament.diagnostics().worker_delta_entries_merged);
	EXPECT_EQ(
		reference.diagnostics().last_merged_stream_fingerprint,
		tournament.diagnostics().last_merged_stream_fingerprint);
	const auto reference_policy = reference.export_root_policy();
	const auto tournament_policy = tournament.export_root_policy();
	EXPECT_EQ(reference_policy.actions.size(), tournament_policy.actions.size());
	for (std::size_t action = 0U; action < reference_policy.actions.size(); ++action)
	{
		EXPECT_EQ(reference_policy.actions[action].action, tournament_policy.actions[action].action);
		EXPECT_EQ(reference_policy.actions[action].probability, tournament_policy.actions[action].probability);
	}
}

TEST_CASE(multiway_solver_tournament_delta_merge_handles_empty_and_singleton_streams)
{
	constexpr std::size_t worker_count = 6U;
	auto limits = valid_limits();
	limits.worker_count = static_cast<std::uint32_t>(worker_count);
	limits.max_worker_delta_entries = 2U;
	texas::MultiwayCFRConfig cfr;
	cfr.player_count = 2;
	std::vector<texas::MultiwayWorkerDeltaStream> empty_streams;
	empty_streams.reserve(worker_count);
	for (std::size_t worker = 0U; worker < worker_count; ++worker)
	{
		empty_streams.emplace_back(worker, 2U);
		empty_streams.back().sort_fixed_order();
	}
	texas::MultiwaySolverCoordinator empty(
		texas::MultiwaySolveRequest(valid_root(), cfr, limits));
	empty.merge_worker_streams(empty_streams, MultiwayDeltaMergePath::Tournament);
	EXPECT_EQ(empty.diagnostics().worker_delta_entries_merged, 0U);

	std::vector<texas::MultiwayWorkerDeltaStream> singleton_streams;
	singleton_streams.reserve(worker_count);
	for (std::size_t worker = 0U; worker < worker_count; ++worker)
	{
		singleton_streams.emplace_back(worker, 2U);
		if (worker == 4U)
			EXPECT_TRUE(singleton_streams.back().try_append(delta(1U, 2.0, 3.0, 17U)));
		singleton_streams.back().sort_fixed_order();
	}
	texas::MultiwaySolverCoordinator singleton(
		texas::MultiwaySolveRequest(valid_root(), cfr, limits));
	singleton.admit_infoset_row(root_row());
	singleton.merge_worker_streams(singleton_streams, MultiwayDeltaMergePath::Tournament);
	EXPECT_EQ(singleton.diagnostics().worker_delta_entries_merged, 1U);
	EXPECT_NEAR(singleton.export_root_policy().actions[1].probability, 1.0, 1e-12);
}

TEST_CASE(multiway_solver_reference_and_tournament_delta_merges_reject_identical_trajectory_duplicates)
{
	constexpr std::size_t worker_count = 6U;
	auto limits = valid_limits();
	limits.worker_count = static_cast<std::uint32_t>(worker_count);
	limits.max_worker_delta_entries = 2U;
	texas::MultiwayCFRConfig cfr;
	cfr.player_count = 2;
	std::vector<texas::MultiwayWorkerDeltaStream> streams;
	streams.reserve(worker_count);
	for (std::size_t worker = 0U; worker < worker_count; ++worker)
	{
		streams.emplace_back(worker, 2U);
		if (worker < 2U)
			EXPECT_TRUE(streams.back().try_append(delta(0U, 1.0, 1.0, 42U)));
		streams.back().sort_fixed_order();
	}
	for (const auto path : { MultiwayDeltaMergePath::Reference, MultiwayDeltaMergePath::Tournament })
	{
		texas::MultiwaySolverCoordinator coordinator(
			texas::MultiwaySolveRequest(valid_root(), cfr, limits));
		coordinator.admit_infoset_row(root_row());
		EXPECT_THROW(coordinator.merge_worker_streams(streams, path), std::invalid_argument);
		EXPECT_EQ(coordinator.diagnostics().worker_delta_entries_merged, 0U);
	}
}

TEST_CASE(multiway_solver_tournament_delta_merge_preserves_compact_storage_and_float64_overflow)
{
	constexpr std::size_t worker_count = 6U;
	auto limits = valid_limits();
	limits.worker_count = static_cast<std::uint32_t>(worker_count);
	limits.max_worker_delta_entries = 8U;
	limits.storage_backend = texas::MultiwaySolverLimits::StorageBackend::CompactInt32;
	texas::MultiwayCFRConfig cfr;
	cfr.player_count = 2;
	texas::MultiwaySolverCoordinator compact(
		texas::MultiwaySolveRequest(valid_root(), cfr, limits));
	compact.admit_infoset_row(root_row());
	auto streams = make_differential_delta_streams(worker_count);
	compact.merge_worker_streams(streams, MultiwayDeltaMergePath::Tournament);
	const auto checkpoint = compact.checkpoint();
	EXPECT_TRUE(compact.compact_storage() != nullptr);
	EXPECT_TRUE(!checkpoint.storage.compact_regrets.empty());

	auto overflow_limits = limits;
	overflow_limits.storage_backend = texas::MultiwaySolverLimits::StorageBackend::Float64Reference;
	overflow_limits.max_worker_delta_entries = 2U;
	texas::MultiwaySolverCoordinator overflow(
		texas::MultiwaySolveRequest(valid_root(), cfr, overflow_limits));
	overflow.admit_infoset_row(root_row());
	std::vector<texas::MultiwayWorkerDeltaStream> overflow_streams;
	overflow_streams.reserve(worker_count);
	for (std::size_t worker = 0U; worker < worker_count; ++worker)
	{
		overflow_streams.emplace_back(worker, 2U);
		if (worker == 0U)
		{
			const auto large = std::ldexp(1.0, 53);
			EXPECT_TRUE(overflow_streams.back().try_append(delta(0U, large, large, 0U)));
			EXPECT_TRUE(overflow_streams.back().try_append(delta(0U, 1.0, 1.0, 1U)));
		}
		overflow_streams.back().sort_fixed_order();
	}
	EXPECT_THROW(
		overflow.merge_worker_streams(overflow_streams, MultiwayDeltaMergePath::Tournament),
		std::overflow_error);
}

TEST_CASE(multiway_solver_tournament_delta_merge_reuses_preallocated_heap_scratch)
{
	constexpr std::size_t worker_count = 6U;
	auto limits = valid_limits();
	limits.worker_count = static_cast<std::uint32_t>(worker_count);
	limits.max_worker_delta_entries = 2U;
	texas::MultiwayCFRConfig cfr;
	cfr.player_count = 2;
	texas::MultiwaySolverCoordinator coordinator(
		texas::MultiwaySolveRequest(valid_root(), cfr, limits));
	coordinator.admit_infoset_row(root_row());
	const auto capacity = coordinator.merge_tournament_scratch_capacity();
	EXPECT_EQ(capacity, worker_count);
	for (std::uint64_t trajectory = 0U; trajectory < 2U; ++trajectory)
	{
		auto streams = make_differential_delta_streams(worker_count);
		for (auto& stream : streams)
			stream.rewind(0U);
		streams[trajectory].try_append(delta(trajectory % 3U, 1.0, 1.0, 100U + trajectory));
		for (auto& stream : streams)
			stream.sort_fixed_order();
		coordinator.merge_worker_streams(streams, MultiwayDeltaMergePath::Tournament);
		EXPECT_EQ(coordinator.merge_tournament_scratch_capacity(), capacity);
	}
}

TEST_CASE(multiway_solver_root_export_is_uniform_until_its_sparse_row_is_admitted)
{
	texas::MultiwaySolverCoordinator coordinator(valid_request());
	const auto policy = coordinator.export_root_policy();
	EXPECT_EQ(policy.actions.size(), std::size_t{ 3 });
	for (const auto& action : policy.actions)
	{
		EXPECT_NEAR(action.probability, 1.0 / 3.0, 1e-12);
	}
}

TEST_CASE(multiway_solver_row_admission_rejects_a_shape_that_disagrees_with_root_actions)
{
	texas::MultiwaySolverCoordinator coordinator(valid_request());
	EXPECT_THROW(coordinator.admit_infoset_row(root_row(1, 2)), std::invalid_argument);
}

TEST_CASE(multiway_solver_result_copies_root_values_and_diagnostics)
{
	texas::MultiwayRootPolicy policy;
	policy.public_state = { 1 };
	std::vector<texas::Value> values = { 1.0, -1.0 };
	texas::MultiwaySolveDiagnostics diagnostics;
	diagnostics.batches_completed = 4;
	const texas::MultiwaySolveResult result(policy, values, diagnostics);
	values[0] = 99.0;
	diagnostics.batches_completed = 0;

	EXPECT_NEAR(result.root_values()[0], 1.0, 1e-12);
	EXPECT_EQ(result.diagnostics().batches_completed, 4U);
}
