#pragma once

/** @file
 * @brief Declares a reusable arena for short-lived, trivially destructible objects.
 */

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <new>
#include <stdexcept>
#include <type_traits>
#include <vector>

namespace texas::core
{

	/** @brief Reuses allocated blocks for short-lived, trivially destructible data. */
	class Arena
	{
	public:
		/** @brief Releases all arena storage for reuse. */
		void reset() noexcept;
		/** @brief Returns the current allocation mark used by release(). */
		std::size_t mark() const noexcept;
		/** @brief Rewinds allocations to a mark returned by mark().
		 *
		 * A mark at or beyond the current mark leaves the arena unchanged.
		 */
		void release(std::size_t mark) noexcept;

		/** @brief Reserves raw arena storage for an array of trivially destructible objects.
		 *
		 * The arena does not construct the objects. Returned storage remains available
		 * until a release(), reset(), or destruction rewinds the owning block.
		 * @tparam T A trivially destructible type.
		 * @param count Number of T-sized storage slots; zero returns nullptr.
		 * @return Pointer to suitably aligned uninitialized storage.
		 * @throws std::overflow_error If the requested byte count or alignment padding overflows.
		 */
		template <class T>
		T* allocate(std::size_t count = 1);

	private:
		struct Block
		{
			std::vector<std::byte> storage;
			std::size_t used = 0;
		};

		static constexpr std::size_t kDefaultBlockSize = 4096;

		Block& acquire_block(std::size_t min_bytes);

		std::vector<Block> blocks_;
		std::size_t current_block_ = 0;
		std::size_t total_used_ = 0;
	};

	template <class T>
	T* Arena::allocate(std::size_t count)
	{
		static_assert(std::is_trivially_destructible_v<T>, "Arena only supports trivially destructible types");

		if (count == 0)
		{
			return nullptr;
		}

		const auto alignment = alignof(T);
		const auto bytes = sizeof(T) * count;
		if (bytes / sizeof(T) != count)
		{
			throw std::overflow_error("Arena allocation overflow");
		}

		const auto padded = bytes + alignment - 1;
		if (padded < bytes)
		{
			throw std::overflow_error("Arena allocation overflow");
		}

		auto* block = &acquire_block(padded);
		while (true)
		{
			const auto base = reinterpret_cast<std::uintptr_t>(block->storage.data());
			const auto current = base + block->used;
			const auto aligned = (current + alignment - 1) & ~static_cast<std::uintptr_t>(alignment - 1);
			const auto aligned_offset = static_cast<std::size_t>(aligned - base);
			const auto required = aligned_offset + bytes;
			if (required >= aligned_offset && required <= block->storage.size())
			{
				block->used = required;
				total_used_ += required - (current - base);
				return reinterpret_cast<T*>(block->storage.data() + aligned_offset);
			}
			block = &acquire_block(padded);
		}
	}

} // namespace texas::core
