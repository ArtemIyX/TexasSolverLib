# Prompt: Redesign the AD7 state identity validation optimization

You are the analyzer agent for the next AD7 optimization. Inspect the current
implementation and prepare a new implementation plan. Do not modify C++ code.

The previous optimization must not simply be reverted. It must be
re-implemented or redesigned so that the original identity-validation contract
is preserved while the measured admission regression is removed.

## Session rules

- Do not build.
- Do not run tests.
- Do not run any built `.exe` files.
- Do not run solver, training, capture, benchmark, or executable-launching
  scripts.
- Use `scripts/` and `rg` for read-only code discovery where useful.
- Inspect the current branch and worktree before analysis.
- Use `project-navigation` to locate code and call paths.
- Use `cpp-code-review` to review the changed C++ independently.
- Do not change source, tests, plans, or logs during analysis.
- Save only the new analysis plan requested below.

## Required output

Create a detailed implementation plan under `docs/plans/2026-10-17/` with a
filename that identifies the redesigned AD7 state identity validation work.
The plan must identify the recommended design, exact files/functions, API
changes, tests, performance measurements, and rollback/safety boundaries.

Do not recommend reverting commit `23c64c4` as the final solution. The next
implementation must replace or improve the design while keeping the
optimization objective.

## Current repository state

Repository: `C:\Users\xeuse\RiderProjects\TexasSolver`

Branch: `PB-3`

Relevant commits:

- `183d878 PB-3 Scripts: Allow sealed checkpoint telemetry`
- `23c64c4 PB-3 Multiway: Optimize state identity validation`
- `51158f8 PB-3 Docs: Record identity validation optimization`

The current implementation from `23c64c4` adds:

- `MultiwayPublicIdentityInspection` in
  `include/solver/multiway/abstraction/multiway_public_builder.hpp`.
- `MultiwayPublicBuilder::inspect_schema_v2_identity(...)` in
  `src/solver/multiway/abstraction/multiway_public_builder.cpp`.
- A replacement call in
  `src/solver/multiway/engine/multiway_solver.cpp` inside
  `validate_public_state_descriptor(...)`.
- Focused tests in `tests/test_multiway_public_builder.cpp` and
  `tests/test_multiway_solver.cpp`.

## Original measured problem

The cold AD7 workload has approximately 69,194 newly admitted public-state
descriptors. Before the optimization, every descriptor traversed its history
three times:

1. Structural history validation.
2. `stable_history_id(...)`.
3. `stable_public_state_id(...)`.

The original reference capture was:

```text
capture: tmp/tracy/ad7-next-cold-fixed-1w-r2
coordinator admission: 264.549 ms
state preflight: 149.387 ms
public-state validation: 117.315 ms
history/identity validation: 74.328 ms
parent transition validation: 23.507 ms
discovery merge: 71.849 ms
catalog commit: 24.008 ms
fingerprint: 9806774443936985566
checkpoint bytes: 137380835
failed: false
discarded trajectories: 0
```

The plan targeted the history/identity bucket because it was the largest
repeated-work component and because the total admission target is below
250 ms. The planned target was approximately:

- history/identity validation at or below 59 ms;
- public-state validation below 102 ms;
- total admission below 250 ms.

## Current implementation idea and measured result

The current implementation keeps the old `stable_history_id(...)` and
`stable_public_state_id(...)` functions as reference paths, but adds a new
inspector. It initializes two FNV-1a accumulators and feeds history values to
both lanes in lockstep.

The central helper is currently equivalent to:

```cpp
void append_u64_pair(
    std::uint64_t& first,
    std::uint64_t& second,
    std::uint64_t value) noexcept
{
    for (std::uint8_t byte = 0U; byte < 8U; ++byte)
    {
        const auto encoded = static_cast<std::uint8_t>(value >> (byte * 8U));
        append_byte(first, encoded);
        append_byte(second, encoded);
    }
}
```

The inspector also:

- computes the public-state prefix once;
- appends the history field prefix to both hashes;
- validates actor, action, action index, menu ID, and target contribution;
- updates both hashes for actor, action, target, and menu ID;
- appends the legal menu to the public-state hash;
- returns both identities plus a history-valid flag.

The operator ran two valid Tracy captures after the implementation. Both had
26 Tracy zones and `acceptance_pass: true`, so the telemetry and correctness
gates passed. The performance gates did not pass.

