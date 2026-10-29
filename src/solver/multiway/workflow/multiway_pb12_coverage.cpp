#include "solver/multiway/workflow/multiway_pb12_coverage.hpp"

#include "games/multiway_replay.hpp"
#include "solver/multiway/abstraction/multiway_action_abstraction.hpp"
#include "solver/multiway/abstraction/multiway_public_builder.hpp"
#include "solver/multiway/blueprint/multiway_artifact.hpp"
#include "solver/multiway/blueprint/multiway_blueprint_policy_provider.hpp"
#include "solver/multiway/blueprint/multiway_blueprint_store.hpp"
#include "solver/multiway/engine/multiway_compact_storage.hpp"
#include "solver/multiway/engine/multiway_external_storage.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <stdexcept>
#include <utility>
#include <vector>

namespace texas::solver::multiway
{
	namespace
	{
		struct CapturedAction
		{
			PlayerId actor;
			games::multiway::MultiwayAction action;
			int target;
			std::uint64_t menu_id;
		};

		constexpr std::array<CapturedAction, 7U> kPrefix = { {
			{ 0, games::multiway::MultiwayAction::Bet, 450, 16356999006715591126ULL },
			{ 1, games::multiway::MultiwayAction::Call, 450, 202585340531706514ULL },
			{ 2, games::multiway::MultiwayAction::Raise, 1350, 202585340531706514ULL },
			{ 3, games::multiway::MultiwayAction::Raise, 2970, 2523384831558200016ULL },
			{ 4, games::multiway::MultiwayAction::Call, 2970, 7917889658074927682ULL },
			{ 5, games::multiway::MultiwayAction::Raise, 6534, 4747676475899684576ULL },
			{ 0, games::multiway::MultiwayAction::AllIn, 10000, 17948765423955528398ULL },
		} };

		constexpr std::array<std::array<CapturedAction, 4U>, 2U> kBranches = { {
			{ {
				{ 1, games::multiway::MultiwayAction::Fold, 450, 17524687290575650146ULL },
				{ 2, games::multiway::MultiwayAction::Fold, 1350, 16789259325962222034ULL },
				{ 3, games::multiway::MultiwayAction::Call, 10000, 14734177828830091388ULL },
				{ 4, games::multiway::MultiwayAction::Call, 10000, 14734177828830091388ULL },
			} },
			{ {
				{ 1, games::multiway::MultiwayAction::Call, 10000, 17524687290575650146ULL },
				{ 2, games::multiway::MultiwayAction::Call, 10000, 16789259325962222034ULL },
				{ 3, games::multiway::MultiwayAction::Call, 10000, 14734177828830091388ULL },
				{ 4, games::multiway::MultiwayAction::Fold, 2970, 14734177828830091388ULL },
			} },
		} };

		struct CoverageTree
		{
			std::vector<MultiwayPublicStateDescriptor> admission_order;
			std::array<MultiwayPublicStateDescriptor, MULTIWAY_PB12_COVERAGE_KEYS.size()> targets{};
		};

		MultiwayPublicStateDescriptor advance(
			const MultiwayPublicStateDescriptor& parent,
			const CapturedAction& captured,
			const MultiwayActionAbstraction& abstraction)
		{
			if (parent.betting.current_player != captured.actor || parent.legal_actions.empty()
				|| parent.legal_actions.front().action_menu_id != captured.menu_id)
			{
				throw std::invalid_argument("captured PB-12 history actor or menu does not match rebuilt state");
			}
			const auto action = std::find_if(parent.legal_actions.begin(), parent.legal_actions.end(),
				[&](const auto& candidate) {
					return candidate.action == captured.action
						&& candidate.target_street_contribution == captured.target;
				});
			if (action == parent.legal_actions.end())
				throw std::invalid_argument("captured PB-12 action is absent from rebuilt menu");

			const auto action_index = static_cast<std::uint32_t>(
				std::distance(parent.legal_actions.begin(), action));
			const auto child_betting = games::multiway::MultiwayState::from_snapshot(parent.betting)
										   .apply(captured.action, captured.target)
										   .snapshot();
			const auto child_actions = abstraction.make_legal_actions(child_betting);
			return MultiwayPublicBuilder::make_action_child(
				parent, action_index, child_betting, child_actions);
		}

		void append_history_action(
			games::multiway::MultiwayHandHistory& history,
			const CapturedAction& action)
		{
			games::multiway::MultiwayReplayEvent event;
			event.kind = games::multiway::MultiwayReplayEventKind::Decision;
			event.decision.acting_seat = action.actor;
			event.decision.action = action.action;
			event.decision.target_street_contribution = action.target;
			history.events.push_back(std::move(event));
		}

