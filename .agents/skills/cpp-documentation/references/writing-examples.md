# Writing and C++ examples

Examples show documentation patterns, not permission to change the APIs. Project examples reflect inspected source; recheck current code before using them. Avoid copying assumptions about `bool`, target platforms, or ownership into unrelated APIs.

## Writing rules

Use a short, descriptive first sentence that explains the result or responsibility. Add one fact per sentence and use active, precise verbs. Name units, owners, lifetimes and boundaries rather than vague terms such as "safe", "fast" or "valid". Describe what happens for edge cases and failures; do not repeat the signature as prose. Put public contracts next to the declaration, design rationale in an architecture guide or near the relevant implementation, and examples in maintained source files where practical. Link rather than duplicate an authoritative contract. When rewriting, preserve accurate rationale and useful links; reconcile stale claims with code and document any unresolved discrepancy.

Use `///` or `/** ... */` consistently with nearby code. Doxygen commands such as `@brief`, `@param`, `@return`, `@throws`, `@tparam`, `@ref`, `@file`, `@ingroup` and `@deprecated` are tools, not a requirement to add empty sections. Doxygen's `@param[out]` expresses output direction; describe behavior on both success and failure explicitly. `@pre` is for an actual caller obligation, not an expected validation failure.

## Project example: checked arithmetic

`include/core/checked_arithmetic.hpp` implements `checked_multiply_to_size`: negative signed inputs or a product too large for `std::size_t` return false before assignment; successful multiplication writes the result. Its `static_assert` checks integral types, but `std::make_unsigned_t` imposes additional restrictions, notably on `bool`. Adapt this example only after checking the current implementation:

```cpp
/**
 * @brief Multiplies two non-negative integral values into a size.
 * @tparam Left Integral type accepted by the implementation's unsigned conversion.
 * @tparam Right Integral type accepted by the implementation's unsigned conversion.
 * @param left First factor; negative signed values fail.
 * @param right Second factor; negative signed values fail.
 * @param[out] result Receives the product on success; unchanged on failure.
 * @return True if the product fits in std::size_t; false otherwise.
 */
template <typename Left, typename Right>
[[nodiscard]] constexpr bool checked_multiply_to_size(
    Left left, Right right, std::size_t& result) noexcept;
```

This is a documentation illustration of the existing declaration, not a replacement header. The unsigned conversion requirement matters: saying "all integral types" would overstate the contract.

## Project example: cleanup and borrowed lifetime

`include/util/thread_join_guard.hpp` stores a reference to a thread vector and a moved stop callable. Its active destructor attempts the stop callable, catches its exceptions, then attempts to join each joinable thread while catching join exceptions. `release()` disables that destructor work. The following illustrates a scoped internal documentation request; verify call sites and edge cases before treating it as a complete contract:

```cpp
/**
 * @brief Attempts to stop and join the referenced workers on destruction.
 *
 * The referenced vector must outlive the guard. The destructor may block
 * while joining and catches exceptions from the stop callable and joins.
 * Calling release() disables this cleanup.
 * @tparam StopFunction Callable invoked before the join attempts.
 */
template <class StopFunction>
class ThreadJoinGuard;
```

Do not write "always joins every worker": a join attempt may throw. Constructor documentation should identify the borrowed vector and moved callable; destructor documentation should state the blocking and exception behavior. Document failure or lifetime constraints only where the implementation and calling contract substantiate them.

## Project example: warnings

`include/solver/hunl/flat/hunl_flat_dcfr.hpp` places MSVC warning 4324 suppression around an explicitly aligned profile struct, with `_MSC_VER` guards and push/pop. The code establishes the scoped suppression and aligned declaration; investigate the original reason before asserting the warning is expected or safe. A future explanatory comment might describe the compiler, warning, alignment, region and restoration once verified. Do not add suppression to resolve a documentation task.

## Reusable declaration patterns

```cpp
/** @file
 * @brief Declares the components used to evaluate a game state.
 */

/** @brief Components used to evaluate game states. */
namespace example {

/** @brief Holds a borrowed range for the duration specified by its contract. */
class RangeView {
public:
    /** @brief Creates an empty view. */
    RangeView() = default;
    /** @brief Destroys the view without owning its underlying data. */
    ~RangeView() = default;
};

/** @brief Describes the outcome of an operation. */
enum class Status {
    success, ///< The operation completed.
    failure  ///< The operation did not complete.
};
}
```

This is schematic, not a TexasSolver API. Replace all claims with verified behavior. For real constructors explain parameters and lifetime; for nontrivial destructors explain actual cleanup and blocking. Name a documented file when global declarations must be associated by Doxygen.

For an unchanged virtual override, Doxygen may inherit documentation if configured to do so. If an explicit `@copydoc` is necessary, qualify the actual base declaration and overload; render-check it before claiming success. For a changed override document its additional behavior while preserving the base contract; don't paste the entire base description.

## Markdown guide templates

### Tutorial

Purpose and prerequisites; numbered steps with runnable inputs; expected output; next step. Explain why a learner performs each step.

### How-to

Task and prerequisites; minimal ordered steps; confirmation of success; relevant failure and recovery paths; link to authoritative reference. Use concrete CLI and file names only after verifying them.

### Reference

Describe every option or field, type, units, default, valid values, precedence and error behavior where applicable. Include examples and compatibility notes only when supported by evidence.

### Architecture explanation

State the problem, boundaries and data flow; describe ownership and lifecycle, meaningful alternatives and tradeoffs, invariants, concurrency, numerical and determinism assumptions, and links to authoritative code or tests. Separate known limitations from unproven claims.

Keep snippets synchronized with code: link or include real example files when practical and verify them only under the repository's execution permissions. Record version and platform assumptions when needed.