### Capture `ad7-identity-cold-fixed-1w-r2`

```text
coordinator admission: 293.565 ms
state preflight: 172.655 ms
public-state validation: 137.768 ms
history/identity validation: 94.214 ms
parent transition validation: 24.754 ms
discovery merge: 77.268 ms
catalog commit: 24.117 ms
```

### Capture `ad7-identity-cold-fixed-1w-r3`

```text
coordinator admission: 299.590 ms
state preflight: 174.667 ms
public-state validation: 138.502 ms
history/identity validation: 94.042 ms
parent transition validation: 25.755 ms
discovery merge: 81.681 ms
catalog commit: 23.920 ms
```

The two runs confirm a stable regression, especially in the target bucket:

- average admission is approximately 296.6 ms versus 264.549 ms before;
- average history/identity validation is approximately 94.1 ms versus 74.328
  ms before;
- history/identity validation regressed by approximately 26.6%;
- total admission regressed by approximately 12.1%.

Both captures still preserved:

- `acceptance_pass: true`;
- deterministic merge fingerprint `9806774443936985566`;
- checkpoint bytes `137380835`;
- `failed: false`;
- discarded trajectories `0`;
- phase coverage above `0.95`;
- byte accounting pass.

This means the failure is performance-only, not an observed identity or
checkpoint correctness failure.

## Current call flow to inspect

Trace this flow from traversal to admission:

1. `MultiwayRootExternalSamplingTraversal::traverse_decision(...)` and the
   public chance/street-transition child creation in
   `src/solver/multiway/engine/multiway_traversal.cpp`.
2. `MultiwayPublicBuilder::make_action_child(...)`,
   `make_board_chance_child(...)`, and
   `make_street_transition_child(...)` in
   `src/solver/multiway/abstraction/multiway_public_builder.cpp`.
3. Producer-side `stable_history_id(...)` and
   `stable_public_state_id(...)`. These IDs must not be trusted as a
   replacement for admission verification.
4. `MultiwayDiscoveryStream::try_append_public_state(...)` and
   `prepare_for_merge(...)` in `multiway_traversal.cpp`.
5. `MultiwayRootBatchRunner::run(...)`, discovery merge, parent-first ordering,
   and `MultiwaySolverCoordinator::admit_prepared_batch(...)`.
6. `validate_public_state_descriptor(...)` in
   `src/solver/multiway/engine/multiway_solver.cpp`.
7. The nested telemetry buckets:
   - `public_state_validation_nanoseconds`;
   - `public_state_history_identity_validation_nanoseconds`;
   - `catalog_menu_key_nanoseconds`.
8. Parent transition validation and catalog/storage commit must remain outside
   the redesign scope unless the new plan proves a separate issue.

Read these reference implementations carefully:

- `append_u64(...)` and FNV constants in `include/core/fingerprint.hpp`;
- `append_history(...)`;
- `stable_history_id(...)`;
- `stable_public_state_id(...)`;
- the old validation loop and the current inspector;
- the focused tests and existing schema-v2 descriptor rejection tests.

## Questions the analyzer must answer

### 1. Why did the dual-hash implementation regress?

Inspect the source and identify concrete likely costs. Address at least:

- per-byte function calls through `append_byte(...)`;
- whether `append_u64_pair(...)` is inlined or remains a call boundary;
- loop-control and branch overhead;
- interleaving two dependent FNV-1a chains;
- whether the compiler can vectorize the loop;
- whether the public prefix and action-menu work changed relative to the old
  path;
- whether the returned aggregate or validation branch adds measurable work;
- whether the timing bucket includes menu-key extraction and clock overhead;
- whether the two runs are sufficient to distinguish variance from regression.

Do not claim a compiler or microarchitectural cause without source evidence or
a clearly marked hypothesis. Builds and generated-code inspection are deferred
to the operator because of the current session rules.

### 2. Can SIMD help?

Evaluate SIMD as an option, but do not assume it is beneficial.

The FNV-1a recurrence is a dependency chain:

```text
hash_next = (hash_previous XOR byte) * 1099511628211 mod 2^64
```

Consider explicitly:

- two independent 64-bit lanes for the history and public-state hashes;
- whether the available SSE2/AVX2 integer instructions support efficient
  exact 64-bit multiplication modulo `2^64`;