		games::multiway::MultiwayHandHistory make_history(std::size_t branch)
		{
			const auto rules = MultiwayGameRules::standard_6max();
			auto history = games::multiway::MultiwayHandHistory::from_rules(rules, 0, 529U);
			for (const auto& action : kPrefix)
				append_history_action(history, action);
			for (const auto& action : kBranches.at(branch))
				append_history_action(history, action);
			history.validate();
			return history;
		}

		CoverageTree build_tree(
			const MultiwayRootSnapshot& root,
			const MultiwayActionAbstraction& abstraction)
		{
			CoverageTree result;
			for (std::size_t branch = 0U; branch < kBranches.size(); ++branch)
			{
				auto current = root.public_state;
				for (const auto& action : kPrefix)
				{
					current = advance(current, action, abstraction);
					result.admission_order.push_back(current);
				}
				for (std::size_t step = 0U; step < kBranches[branch].size(); ++step)
				{
					current = advance(current, kBranches[branch][step], abstraction);
					const auto key_index = branch * kBranches[branch].size() + step;
					const auto& key = MULTIWAY_PB12_COVERAGE_KEYS[key_index];
					if (current.id.value != key.public_state_id
						|| current.betting.current_player != key.seat
						|| current.betting.street != core::Street::Preflop
						|| current.legal_actions.empty()
						|| current.legal_actions.front().action_menu_id != key.action_menu_id
						|| key.bucket >= 169U)
					{
						throw std::invalid_argument("captured PB-12 key does not match rebuilt history");
					}
					result.targets[key_index] = current;
					result.admission_order.push_back(current);
				}
			}

			std::vector<MultiwayPublicStateDescriptor> unique_order;
			unique_order.reserve(result.admission_order.size());
			for (const auto& state : result.admission_order)
			{
				const auto previous = std::find_if(unique_order.begin(), unique_order.end(),
					[&](const auto& candidate) { return candidate.id == state.id; });
				if (previous == unique_order.end())
					unique_order.push_back(state);
				else if (!MultiwaySolverCoordinator::public_state_candidates_compatible(*previous, state))
					throw std::invalid_argument("captured PB-12 branches disagree on a shared public state");
			}
			result.admission_order = std::move(unique_order);
			return result;
		}

		void verify_admission(
			MultiwaySolverCoordinator& coordinator,
			const CoverageTree& tree)
		{
			auto lease = coordinator.acquire_export_lease();
			const auto* external = lease.external_storage();
			const auto* compact = lease.compact_storage();
			if ((external == nullptr) == (compact == nullptr))
				throw std::logic_error("PB-12 targeted coverage requires a compact storage backend");

			for (std::size_t index = 0U; index < tree.targets.size(); ++index)
			{
				const auto& state = tree.targets[index];
				const auto& key = MULTIWAY_PB12_COVERAGE_KEYS[index];
				const bool state_exists = external != nullptr
					? external->catalog().contains(state.id)
					: lease.public_catalog().contains(state.id);
				if (!state_exists)
					throw std::runtime_error("PB-12 target is absent from the public catalog after admission");
				if (external != nullptr)
				{
					std::array<MultiwayActionDescriptor, MULTIWAY_MAX_ABSTRACTED_ACTIONS> menu{};
					std::size_t menu_count = 0U;
					if (!external->catalog().lookup_menu(state.id, menu.data(), menu.size(), menu_count)
						|| menu_count != state.legal_actions.size() || menu_count == 0U
						|| menu.front().action_menu_id != key.action_menu_id)
					{
						throw std::runtime_error("PB-12 target menu is missing or mismatched after admission");
					}
					MultiwayExternalRowBinding binding{};
					if (!external->compact_storage().lookup_row({ state.id, key.seat }, binding)
						|| binding.public_state() != state.id || binding.seat() != key.seat
						|| binding.bucket_count() != 169U
						|| binding.action_count() != state.legal_actions.size()
						|| key.bucket >= binding.bucket_count())
					{
						throw std::runtime_error("PB-12 exact seat/bucket row is missing after admission");
					}
					continue;
				}

				const auto menu = lease.public_catalog().packed_menu(state.id);
				if (menu.actions == nullptr || menu.action_count != state.legal_actions.size()
					|| menu.action_menu_id != key.action_menu_id)
				{
					throw std::runtime_error("PB-12 target menu is missing or mismatched after admission");
				}
				const auto* row = compact->metadata({ state.id, key.seat });
				if (row == nullptr || row->shape.infoset != MultiwayInfosetId{ state.id, key.seat }
					|| row->shape.bucket_count != 169U
					|| row->shape.action_count != state.legal_actions.size()
					|| key.bucket >= row->shape.bucket_count)
				{
					throw std::runtime_error("PB-12 exact seat/bucket row is missing after admission");
				}
			}
		}
	} // namespace

