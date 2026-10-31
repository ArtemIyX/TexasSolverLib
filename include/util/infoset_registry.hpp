#pragma once

/** @file
 * Stable interned identifiers and metadata for HUNL infoset keys.
 */

#include "core/legacy_namespace_compat.hpp"

#include "core/types.hpp"
#include "games/hunl.hpp"

#include <cstddef>
#include <optional>
#include <unordered_map>
#include <vector>

namespace texas::util
{

	/** Metadata retained for an interned infoset id. */
#pragma pack(push, 8)
	struct InfosetMeta
	{
		std::size_t action_count = 0;
	};
#pragma pack(pop)

	/**
	 * @brief Stable mapping between external infoset keys and numeric IDs.
	 * @details Interns generic keys and HUNL encodings in one registry.
	 */
	class InfosetRegistry
	{
	public:
		/** Returns the existing id or interns key with action_count metadata. */
		InfosetId intern(const InfosetKey& key, std::size_t action_count);
		/** Returns the existing id or interns an encoded HUNL infoset. */
		InfosetId intern(const HUNLInfosetEncoding& encoding, std::size_t action_count);
		/** Finds key without inserting it. */
		[[nodiscard]] std::optional<InfosetId> find(const InfosetKey& key) const;
		/** Returns the external key stored for id. */
		[[nodiscard]] const InfosetKey& key_for(InfosetId id) const;
		/** Returns action-count metadata stored for id. */
		[[nodiscard]] const InfosetMeta& meta_for(InfosetId id) const;
		/** Returns interned keys in their numeric-id order. */
		[[nodiscard]] const std::vector<InfosetKey>& keys() const noexcept;
		/** Returns the number of interned infosets. */
		[[nodiscard]] std::size_t size() const noexcept;
		/** Returns true when the registry contains no keys. */
		[[nodiscard]] bool empty() const noexcept;
		/** Removes every key and id mapping. */
		void clear() noexcept;

	private:
		std::unordered_map<InfosetKey, InfosetId> key_to_id_;
		std::unordered_map<HUNLInfosetEncoding, InfosetId, HUNLInfosetEncodingHash> hunl_to_id_;
		std::vector<InfosetKey> id_to_key_;
		std::vector<InfosetMeta> meta_;
	};

} // namespace texas::util
