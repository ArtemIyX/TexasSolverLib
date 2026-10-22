#pragma once

#include "core/atomic_publish.hpp"
#include "games/multiway_private.hpp"
#include "games/multiway_state.hpp"
#include "solver/multiway/engine/multiway_cfr.hpp"
#include "solver/multiway/engine/multiway_scheduler.hpp"

#include <cstddef>
#include <cstdint>
#include <array>
#include <deque>
#include <mutex>
#include <shared_mutex>
#include <optional>
#include <vector>

namespace texas::solver::multiway
{

	class MultiwayCompactStorage;

	class MultiwayTerminalAdapter;
	struct MultiwayAdmissionDiagnostics;

	// Stable numeric identities are assigned by the coordinator. Zero is reserved
	// as an invalid identity so an omitted id cannot become a storage key.
	struct MultiwayPublicStateId
	{
		std::uint64_t value = 0;

		constexpr bool operator==(const MultiwayPublicStateId& other) const noexcept
		{
			return value == other.value;
		}
		constexpr bool operator!=(const MultiwayPublicStateId& other) const noexcept
		{
			return !(*this == other);
		}
		constexpr bool operator<(const MultiwayPublicStateId& other) const noexcept
		{
			return value < other.value;
		}
	};

	struct MultiwayInfosetId
	{
		MultiwayPublicStateId public_state{};
		core::PlayerId seat = -1;

		constexpr bool operator==(const MultiwayInfosetId& other) const noexcept
		{
			return public_state == other.public_state && seat == other.seat;
		}
		constexpr bool operator!=(const MultiwayInfosetId& other) const noexcept
		{
			return !(*this == other);
		}
		constexpr bool operator<(const MultiwayInfosetId& other) const noexcept
		{
			if (seat != other.seat)
				return seat < other.seat;
			return public_state < other.public_state;
		}
	};

	struct MultiwayActionDescriptor
	{
		MultiwayAction action = MultiwayAction::Fold;
		std::uint32_t action_index = 0;
		// Exact acting-seat street contribution after this action. This is
		// required for every action, including Fold, Check, Call, and AllIn.
		int target_street_contribution = 0;
		std::uint64_t action_menu_id = 0;

		constexpr bool operator==(const MultiwayActionDescriptor& other) const noexcept
		{
			return action == other.action && action_index == other.action_index && target_street_contribution == other.target_street_contribution && action_menu_id == other.action_menu_id;
		}
		constexpr bool operator!=(const MultiwayActionDescriptor& other) const noexcept
		{
			return !(*this == other);
		}
	};

	struct MultiwayPublicHistoryEntry
	{
		core::PlayerId actor = -1;
		MultiwayActionDescriptor action{};

		constexpr bool operator==(const MultiwayPublicHistoryEntry& other) const noexcept
		{
			return actor == other.actor && action == other.action;
		}
	};

	// The known board and its deterministic chance boundary.  `remaining_board_cards`
	// prevents a root from silently relying on a caller-selected runout length.
	struct MultiwayBoardRunoutState
	{
		std::uint8_t remaining_board_cards = 2;
		bool chance_only_runout = false;

		constexpr bool operator==(const MultiwayBoardRunoutState& other) const noexcept
		{
			return remaining_board_cards == other.remaining_board_cards && chance_only_runout == other.chance_only_runout;
		}
		constexpr bool operator!=(const MultiwayBoardRunoutState& other) const noexcept
		{
			return !(*this == other);
		}
	};

	// Side-pot settlement follows the existing terminal layer's cyclic seat-id
	// order, beginning at `odd_chip_first_seat`.  Seat order for transitions is
	// intentionally separate from this legacy settlement convention.
	enum class MultiwayOddChipRule : std::uint8_t
	{
		AscendingSeatIdFromFirstSeat,
	};

	// A stable action abstraction is identified by its coordinator-assigned menu
	// id together with the version that produced its target sizes.
	struct MultiwayActionAbstractionIdentity
	{
		std::uint64_t menu_id = 0;
		std::uint64_t version = 0;

		constexpr bool operator==(const MultiwayActionAbstractionIdentity& other) const noexcept
		{
			return menu_id == other.menu_id && version == other.version;
		}
	};

	enum class MultiwayPublicParentEdgeKind : std::uint8_t
	{
		None,
		BettingAction,
		BoardChance,
		StreetTransition,
	};

	// Typed incoming edge for a coordinator-admitted public state. Only the
	// payload matching `kind` is consumed: action, dealt_card, or next board.
	struct MultiwayPublicParentEdge
	{
		MultiwayPublicParentEdgeKind kind = MultiwayPublicParentEdgeKind::None;
		MultiwayActionDescriptor action{};
		std::uint8_t dealt_card = 0;
		// A preflop-to-flop chance edge deals a canonical sorted three-card
		// combination. Turn and river edges contain one card.
		std::vector<std::uint8_t> dealt_cards;
		std::vector<std::uint8_t> transition_board;
	};

	// This descriptor is the canonical public object admitted by the coordinator.
	// It intentionally contains no private cards, ranges, or mutable policy data.
	struct MultiwayPublicStateDescriptor
	{
		MultiwayPublicStateId id{};
		MultiwayPublicStateId parent_id{};
		MultiwayPublicParentEdge incoming_edge{};
		std::uint64_t canonical_history_id = 0;
		MultiwayBettingSnapshot betting{};
		std::vector<std::uint8_t> board;
		MultiwayBoardRunoutState board_runout{};
		std::vector<MultiwayPublicHistoryEntry> history;
		std::vector<MultiwayActionDescriptor> legal_actions;
	};