	MultiwayPB12CoverageAdmission admit_multiway_pb12_coverage(
		const MultiwayRootSnapshot& root,
		const MultiwayActionAbstraction& abstraction,
		MultiwaySolverCoordinator& coordinator)
	{
		if (coordinator.limits().storage_backend == MultiwaySolverLimits::StorageBackend::Float64Reference)
			throw std::invalid_argument("PB-12 targeted coverage requires a compact storage backend");
		const auto tree = build_tree(root, abstraction);
		std::vector<MultiwayPublicStateDescriptor> states = tree.admission_order;
		std::sort(states.begin(), states.end(), [](const auto& left, const auto& right) {
			return left.id < right.id;
		});

		std::vector<std::size_t> parent_order;
		parent_order.reserve(tree.admission_order.size());
		for (const auto& ordered : tree.admission_order)
		{
			const auto state = std::lower_bound(states.begin(), states.end(), ordered.id,
				[](const auto& candidate, MultiwayPublicStateId id) { return candidate.id < id; });
			if (state == states.end() || state->id != ordered.id)
				throw std::logic_error("PB-12 admission state order is inconsistent");
			parent_order.push_back(static_cast<std::size_t>(std::distance(states.begin(), state)));
		}

		std::vector<MultiwaySparseRowShape> rows;
		rows.reserve(tree.targets.size());
		for (std::size_t index = 0U; index < tree.targets.size(); ++index)
		{
			const auto& state = tree.targets[index];
			const auto& key = MULTIWAY_PB12_COVERAGE_KEYS[index];
			rows.push_back({ { state.id, key.seat }, 169U,
				static_cast<std::uint8_t>(state.legal_actions.size()) });
		}
		std::sort(rows.begin(), rows.end(), [](const auto& left, const auto& right) {
			return left.infoset < right.infoset;
		});

		coordinator.admit_prepared_batch(states, parent_order, rows);
		verify_admission(coordinator, tree);
		return { tree.targets };
	}

	void verify_multiway_pb12_policy_file(
		const std::filesystem::path& policy_path,
		const MultiwayModelIdentity& identity)
	{
		auto artifact = MultiwayFullBlueprintArtifacts::load_verified(policy_path, identity);
		const MultiwayBlueprintStore store(identity, std::move(artifact.rows));
		const MultiwayBlueprintPolicyProvider provider(store);
		const auto rules = MultiwayGameRules::standard_6max();
		const MultiwayActionAbstraction abstraction;
		const auto initial = MultiwayState::initial(rules, 0).snapshot();
		const auto root = MultiwayPublicBuilder::make_root(
			initial, {}, abstraction.make_legal_actions(initial));
		MultiwayRootSnapshot root_snapshot;
		root_snapshot.public_state = root;
		const auto tree = build_tree(root_snapshot, abstraction);
		std::array<games::multiway::MultiwayHandHistory, 2U> histories = {
			make_history(0U), make_history(1U)
		};

		for (std::size_t index = 0U; index < tree.targets.size(); ++index)
		{
			const auto& key = MULTIWAY_PB12_COVERAGE_KEYS[index];
			const auto& target = tree.targets[index];
			const auto branch = index / kBranches[0U].size();
			const auto branch_step = index % kBranches[0U].size();
			auto prefix = histories[branch];
			prefix.events.resize(kPrefix.size() + branch_step + 1U);
			const auto replayed = games::multiway::replay_multiway_hand(prefix).snapshot();
			if (replayed.current_player != key.seat
				|| replayed.stacks != target.betting.stacks
				|| replayed.contributions != target.betting.contributions
				|| replayed.street_contributions != target.betting.street_contributions)
			{
				throw std::runtime_error("PB-12 policy verification replay diverged from captured public history");
			}

			std::array<Probability, MULTIWAY_MAX_ABSTRACTED_ACTIONS> strategy{};
			const auto status = provider.strategy_into(
				{ target.id, key.seat }, key.bucket, target.legal_actions.data(),
				target.legal_actions.size(), strategy.data());
			if (status != MultiwayBlueprintLookupStatus::Hit)
				throw std::runtime_error("PB-12 exported policy is missing an exact replay key");
			Probability total = 0.0;
			for (std::size_t action = 0U; action < target.legal_actions.size(); ++action)
				total += strategy[action];
			if (!std::isfinite(total) || std::abs(total - 1.0) > 1e-9)
				throw std::runtime_error("PB-12 exported policy strategy is not normalized");
		}
	}

} // namespace texas::solver::multiway
