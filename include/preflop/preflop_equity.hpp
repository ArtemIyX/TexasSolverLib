#pragma once

/** @file
 * @brief Preflop hand-class indexing and equity-table construction and persistence.
 */

#include "core/legacy_namespace_compat.hpp"

#include "games/hunl.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <tuple>
#include <vector>

namespace texas::preflop
{

	/** @brief Number of rank-and-suitedness classes in the preflop matrix. */
	inline constexpr std::size_t PREFLOP_NUM_CLASSES = 169;
	/** @brief Number of suit variants retained per pair of hand classes. */
	inline constexpr std::size_t PREFLOP_NUM_VARIANTS = 3;

	/** @brief Concrete hero and villain private cards for one class-pair variant. */
#pragma pack(push, 8)
	struct HoleRep
	{
		std::array<std::uint8_t, 2> hero{};	   ///< Hero's encoded private cards.
		std::array<std::uint8_t, 2> villain{}; ///< Villain's encoded private cards.
	};
#pragma pack(pop)

	/** @brief Maps ranks and suitedness to one of the 169 canonical hand classes. */
	std::uint16_t class_index(std::uint8_t rank_hi, std::uint8_t rank_lo, bool suited);
	/** @brief Decodes a class index into high rank, low rank and suitedness. */
	std::tuple<std::uint8_t, std::uint8_t, bool> class_decode(std::uint16_t class_idx);
	/** @brief Maps two encoded hole cards to their canonical class. */
	std::uint16_t hole_to_class(const std::array<std::uint8_t, 2>& hole);
	/** @brief Creates a valid concrete deal for class indices and a suit variant, when possible. */
	std::optional<HoleRep> build_hole_rep(std::uint16_t hero_class, std::uint16_t villain_class, std::uint8_t variant);

	/** @brief Enumerates exact runouts and returns hero's equity for a card pair. */
	double enumerate_pair_equity(const std::array<std::uint8_t, 2>& hero, const std::array<std::uint8_t, 2>& villain);
	/** @brief Builds the flattened 169-by-169-by-3 equity table serially. */
	std::vector<double> build_equity_table_flat();
	/** @brief Builds the flattened equity table using the requested worker count. */
	std::vector<double> build_equity_table_flat_parallel(std::size_t n_threads);
	/** @brief Estimates pair equity by sampling random boards.
	 * @param n_samples Number of board samples.
	 * @param seed Seed for deterministic sampling.
	 */
	double monte_carlo_pair_equity(
		const std::array<std::uint8_t, 2>& hero,
		const std::array<std::uint8_t, 2>& villain,
		std::size_t n_samples,
		std::uint64_t seed);

	/** @brief In-memory preflop class-pair equity values with file persistence. */
	class PreflopEquityTable
	{
	public:
		/** @brief Creates an empty equity table. */
		PreflopEquityTable();

		/** @brief Returns the equity for one class pair and suit variant. */
		double at(std::size_t hero_class, std::size_t villain_class, std::size_t variant) const;
		/** @brief Returns a mutable reference to one class-pair equity value. */
		double& at(std::size_t hero_class, std::size_t villain_class, std::size_t variant);
		/** @brief Reports whether the table contains no data. */
		bool empty() const;
		/** @brief Returns the flattened table storage. */
		const std::vector<double>& data() const;
		/** @brief Returns mutable flattened table storage. */
		std::vector<double>& data();

		/** @brief Builds a complete equity table serially. */
		static PreflopEquityTable build();
		/** @brief Builds a complete equity table using the requested worker count. */
		static PreflopEquityTable build_parallel(std::size_t n_threads);
		/** @brief Loads a binary table from path. */
		static PreflopEquityTable load(const std::filesystem::path& path);
		/** @brief Saves this table in binary format. */
		void save(const std::filesystem::path& path) const;
		/** @brief Loads a table from a delimited text file. */
		static PreflopEquityTable load_csv(const std::string& path);
		/** @brief Saves the table as delimited text. */
		void save_csv(const std::string& path) const;

	private:
		std::vector<double> table_;
		bool valid_ = false;
	};

} // namespace texas::preflop