	struct MultiwayPackedBetting
	{
		std::array<int, 6U> stacks{};
		std::array<int, 6U> contributions{};
		std::array<int, 6U> street_contributions{};
		std::array<std::uint8_t, 6U> folded{};
		std::array<std::uint8_t, 6U> all_in{};
		std::array<std::uint8_t, 6U> may_raise{};
		std::array<std::uint8_t, 6U> pending{};
		std::array<std::uint8_t, 6U> has_acted{};
		std::array<int, 6U> bet_faced_when_acted{};
		std::uint8_t seat_count = 0U;
		core::PlayerId current_player = -1;
		core::PlayerId last_aggressor = -1;
		int current_bet = 0;
		int last_full_raise_size = 0;
		int big_blind = 0;
		core::Street street = core::Street::Flop;
	};

	struct MultiwayPackedPublicNode
	{
		MultiwayPublicStateId id{};
		MultiwayPublicStateId parent_id{};
		std::uint64_t canonical_history_id = 0U;
		MultiwayPackedBetting betting{};
		std::array<std::uint8_t, 5U> board{};
		std::uint8_t board_count = 0U;
		MultiwayBoardRunoutState board_runout{};
		MultiwayPublicParentEdgeKind incoming_kind = MultiwayPublicParentEdgeKind::None;
		MultiwayActionDescriptor incoming_action{};
		core::PlayerId incoming_actor = -1;
		std::uint8_t dealt_card = 0U;
		std::array<std::uint8_t, 3U> dealt_cards{};
		std::uint8_t dealt_card_count = 0U;
		std::array<std::uint8_t, 5U> transition_board{};
		std::uint8_t transition_board_count = 0U;
		std::uint32_t legal_menu_handle = 0U;
	};

	// Append-only fixed-node public catalog. Dynamic descriptors are recreated
	// only at cold API/checkpoint boundaries.
	class MultiwayPublicCatalog
	{
	public:
		explicit MultiwayPublicCatalog(std::size_t max_nodes = 0U);

		void clear() noexcept;
		void admit(
			const MultiwayPublicStateDescriptor& state,
			std::uint64_t* index_insertion_nanoseconds = nullptr,
			MultiwayAdmissionDiagnostics* diagnostics = nullptr);
		void admit_batch(
			const std::vector<MultiwayPublicStateDescriptor>& states,
			const std::vector<std::uint8_t>& new_state_flags,
			std::uint64_t* index_insertion_nanoseconds = nullptr,
			std::uint64_t* menu_lookup_nanoseconds = nullptr,
			std::uint64_t* index_merge_nanoseconds = nullptr,
			MultiwayAdmissionDiagnostics* diagnostics = nullptr,
			const std::vector<std::uint64_t>* validated_menu_keys = nullptr,
			std::vector<std::uint32_t>* resolved_menu_handles = nullptr);
		[[nodiscard]] bool contains(MultiwayPublicStateId id) const noexcept;
		[[nodiscard]] bool reconstruct(MultiwayPublicStateId id, MultiwayPublicStateDescriptor& output) const;
		void export_descriptors(std::vector<MultiwayPublicStateDescriptor>& output) const;
		[[nodiscard]] std::size_t size() const noexcept { return nodes_.size(); }
		[[nodiscard]] std::size_t memory_bytes() const noexcept;

	private:
		friend class MultiwaySolverCoordinator;
		struct IndexEntry
		{
			MultiwayPublicStateId id{};
			std::size_t node_index = 0U;
		};
		struct MenuIndexEntry
		{
			std::uint64_t canonical_menu_id = 0U;
			std::uint32_t menu_handle = 0U;
		};
		struct PendingMenuIndexEntry
		{
			std::uint64_t canonical_menu_id = 0U;
			std::uint32_t pending_menu_index = 0U;
		};

		[[nodiscard]] std::size_t find_menu(const std::vector<MultiwayActionDescriptor>& menu) const noexcept;
		[[nodiscard]] std::size_t find_menu_by_key(
			const std::vector<MultiwayActionDescriptor>& menu,
			std::uint64_t canonical_menu_id,
			MultiwayAdmissionDiagnostics* diagnostics = nullptr,
			std::uint64_t* index_lookup_nanoseconds = nullptr,
			std::uint64_t* equality_nanoseconds = nullptr) const noexcept;
		[[nodiscard]] static std::uint64_t menu_key(const std::vector<MultiwayActionDescriptor>& menu) noexcept;
		void validate_menu_index_debug() const noexcept;
		void classify_new_states(
			const std::vector<MultiwayPublicStateDescriptor>& states,
			std::vector<std::uint8_t>& new_state_flags) const;
		[[nodiscard]] MultiwayPackedPublicNode pack_node(
			const MultiwayPublicStateDescriptor& state, std::uint32_t menu_handle) const noexcept;
		[[nodiscard]] const MultiwayPackedPublicNode* node(MultiwayPublicStateId id) const noexcept;

		std::size_t max_nodes_ = 0U;
		std::vector<MultiwayPackedPublicNode> nodes_;
		std::vector<IndexEntry> index_;
		std::vector<std::vector<MultiwayActionDescriptor>> menus_;
		std::vector<MenuIndexEntry> menu_index_;
		std::vector<std::vector<MultiwayActionDescriptor>> batch_pending_menus_;
		std::vector<std::uint64_t> batch_menu_keys_;
		std::vector<std::uint32_t> batch_menu_handles_;
		std::vector<std::uint32_t> batch_pending_menu_indexes_;
		std::vector<IndexEntry> batch_pending_index_;
		std::vector<IndexEntry> batch_merged_index_;
		std::vector<PendingMenuIndexEntry> batch_pending_menu_index_;
		std::vector<MenuIndexEntry> batch_merged_menu_index_;
	};

