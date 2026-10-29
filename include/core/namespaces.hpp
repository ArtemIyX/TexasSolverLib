#pragma once

/** @file
 * @brief Declares TexasSolver's namespace hierarchy for shared subsystem headers.
 */

// Declares the public namespace hierarchy before individual subsystem headers
// contribute their declarations. Root using-directives support cross-subsystem
// references during the breaking migration from core:: to texas::.
/** @brief Root namespace for TexasSolver APIs and implementation subsystems. */
namespace texas
{
	/** @brief Shared domain types and compatibility interfaces. */
	namespace core
	{
	}
	/** @brief Game definitions and state-transition models. */
	namespace games
	{
		/** @brief Heads-up no-limit Hold'em game model. */
		namespace hunl
		{
		}
		/** @brief Multiway poker game models. */
		namespace multiway
		{
		}
	} // namespace games
	/** @brief Range representations shared by game and solver components. */
	namespace ranges
	{
	}
	/** @brief Preflop calculation and lookup components. */
	namespace preflop
	{
	}
	/** @brief Solver algorithms and game-specific solver modules. */
	namespace solver
	{
		/** @brief Generic DCFR solver components. */
		namespace dcfr
		{
		}
		/** @brief Heads-up no-limit solver components. */
		namespace hunl
		{
		}
		/** @brief Multiway solver components. */
		namespace multiway
		{
		}
	} // namespace solver
	/** @brief Shared utility and profiling components. */
	namespace util
	{
		/** @brief Internal utility implementation details. */
		namespace detail
		{
		}
		/** @brief Profiling helpers. */
		namespace profiling
		{
		}
	} // namespace util

} // namespace texas
