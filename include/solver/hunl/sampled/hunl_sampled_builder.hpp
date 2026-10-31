#pragma once

/** @file
 * @brief Lazily builds and memoizes sampled HUNL state graphs under memory limits.
 */

#include "core/legacy_namespace_compat.hpp"

#include "games/hunl.hpp"
#include "games/hunl_flat_graph.hpp"
#include "games/hunl_tree.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <limits>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace texas::solver::hunl
{

	/** @brief Bounds public-state cache and builder memory growth. */
#pragma pack(push, 8)
	struct HUNLSampledBuilderConfig
	{
		std::size_t max_cached_public_states = 0; ///< Maximum cached states; zero means no count limit.
		std::uint64_t memory_limit_bytes = 0;	  ///< Maximum estimated bytes; zero means no byte limit.
	};
#pragma pack(pop)

	/** @brief Fixed-capacity canonical public-state key for sampled graph lookup. */
#pragma pack(push, 8)
	struct HUNLSampledStateKey
	{
		std::array<int, HUNL_MAX_HISTORY_CODES> history_codes = {};
		std::array<std::uint8_t, 4> street_lengths = { 0, 0, 0, 0 };
		HUNLFlatPackedBoard board{};
		std::array<int, 2> contributions = { 0, 0 };
		std::array<int, 2> stacks = { 0, 0 };
		int to_call = 0;
		PlayerId cur_player = -1;
		PlayerId street_aggressor = -1;
		Street street = Street::Preflop;
		std::uint8_t street_num_raises = 0;
		std::uint8_t pending_board_deals = 0;
		std::uint8_t history_count = 0;
		std::array<bool, 2> folded = { false, false };
		std::array<bool, 2> all_in = { false, false };

		/** @brief Compares all state fields used by builder memoization. */
		bool operator==(const HUNLSampledStateKey& other) const noexcept
		{
			return history_codes == other.history_codes && street_lengths == other.street_lengths && board.cards == other.board.cards && board.count == other.board.count && contributions == other.contributions && stacks == other.stacks && to_call == other.to_call && cur_player == other.cur_player && street_aggressor == other.street_aggressor && street == other.street && street_num_raises == other.street_num_raises && pending_board_deals == other.pending_board_deals && history_count == other.history_count && folded == other.folded && all_in == other.all_in;
		}
	};
#pragma pack(pop)

	/** @brief Hashes a sampled state key consistently with equality. */
#pragma pack(push, 8)
	struct HUNLSampledStateKeyHash
	{
		/** @brief Computes the state-key hash. */
		std::size_t operator()(const HUNLSampledStateKey& key) const noexcept;
	};
#pragma pack(pop)

	/** @brief One action or chance edge in the sampled public graph. */
#pragma pack(push, 8)
	struct HUNLSampledEdge
	{
		ActionId action = 0;
		std::uint32_t child = 0;
		double probability = 0.0;
		std::uint32_t multiplicity = 1;
	};
#pragma pack(pop)

	/** @brief Memoized node metadata and edge range for a sampled game state. */
#pragma pack(push, 8)
	struct HUNLSampledNode
	{
		HUNLFlatNodeType type = HUNLFlatNodeType::Decision;
		HUNLSampledStateKey key{};
		InfosetId infoset_id{};
		HUNLFlatPackedBoard board{};
		std::array<int, 2> contributions = { 0, 0 };
		std::array<double, 2> terminal_utility = { 0.0, 0.0 };
		TerminalKind terminal_kind = TerminalKind::non_terminal();
		PlayerId player = -1;
		Street street = Street::Preflop;
		std::uint32_t edge_begin = 0;
		std::uint16_t edge_count = 0;
		bool expanded = false;
	};
#pragma pack(pop)

	/** @brief Memory and graph-size estimate for a sampled builder. */
#pragma pack(push, 8)
	struct HUNLSampledBuilderMemoryEstimate
	{
		std::uint64_t nodes_bytes = 0;
		std::uint64_t edges_bytes = 0;
		std::uint64_t state_cache_bytes = 0;
		std::uint64_t lookup_bytes = 0;
		std::uint64_t infoset_bytes = 0;
		std::uint64_t nodes = 0;
		std::uint64_t edges = 0;

		/** @brief Sums byte components with saturation on overflow. */
		[[nodiscard]] std::uint64_t total_bytes() const noexcept
		{
			std::uint64_t total = 0;
			for (const auto value : {
					 nodes_bytes, edges_bytes, state_cache_bytes, lookup_bytes, infoset_bytes })
			{
				if (value > std::numeric_limits<std::uint64_t>::max() - total)
				{
					return std::numeric_limits<std::uint64_t>::max();
				}
				total += value;
			}
			return total;
		}
	};
#pragma pack(pop)

	/** @brief Lazily expands and deduplicates public HUNL states for sampled traversal. */
	class HUNLSampledBuilder
	{
	public:
		/** @brief Creates a builder with optional cache and memory limits. */
		explicit HUNLSampledBuilder(HUNLSampledBuilderConfig config = {});

		/** @brief Sets root_state as the root and returns its node ID. */
		[[nodiscard]] std::uint32_t initialize(const HUNLState& root_state);
		/** @brief Expands node_id and memoizes its outgoing branches. */
		void ensure_expanded(std::uint32_t node_id);

		/** @brief Returns a node by ID. */
		[[nodiscard]] const HUNLSampledNode& node(std::uint32_t node_id) const;
		/** @brief Returns mutable access to a node by ID. */
		[[nodiscard]] HUNLSampledNode& node_mut(std::uint32_t node_id);
		/** @brief Returns an edge by ID. */
		[[nodiscard]] const HUNLSampledEdge& edge(std::uint32_t edge_id) const;
		/** @brief Returns the game state represented by a node. */
		[[nodiscard]] const HUNLState& state(std::uint32_t node_id) const;
		/** @brief Returns the root node ID. */
		[[nodiscard]] std::uint32_t root_id() const noexcept;
		/** @brief Returns the number of memoized nodes. */
		[[nodiscard]] std::size_t node_count() const noexcept;
		/** @brief Returns the number of stored edges. */
		[[nodiscard]] std::size_t edge_count() const noexcept;
		/** @brief Returns the number of registered information sets. */
		[[nodiscard]] std::size_t infoset_count() const noexcept;
		/** @brief Estimates memory currently owned by the builder. */
		[[nodiscard]] HUNLSampledBuilderMemoryEstimate memory_estimate() const noexcept;
		/** @brief Returns active builder limits. */
		[[nodiscard]] const HUNLSampledBuilderConfig& config() const noexcept;
		/** @brief Sets the maximum number of cached public states. */
		void set_max_cached_public_states(std::size_t maximum) noexcept;
		/** @brief Sets the estimated memory growth limit in bytes. */
		void set_memory_limit_bytes(std::uint64_t limit) noexcept;
		/** @brief Clears cached states and edges while retaining vector capacity. */
		void clear() noexcept;

		/** @brief Builds the canonical key for one public HUNL state. */
		[[nodiscard]] static HUNLSampledStateKey make_key(const HUNLState& state);

	private:
		[[nodiscard]] std::uint32_t find_or_create(const HUNLState& state);
		[[nodiscard]] std::pair<InfosetId, bool> find_or_create_infoset_id(const HUNLState& state);
		[[nodiscard]] static std::uint64_t estimate_state_bytes(const HUNLState& state) noexcept;
		void admit_growth(std::uint64_t bytes) const;
		void reserve_edge_capacity(std::size_t required_size);

		HUNLSampledBuilderConfig config_;
		std::uint32_t root_id_ = 0;
		std::unordered_map<HUNLSampledStateKey, std::uint32_t, HUNLSampledStateKeyHash> node_lookup_;
		std::unordered_map<std::string, InfosetId> infoset_lookup_;
		std::vector<std::string> infoset_keys_;
		std::vector<HUNLSampledNode> nodes_;
		std::vector<HUNLState> states_;
		std::vector<HUNLSampledEdge> edges_;
	};

} // namespace texas::solver::hunl