	// The complete public/private root needed by a future traversal. It is copied
	// into MultiwaySolveRequest and then exposed only through const accessors.
	struct MultiwayRootSnapshot
	{
		MultiwayPublicStateDescriptor public_state;
		MultiwayInfosetId root_infoset{};
		std::uint32_t root_bucket = 0;
		// Resolver roots use the canonical private-hand id instead of a model bucket.
		bool root_uses_exact_private_hand = false;
		std::vector<core::PlayerId> seat_order;
		// First actionable seat after a non-runout street transition.  This makes
		// future transition ownership deterministic instead of caller-selected.
		core::PlayerId next_street_first_seat = 0;
		core::PlayerId odd_chip_first_seat = -1;
		MultiwayOddChipRule odd_chip_rule = MultiwayOddChipRule::AscendingSeatIdFromFirstSeat;
		// Rake is immutable root/model metadata, including explicit zero rake.
		games::multiway::MultiwayRakePolicy rake_policy = games::multiway::MultiwayRakePolicy::explicit_zero();
		MultiwayPrivateConfig private_ranges{};
		std::uint64_t action_abstraction_version = 0;
		std::uint64_t leaf_model_version = 0;
		// Terminal settlement is chips. The adapter converts utility values once
		// at this root boundary; unsupported normalizations are rejected.
		MultiwayValueUnits value_units = MultiwayValueUnits::Chips;

		void validate() const;

		[[nodiscard]] std::uint64_t action_menu_id() const noexcept
		{
			return public_state.legal_actions.empty() ? 0 : public_state.legal_actions.front().action_menu_id;
		}
		[[nodiscard]] MultiwayActionAbstractionIdentity action_abstraction_identity() const noexcept
		{
			return { action_menu_id(), action_abstraction_version };
		}
		[[nodiscard]] std::uint64_t terminal_model_identity() const noexcept
		{
			return leaf_model_version ^ rake_policy.identity();
		}
	};

	struct MultiwaySolverLimits
	{
		enum class StorageBackend : std::uint8_t
		{
			Float64Reference,
			CompactInt32
		};
		std::uint64_t seed = 1;
		std::uint32_t worker_count = 1;
		std::uint32_t trajectories_per_batch = 1;
		std::size_t max_public_states = 0;
		std::size_t max_sparse_rows = 0;
		std::size_t max_sparse_values = 0;
		std::size_t max_worker_delta_entries = 0;
		std::uint32_t max_batches = 64;
		StorageBackend storage_backend = StorageBackend::Float64Reference;
		std::uint32_t compact_page_width = 16U;
		// Per-worker bounded discovery entries. Zero preserves callers that do
		// not enable immutable discovery epochs.
		std::size_t max_discovery_entries = 0;

		void validate() const;
	};

	// Immutable boundary input. Traversal must treat this request as a batch
	// snapshot and cannot mutate the caller's root, ranges, or policy metadata.
	class MultiwaySolveRequest
	{
	public:
		MultiwaySolveRequest(
			MultiwayRootSnapshot root,
			MultiwayCFRConfig cfr_config,
			MultiwaySolverLimits limits = {});

		[[nodiscard]] const MultiwayRootSnapshot& root() const noexcept { return root_; }
		[[nodiscard]] const MultiwayCFRConfig& cfr_config() const noexcept { return cfr_config_; }
		[[nodiscard]] const MultiwaySolverLimits& limits() const noexcept { return limits_; }
		[[nodiscard]] const MultiwayPrivateRangeFeasibilityResult& private_range_feasibility() const noexcept
		{
			return private_range_feasibility_;
		}
		[[nodiscard]] const MultiwayCompiledPrivateRanges& compiled_private_ranges() const noexcept
		{
			return *compiled_private_ranges_;
		}

	private:
		MultiwayRootSnapshot root_;
		MultiwayCFRConfig cfr_config_;
		MultiwaySolverLimits limits_;
		MultiwayPrivateRangeFeasibilityResult private_range_feasibility_{};
		std::optional<MultiwayCompiledPrivateRanges> compiled_private_ranges_;
	};

	struct MultiwaySparseRowShape
	{
		MultiwayInfosetId infoset{};
		std::uint32_t bucket_count = 0;
		std::uint8_t action_count = 0;
	};

	struct MultiwaySparseRowMetadata
	{
		MultiwaySparseRowShape shape{};
		std::size_t regret_offset = 0;
		std::size_t strategy_sum_offset = 0;

		[[nodiscard]] std::size_t value_count() const noexcept;
	};

	struct MultiwaySparseStorageCheckpoint
	{
		std::vector<MultiwaySparseRowShape> shapes;
		std::vector<double> regrets;
		std::vector<double> strategy_sums;
	};