- whether AVX2 32-bit multiply decomposition would cost more than scalar code;
- whether AVX-512 or runtime dispatch exists in this repository and is allowed;
- scalar fallback and exact equivalence on all CPUs;
- ABI, compiler, and target-feature constraints on Windows/MSVC;
- whether vector code would preserve deterministic IDs exactly;
- whether a SIMD implementation would add complexity without reducing the
  measured 94 ms bucket.

If SIMD is not justified, say so and recommend a scalar design such as direct
inlined lane updates, loop unrolling, specialized four-field history encoding,
or a carefully structured fixed-width helper.

### 3. Explore alternatives beyond `append_u64_pair`

Rank at least three designs, for example:

1. Direct inlined dual-lane byte updates with no helper call inside the byte
   loop, plus measured unrolling.
2. A specialized schema-v2 history encoder that processes the four fixed
   64-bit fields per entry with explicit or bounded unrolling.
3. A small fixed two-lane hash state type with compiler-friendly operations and
   a scalar reference path.
4. A carefully justified SIMD implementation with scalar fallback.
5. A producer/admission intermediate representation that reuses already
   encoded history bytes without trusting producer IDs, only if its memory,
   lifetime, and deterministic behavior are safe.
6. A validation/hash pipeline that rejects malformed entries before hashing
   while preserving the valid-path performance target.

Do not recommend a cache unless the plan proves bounded lifetime, invalidation,
memory accounting, descriptor identity safety, and no stale-state admission.
Do not skip admission verification based on producer-side IDs.

## Non-negotiable correctness invariants

The redesigned implementation must preserve:

- schema-v2 history ID bytes and public-state ID bytes exactly;
- deterministic merge fingerprint `9806774443936985566` for the cold workload;
- established steady-workload fingerprint `3496596242701454594`;
- cold checkpoint bytes `137380835`;
- steady checkpoint bytes `387676624`;
- rejection of invalid actor, action, action index, menu ID, and negative target;
- rejection of independently mutated canonical history ID;
- rejection of independently mutated public-state ID;
- rejection of changed history with stale IDs;
- no catalog or storage mutation after a rejected prepared batch;
- same error ordering for malformed history versus fingerprint mismatch;
- fixed and dynamic scheduler equivalence;
- deterministic merge and exact-mode behavior;
- unchanged checkpoint telemetry schema and byte accounting;
- no persistent or retained scratch-memory increase;
- no allocation, string, hash-map, logging, or virtual dispatch in the
  admission identity hot path.

## Required plan contents

The new plan must include:

1. A concise diagnosis of the measured regression.
2. Exact source symbols and file locations.
3. A ranked comparison of at least three redesigns.
4. One recommended design and why it is expected to beat the current and old
   implementations.
5. Exact API/data-structure changes, including ownership and `noexcept`
   behavior.
6. How the old reference hash functions remain available for differential
   testing.
7. A focused test plan covering golden IDs, root and non-empty histories,
   board-chance and street-transition descriptors, empty/non-empty menus, and
   every malformed-history mutation.
8. A solver admission plan covering independent ID mutations and transactional
   rejection.
9. SIMD decision criteria and scalar fallback requirements, if SIMD remains a
   candidate.
10. Operator-only build, focused-test, and Tracy commands. Do not execute them
    during analysis.
11. A measurement matrix with before/after fields and valid exclusive-bucket
    formulas. Do not add nested telemetry values to parent buckets.
12. Acceptance gates:
    - coordinator admission `< 250 ms`;
    - history/identity validation `≤ 59 ms`;
    - public validation `< 102 ms`;
    - catalog commit `≤ 25 ms`;
    - no unaffected top-level phase regression greater than `10%` without an
      explanation;
    - all correctness and checkpoint invariants pass.
13. A stop rule: if the redesign cannot show a credible path below the gates,
    identify the next isolated bottleneck instead of combining unrelated
    changes.

## Important interpretation

The telemetry analyzer result is valid for the two captures because it reported
`acceptance_pass: true`. The missing
`mw.checkpoint.artifact_validation` zone was intentionally allowed with
`-AllowMissingArtifactValidation`; it is unrelated to this performance
regression.

The current evidence supports redesigning the identity path, not changing
checkpoint code, checkpoint bytes, merge ordering, catalog semantics, or
telemetry schema.