	// Runtime-only checkpoint diagnostics. These fields are not serialized or
	// included in the checkpoint payload hash.
	struct MultiwayCheckpointTelemetry
	{
		std::uint64_t snapshot_lock_wait_nanoseconds = 0U;
		std::uint64_t public_catalog_export_nanoseconds = 0U;
		std::uint64_t compact_storage_export_nanoseconds = 0U;
		std::uint64_t canonicalization_nanoseconds = 0U;
		std::uint64_t validation_nanoseconds = 0U;
		std::uint64_t payload_hash_nanoseconds = 0U;
		std::uint64_t binary_encoding_write_nanoseconds = 0U;
		std::uint64_t stream_close_or_flush_nanoseconds = 0U;
		std::uint64_t atomic_publication_nanoseconds = 0U;
		core::AtomicPublishMode atomic_publication_mode = core::AtomicPublishMode::Unknown;
		std::uint64_t public_state_bytes = 0U;
		std::uint64_t row_shape_bytes = 0U;
		std::uint64_t regret_bytes = 0U;
		std::uint64_t strategy_sum_bytes = 0U;
		std::uint64_t late_window_baseline_bytes = 0U;

		void accumulate(const MultiwayCheckpointTelemetry& other) noexcept
		{
			snapshot_lock_wait_nanoseconds += other.snapshot_lock_wait_nanoseconds;
			public_catalog_export_nanoseconds += other.public_catalog_export_nanoseconds;
			compact_storage_export_nanoseconds += other.compact_storage_export_nanoseconds;
			canonicalization_nanoseconds += other.canonicalization_nanoseconds;
			validation_nanoseconds += other.validation_nanoseconds;
			payload_hash_nanoseconds += other.payload_hash_nanoseconds;
			binary_encoding_write_nanoseconds += other.binary_encoding_write_nanoseconds;
			stream_close_or_flush_nanoseconds += other.stream_close_or_flush_nanoseconds;
			atomic_publication_nanoseconds += other.atomic_publication_nanoseconds;
			if (other.atomic_publication_mode != core::AtomicPublishMode::Unknown)
				atomic_publication_mode = other.atomic_publication_mode;
			public_state_bytes = public_state_bytes > other.public_state_bytes ? public_state_bytes : other.public_state_bytes;
			row_shape_bytes = row_shape_bytes > other.row_shape_bytes ? row_shape_bytes : other.row_shape_bytes;
			regret_bytes = regret_bytes > other.regret_bytes ? regret_bytes : other.regret_bytes;
			strategy_sum_bytes = strategy_sum_bytes > other.strategy_sum_bytes ? strategy_sum_bytes : other.strategy_sum_bytes;
			late_window_baseline_bytes = late_window_baseline_bytes > other.late_window_baseline_bytes ? late_window_baseline_bytes : other.late_window_baseline_bytes;
		}
	};

	struct MultiwayCoordinatorCheckpoint
	{
		std::vector<MultiwayPublicStateDescriptor> public_states;
		MultiwaySparseStorageCheckpoint storage;
		std::uint64_t terminal_visits = 0U;
		std::uint64_t leaf_visits = 0U;
		std::uint64_t missing_lookup_requests = 0U;
		std::uint64_t last_merged_stream_fingerprint = 0U;
		std::array<std::uint64_t, 4> street_visits{};
		MultiwayCheckpointTelemetry telemetry{};
	};

	// Coordinator-owned sparse rows. Values are action-major: [action][bucket].
	// Persistent accumulators are Float64. Merges use fixed global delta order and
	// reject a nonzero delta that a Float64 accumulator cannot represent, rather
	// than silently losing long-run regret or average-strategy mass.
	// Only the coordinator receives a mutable instance; workers emit deltas.
	class MultiwaySparseRowStorage
	{
	public:
		MultiwaySparseRowStorage(std::size_t max_rows, std::size_t max_values);

		[[nodiscard]] bool has_row(MultiwayInfosetId infoset) const noexcept;
		[[nodiscard]] const MultiwaySparseRowMetadata* metadata(MultiwayInfosetId infoset) const noexcept;
		[[nodiscard]] std::vector<Probability> average_strategy(
			MultiwayInfosetId infoset,
			std::uint32_t bucket) const;
		[[nodiscard]] std::vector<Probability> regret_matched_strategy(
			MultiwayInfosetId infoset,
			std::uint32_t bucket) const;
		void regret_matched_strategy_into(
			MultiwayInfosetId infoset,
			std::uint32_t bucket,
			Probability* output,
			std::size_t output_size) const;
		[[nodiscard]] bool action_below_regret(
			MultiwayInfosetId infoset, std::uint32_t bucket, std::uint8_t action,
			double threshold) const noexcept;
		[[nodiscard]] std::vector<double> strategy_sums(
			MultiwayInfosetId infoset,
			std::uint32_t bucket) const;
		void scale_regrets(double factor);
		[[nodiscard]] std::size_t scale_and_prune_regrets(
			double factor, double threshold, double regret_floor) noexcept;
		[[nodiscard]] std::size_t prune_negative_regrets(
			double threshold = 0.0, double regret_floor = 0.0) noexcept;
		void admit_rows(
			const std::vector<MultiwaySparseRowShape>& shapes,
			const std::vector<std::uint8_t>& new_row_flags);
		[[nodiscard]] std::size_t row_count() const noexcept { return metadata_.size(); }
		[[nodiscard]] std::size_t value_count() const noexcept { return regret_.size(); }
		[[nodiscard]] std::size_t value_capacity_limit() const noexcept { return max_values_; }
		[[nodiscard]] const std::vector<MultiwaySparseRowMetadata>& rows() const noexcept { return metadata_; }

	private:
		friend class MultiwaySolverCoordinator;

		void admit_row(const MultiwaySparseRowShape& shape);
		void apply_delta(
			MultiwayInfosetId infoset,
			std::uint32_t bucket,
			std::uint8_t action,
			double regret,
			double strategy_sum);

		std::size_t max_rows_ = 0;
		std::size_t max_values_ = 0;
		std::vector<MultiwaySparseRowMetadata> metadata_;
		std::vector<double> regret_;
		std::vector<double> strategy_sum_;
	};

	struct MultiwayWorkerDelta
	{
		MultiwayInfosetId infoset{};
		std::uint32_t bucket = 0;
		std::uint8_t action = 0;
		double regret = 0.0;
		double strategy_sum = 0.0;
		std::uint64_t trajectory_id = 0;
	};

	// Bounded worker-local stream. Traversal appends allocation-free after the
	// constructor reserves capacity, sorts once at the worker boundary, and never
	// accesses coordinator storage directly.
	class MultiwayWorkerDeltaStream
	{
	public:
		MultiwayWorkerDeltaStream(std::size_t worker_index, std::size_t capacity);

		[[nodiscard]] std::size_t worker_index() const noexcept { return worker_index_; }
		[[nodiscard]] std::size_t capacity() const noexcept { return capacity_; }
		[[nodiscard]] std::size_t size() const noexcept { return deltas_.size(); }
		[[nodiscard]] bool try_append(const MultiwayWorkerDelta& delta) noexcept;
		void rewind(std::size_t size) noexcept;
		void sort_fixed_order() noexcept;
		[[nodiscard]] bool is_fixed_order() const noexcept;
		[[nodiscard]] const std::vector<MultiwayWorkerDelta>& deltas() const noexcept { return deltas_; }

	private:
		std::size_t worker_index_ = 0;
		std::size_t capacity_ = 0;
		std::vector<MultiwayWorkerDelta> deltas_;
	};

	struct MultiwayRootActionProbability
	{
		MultiwayActionDescriptor action{};
		Probability probability = 0.0;
	};

	struct MultiwayRootPolicy
	{
		MultiwayPublicStateId public_state{};
		MultiwayInfosetId infoset{};
		std::uint32_t bucket = 0;
		std::vector<MultiwayRootActionProbability> actions;
	};

	struct MultiwaySolveDiagnostics
	{
		std::uint64_t batches_completed = 0;
		std::uint64_t trajectories_attempted = 0;
		std::uint64_t trajectories_accepted = 0;
		std::uint64_t trajectories_discarded = 0;
		std::uint64_t public_states_admitted = 0;
		std::uint64_t sparse_rows_admitted = 0;
		std::uint64_t preflop_rows_admitted = 0;
		std::uint64_t flop_rows_admitted = 0;
		std::uint64_t turn_rows_admitted = 0;
		std::uint64_t river_rows_admitted = 0;
		std::uint64_t worker_delta_entries_merged = 0;
		std::uint64_t terminal_visits = 0;
		std::uint64_t leaf_visits = 0;
		std::array<std::uint64_t, 4> street_visits{};
		std::uint64_t missing_lookup_requests = 0;
		std::uint64_t coordinator_lock_wait_nanoseconds = 0;
		std::uint64_t last_merged_stream_fingerprint = 0U;
		double traversal_seconds = 0.0;
		double merge_seconds = 0.0;
		double export_seconds = 0.0;
		std::optional<MultiwayNashConv> quality;
	};

	// Cold coordinator admission diagnostics. These counters are runtime-only
	// telemetry and do not affect strategy identity or deterministic merges.
	struct MultiwayAdmissionDiagnostics
	{
		std::uint64_t raw_public_state_candidates = 0U;
		std::uint64_t raw_row_candidates = 0U;
		std::uint64_t worker_local_unique_public_state_candidates = 0U;
		std::uint64_t worker_local_unique_row_candidates = 0U;
		std::uint64_t globally_unique_public_state_candidates = 0U;
		std::uint64_t globally_unique_row_candidates = 0U;
		std::uint64_t already_admitted_public_state_candidates = 0U;
		std::uint64_t already_admitted_row_candidates = 0U;
		std::uint64_t newly_admitted_public_state_candidates = 0U;
		std::uint64_t newly_admitted_row_candidates = 0U;
		std::uint64_t parent_resolution_passes = 0U;
		std::uint64_t parent_resolution_probes = 0U;
		std::uint64_t descriptor_reconstructions = 0U;
		std::uint64_t existing_public_state_checks = 0U;
		std::uint64_t new_public_state_validations = 0U;
		std::uint64_t parent_catalog_resolutions = 0U;
		std::uint64_t row_batch_state_resolutions = 0U;
		std::uint64_t row_catalog_state_resolutions = 0U;
		std::uint64_t lock_acquisitions = 0U;
		std::uint64_t discovery_prepare_nanoseconds = 0U;
		std::uint64_t discovery_merge_nanoseconds = 0U;
		std::uint64_t discovery_public_state_merge_nanoseconds = 0U;
		std::uint64_t discovery_row_merge_nanoseconds = 0U;
		std::uint64_t discovery_public_state_source_elements = 0U;
		std::uint64_t discovery_row_source_elements = 0U;
		std::uint64_t discovery_public_state_duplicate_candidates = 0U;
		std::uint64_t discovery_row_duplicate_candidates = 0U;
		std::uint64_t discovery_single_stream_batches = 0U;
		std::uint64_t discovery_one_stream_fast_path_uses = 0U;
		std::uint64_t parent_order_nanoseconds = 0U;
		std::uint64_t state_catalog_classification_nanoseconds = 0U;
		std::uint64_t public_state_validation_nanoseconds = 0U;
		std::uint64_t parent_transition_validation_nanoseconds = 0U;
		std::uint64_t state_preflight_nanoseconds = 0U;
		std::uint64_t row_state_resolution_nanoseconds = 0U;
		std::uint64_t row_shape_validation_nanoseconds = 0U;
		std::uint64_t row_preflight_nanoseconds = 0U;
		std::uint64_t catalog_commit_nanoseconds = 0U;
		std::uint64_t storage_commit_nanoseconds = 0U;
		std::uint64_t catalog_menu_lookup_nanoseconds = 0U;
		std::uint64_t catalog_menu_key_nanoseconds = 0U;
		std::uint64_t catalog_menu_index_lookup_nanoseconds = 0U;
		std::uint64_t catalog_menu_equality_nanoseconds = 0U;
		std::uint64_t catalog_node_pack_nanoseconds = 0U;
		std::uint64_t catalog_reserve_nanoseconds = 0U;
		std::uint64_t catalog_append_nanoseconds = 0U;
		std::uint64_t catalog_index_merge_nanoseconds = 0U;
		std::uint64_t catalog_index_insertion_nanoseconds = 0U;
		std::uint64_t catalog_menu_index_probes = 0U;
		std::uint64_t catalog_menu_equality_checks = 0U;
		std::uint64_t catalog_existing_menu_hits = 0U;
		std::uint64_t catalog_batch_menu_reuse_hits = 0U;
		std::uint64_t catalog_new_menus = 0U;
		std::uint64_t catalog_menu_id_collision_misses = 0U;
		std::uint64_t public_state_betting_board_validation_nanoseconds = 0U;
		std::uint64_t public_state_menu_validation_nanoseconds = 0U;
		std::uint64_t public_state_history_identity_validation_nanoseconds = 0U;
		std::uint64_t row_admission_nanoseconds = 0U;
		std::uint64_t parent_transition_fixed_state_nanoseconds = 0U;
		std::uint64_t parent_transition_action_edge_nanoseconds = 0U;
		std::uint64_t parent_transition_board_chance_edge_nanoseconds = 0U;
		std::uint64_t parent_transition_street_edge_nanoseconds = 0U;
		std::uint64_t parent_transition_action_edges = 0U;
		std::uint64_t parent_transition_board_chance_edges = 0U;
		std::uint64_t parent_transition_street_edges = 0U;
		std::uint64_t parent_transition_action_history_prefix_elements = 0U;
		std::uint64_t parent_transition_action_menu_probes = 0U;
		std::vector<std::uint64_t> worker_raw_public_state_candidates_by_worker;
		std::vector<std::uint64_t> worker_raw_row_candidates_by_worker;
		std::vector<std::uint64_t> worker_local_unique_public_state_candidates_by_worker;
		std::vector<std::uint64_t> worker_local_unique_row_candidates_by_worker;

		void accumulate(const MultiwayAdmissionDiagnostics& other)
		{
			raw_public_state_candidates += other.raw_public_state_candidates;
			raw_row_candidates += other.raw_row_candidates;
			worker_local_unique_public_state_candidates += other.worker_local_unique_public_state_candidates;
			worker_local_unique_row_candidates += other.worker_local_unique_row_candidates;
			globally_unique_public_state_candidates += other.globally_unique_public_state_candidates;
			globally_unique_row_candidates += other.globally_unique_row_candidates;
			already_admitted_public_state_candidates += other.already_admitted_public_state_candidates;
			already_admitted_row_candidates += other.already_admitted_row_candidates;
			newly_admitted_public_state_candidates += other.newly_admitted_public_state_candidates;
			newly_admitted_row_candidates += other.newly_admitted_row_candidates;
			parent_resolution_passes += other.parent_resolution_passes;
			parent_resolution_probes += other.parent_resolution_probes;
			descriptor_reconstructions += other.descriptor_reconstructions;
			existing_public_state_checks += other.existing_public_state_checks;
			new_public_state_validations += other.new_public_state_validations;
			parent_catalog_resolutions += other.parent_catalog_resolutions;
			row_batch_state_resolutions += other.row_batch_state_resolutions;
			row_catalog_state_resolutions += other.row_catalog_state_resolutions;
			lock_acquisitions += other.lock_acquisitions;
			discovery_prepare_nanoseconds += other.discovery_prepare_nanoseconds;
			discovery_merge_nanoseconds += other.discovery_merge_nanoseconds;
			discovery_public_state_merge_nanoseconds += other.discovery_public_state_merge_nanoseconds;
			discovery_row_merge_nanoseconds += other.discovery_row_merge_nanoseconds;
			discovery_public_state_source_elements += other.discovery_public_state_source_elements;
			discovery_row_source_elements += other.discovery_row_source_elements;
			discovery_public_state_duplicate_candidates += other.discovery_public_state_duplicate_candidates;
			discovery_row_duplicate_candidates += other.discovery_row_duplicate_candidates;
			discovery_single_stream_batches += other.discovery_single_stream_batches;
			discovery_one_stream_fast_path_uses += other.discovery_one_stream_fast_path_uses;
			parent_order_nanoseconds += other.parent_order_nanoseconds;
			state_catalog_classification_nanoseconds += other.state_catalog_classification_nanoseconds;
			public_state_validation_nanoseconds += other.public_state_validation_nanoseconds;
			parent_transition_validation_nanoseconds += other.parent_transition_validation_nanoseconds;
			state_preflight_nanoseconds += other.state_preflight_nanoseconds;
			row_state_resolution_nanoseconds += other.row_state_resolution_nanoseconds;
			row_shape_validation_nanoseconds += other.row_shape_validation_nanoseconds;
			row_preflight_nanoseconds += other.row_preflight_nanoseconds;
			catalog_commit_nanoseconds += other.catalog_commit_nanoseconds;
			storage_commit_nanoseconds += other.storage_commit_nanoseconds;
			catalog_menu_lookup_nanoseconds += other.catalog_menu_lookup_nanoseconds;
			catalog_menu_key_nanoseconds += other.catalog_menu_key_nanoseconds;
			catalog_menu_index_lookup_nanoseconds += other.catalog_menu_index_lookup_nanoseconds;
			catalog_menu_equality_nanoseconds += other.catalog_menu_equality_nanoseconds;
			catalog_node_pack_nanoseconds += other.catalog_node_pack_nanoseconds;
			catalog_reserve_nanoseconds += other.catalog_reserve_nanoseconds;
			catalog_append_nanoseconds += other.catalog_append_nanoseconds;
			catalog_index_merge_nanoseconds += other.catalog_index_merge_nanoseconds;
			catalog_index_insertion_nanoseconds += other.catalog_index_insertion_nanoseconds;
			catalog_menu_index_probes += other.catalog_menu_index_probes;
			catalog_menu_equality_checks += other.catalog_menu_equality_checks;
			catalog_existing_menu_hits += other.catalog_existing_menu_hits;
			catalog_batch_menu_reuse_hits += other.catalog_batch_menu_reuse_hits;
			catalog_new_menus += other.catalog_new_menus;
			catalog_menu_id_collision_misses += other.catalog_menu_id_collision_misses;
			public_state_betting_board_validation_nanoseconds += other.public_state_betting_board_validation_nanoseconds;
			public_state_menu_validation_nanoseconds += other.public_state_menu_validation_nanoseconds;
			public_state_history_identity_validation_nanoseconds += other.public_state_history_identity_validation_nanoseconds;
			row_admission_nanoseconds += other.row_admission_nanoseconds;
			parent_transition_fixed_state_nanoseconds += other.parent_transition_fixed_state_nanoseconds;
			parent_transition_action_edge_nanoseconds += other.parent_transition_action_edge_nanoseconds;
			parent_transition_board_chance_edge_nanoseconds += other.parent_transition_board_chance_edge_nanoseconds;
			parent_transition_street_edge_nanoseconds += other.parent_transition_street_edge_nanoseconds;
			parent_transition_action_edges += other.parent_transition_action_edges;
			parent_transition_board_chance_edges += other.parent_transition_board_chance_edges;
			parent_transition_street_edges += other.parent_transition_street_edges;
			parent_transition_action_history_prefix_elements += other.parent_transition_action_history_prefix_elements;
			parent_transition_action_menu_probes += other.parent_transition_action_menu_probes;
			const auto append = [](std::vector<std::uint64_t>& target, const std::vector<std::uint64_t>& source) {
				if (target.size() < source.size())
					target.resize(source.size(), 0U);
				for (std::size_t index = 0U; index < source.size(); ++index)
					target[index] += source[index];
			};
			append(worker_raw_public_state_candidates_by_worker, other.worker_raw_public_state_candidates_by_worker);
			append(worker_raw_row_candidates_by_worker, other.worker_raw_row_candidates_by_worker);
			append(worker_local_unique_public_state_candidates_by_worker, other.worker_local_unique_public_state_candidates_by_worker);
			append(worker_local_unique_row_candidates_by_worker, other.worker_local_unique_row_candidates_by_worker);
		}
	};

	// Immutable boundary output. It deliberately retains only a root policy,
	// per-seat root values, and diagnostics; it never exports dense global policy.
	class MultiwaySolveResult
	{
	public:
		MultiwaySolveResult(
			MultiwayRootPolicy root_policy,
			std::vector<Value> root_values,
			MultiwaySolveDiagnostics diagnostics);

		[[nodiscard]] const MultiwayRootPolicy& root_policy() const noexcept { return root_policy_; }
		[[nodiscard]] const std::vector<Value>& root_values() const noexcept { return root_values_; }
		[[nodiscard]] const MultiwaySolveDiagnostics& diagnostics() const noexcept { return diagnostics_; }

	private:
		MultiwayRootPolicy root_policy_;
		std::vector<Value> root_values_;
		MultiwaySolveDiagnostics diagnostics_;
	};

	// Coordinator boundary for multiway batches. It is the sole owner of public
	// state admission and row mutation. This is intentionally not a traversal API.
	class MultiwaySolverCoordinator
	{
	public:
		explicit MultiwaySolverCoordinator(const MultiwaySolveRequest& request);
		~MultiwaySolverCoordinator();

		void admit_public_state(const MultiwayPublicStateDescriptor& state);
		// Inputs must be canonical and public_states must be sorted by id. The
		// parent_order indexes that stream in parent-first order.
		void admit_prepared_batch(
			const std::vector<MultiwayPublicStateDescriptor>& public_states,
			const std::vector<std::size_t>& parent_order,
			const std::vector<MultiwaySparseRowShape>& rows,
			const std::vector<std::size_t>* parent_indexes = nullptr);
		[[nodiscard]] static bool public_state_candidates_compatible(
			const MultiwayPublicStateDescriptor& left,
			const MultiwayPublicStateDescriptor& right) noexcept;
		void admit_infoset_row(const MultiwaySparseRowShape& shape);
		[[nodiscard]] bool has_infoset_row(MultiwayInfosetId infoset) const noexcept;
		// Epoch queries are lock-free and valid only while the coordinator's
		// immutable batch epoch is not being mutated.
		[[nodiscard]] bool has_infoset_row_epoch(MultiwayInfosetId infoset) const noexcept;
		[[nodiscard]] bool has_public_state_epoch(MultiwayPublicStateId id) const noexcept;
		void regret_matched_strategy_into(
			MultiwayInfosetId infoset,
			std::uint32_t bucket,
			Probability* output,
			std::size_t output_size) const;
		void merge_worker_streams(const std::vector<MultiwayWorkerDeltaStream>& streams);
		void merge_worker_streams(const std::vector<const MultiwayWorkerDeltaStream*>& streams);

		[[nodiscard]] MultiwayRootPolicy export_root_policy() const;
		[[nodiscard]] MultiwayRootPolicy export_root_current_policy() const;
		[[nodiscard]] std::vector<double> export_root_strategy_sums() const;
		[[nodiscard]] MultiwayRootPolicy export_root_policy_since(
			const std::vector<double>& baseline_strategy_sums) const;
		void scale_regrets(double factor);
		[[nodiscard]] std::size_t scale_and_prune_regrets(
			double factor, double threshold, double regret_floor) noexcept;
		void record_terminal_visit() noexcept;
		void record_leaf_visit() noexcept;
		void record_missing_lookup() noexcept;
		void merge_traversal_counters(
			std::uint64_t terminal_visits,
			std::uint64_t leaf_visits,
			std::uint64_t missing_lookup_requests) noexcept;
		void regret_matched_strategy_into_epoch(
			MultiwayInfosetId infoset,
			std::uint32_t bucket,
			Probability* output,
			std::size_t output_size) const;
		[[nodiscard]] bool action_below_regret_epoch(
			MultiwayInfosetId infoset, std::uint32_t bucket, std::uint8_t action,
			double threshold) const noexcept;
		[[nodiscard]] bool action_below_regret(
			MultiwayInfosetId infoset, std::uint32_t bucket, std::uint8_t action,
			double threshold) const noexcept;
		[[nodiscard]] std::size_t prune_negative_regrets(
			double threshold = 0.0, double regret_floor = 0.0) noexcept;
		[[nodiscard]] const MultiwaySparseRowStorage& storage() const noexcept { return storage_; }
		[[nodiscard]] const MultiwayCompactStorage* compact_storage() const noexcept { return compact_storage_.get(); }
		[[nodiscard]] const MultiwaySolveDiagnostics& diagnostics() const noexcept { return diagnostics_; }
		[[nodiscard]] const MultiwaySolverLimits& limits() const noexcept { return request_.limits(); }
		void set_admission_diagnostics(MultiwayAdmissionDiagnostics* diagnostics) noexcept
		{
			admission_diagnostics_ = diagnostics;
		}
		[[nodiscard]] std::size_t merge_scratch_capacity() const noexcept
		{
			return static_cast<std::size_t>(request_.limits().worker_count) * request_.limits().max_worker_delta_entries;
		}
		[[nodiscard]] const MultiwayRootSnapshot& root() const noexcept { return request_.root(); }
		[[nodiscard]] bool has_public_state(MultiwayPublicStateId id) const noexcept;
		[[nodiscard]] const MultiwayPublicStateDescriptor* find_public_state(
			MultiwayPublicStateId id) const noexcept;
		[[nodiscard]] MultiwayCoordinatorCheckpoint checkpoint() const;
		void restore_checkpoint(const MultiwayCoordinatorCheckpoint& checkpoint);

	private:
		friend class MultiwayTerminalAdapter;
		struct PendingMergeCell
		{
			std::size_t index = 0U;
			double regret = 0.0;
			double strategy_sum = 0.0;
		};

		[[nodiscard]] const MultiwayPublicStateDescriptor* public_state(
			MultiwayPublicStateId id) const;
		void merge_worker_streams_locked(
			const std::vector<const MultiwayWorkerDeltaStream*>& streams);

		MultiwaySolveRequest request_;
		MultiwaySparseRowStorage storage_;
		std::unique_ptr<MultiwayCompactStorage> compact_storage_;
		MultiwayPublicCatalog public_catalog_;
		std::vector<const MultiwayWorkerDeltaStream*> merge_stream_views_;
		std::vector<std::size_t> merge_stream_cursors_;
		std::vector<PendingMergeCell> pending_merge_cells_;
		std::vector<std::uint8_t> admission_processed_states_;
		std::vector<std::uint8_t> admission_new_states_;
		std::vector<std::uint8_t> admission_new_rows_;
		std::vector<std::uint8_t> admission_new_row_streets_;
		std::vector<std::size_t> admission_row_state_indexes_;
		std::vector<std::uint64_t> admission_state_menu_keys_;
		std::vector<std::uint8_t> admission_state_action_counts_;
		std::vector<std::uint8_t> admission_state_streets_;
		std::vector<core::PlayerId> admission_state_current_players_;
		std::vector<std::uint8_t> admission_state_next_node_kinds_;
		std::vector<std::uint32_t> admission_state_menu_handles_;
		MultiwaySolveDiagnostics diagnostics_;
		MultiwayAdmissionDiagnostics* admission_diagnostics_ = nullptr;
		mutable std::shared_mutex traversal_mutex_;
	};

} // namespace texas::solver::multiway
