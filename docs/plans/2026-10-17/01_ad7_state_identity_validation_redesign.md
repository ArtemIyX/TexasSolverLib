Task: Redesign AD7 schema-v2 state identity validation so admission keeps independent verification and removes the measured regression from commit `23c64c4`.

# AD7 state identity validation redesign

## Scope and repository state

- Inspected branch: `PB-3` at `9bbf0af`, one commit ahead of `origin/PB-3`.
- Relevant implementation: `23c64c4`; the later commits only record the work and add this planning prompt.
- The worktree contains unrelated untracked plans. They must remain untouched.
- Git status requires `--ignore-submodules=all` because the vendored evaluator submodule metadata is not readable in this environment.
- No build, test, executable, solver, training, capture, benchmark, or analyzer script was run. This file is the only saved change.

This redesign is limited to schema-v2 history validation and identity computation during admission. It must not change traversal semantics, producer identities, discovery merge, parent ordering, parent transition validation, catalog/storage commit, checkpoint schemas, or telemetry schemas.

## Conclusion

Replace the current one-pass generic dual-hash loop with a two-stage scalar verifier:

1. Validate every history entry before hashing.
2. Run a branch-free, fixed-width dual-lane history encoder whose eight byte steps are compile-time unrolled and update both scalar FNV-1a chains directly.

This is not a reversion to the old three-traversal path. It keeps independent admission verification, reduces the old path from three history traversals to two, overlaps the two independent hash chains, and removes the current helper-loop/code-generation risks. Keep the old `stable_history_id(...)` and `stable_public_state_id(...)` implementations unchanged as independent reference paths.

Do not implement SIMD, producer-ID trust, an encoded-history cache, or descriptor-retained scratch in the first redesign.

## Independent C++ review

Findings: 1.

```text
[R1] P2 — The lockstep identity helper regresses the measured admission hot path
Location: src/solver/multiway/abstraction/multiway_public_builder.cpp:210
Evidence: commit 23c64c4 replaces two reference hash calls with append_u64_pair in every admitted history field. Two matching 69,194-state captures report 94.214 ms and 94.042 ms in the target bucket versus 74.328 ms before the change.
Impact: history/identity validation is about 26.6% slower and average total admission is about 12.1% slower, missing all three AD7 performance gates.
Fix: retain the verifier contract but replace the runtime byte loop/helper composition with a validated, explicitly unrolled scalar dual-lane encoder and requalify it before touching another phase.
```

No correctness defect was found in the changed inspector or caller. The captures preserve identities, checkpoint size, rejection status, and telemetry acceptance.

## Measured diagnosis

| Metric | Reference `ad7-next-cold-fixed-1w-r2` | Current r2 | Current r3 | Current mean | Change from reference |
|---|---:|---:|---:|---:|---:|
| Coordinator admission | 264.549 ms | 293.565 ms | 299.590 ms | 296.578 ms | +12.1% |
| State preflight | 149.387 ms | 172.655 ms | 174.667 ms | 173.661 ms | +16.2% |
| Public-state validation | 117.315 ms | 137.768 ms | 138.502 ms | 138.135 ms | +17.7% |
| History/identity validation | 74.328 ms | 94.214 ms | 94.042 ms | 94.128 ms | +26.6% |
| Catalog menu-key extraction | 1.126 ms | 1.163 ms | 1.137 ms | 1.150 ms | +2.2% |
| Parent transition validation | 23.507 ms | 24.754 ms | 25.755 ms | 25.255 ms | +7.4% |
| Discovery merge | 71.849 ms | 77.268 ms | 81.681 ms | 79.475 ms | +10.6% |
| Catalog commit | 24.008 ms | 24.117 ms | 23.920 ms | 24.019 ms | flat |

The two current target-bucket measurements differ by only `0.172 ms`, about `0.18%` of their mean. Total admission differs by about `2.0%`, largely alongside discovery-merge variance. Two after-captures are not a statistical distribution and the reference has only one matching sample, but the repeatable `19.8 ms` target-bucket shift is large enough to reject the implementation operationally. The operator must still run matched repeated before/after captures for final attribution.

### Concrete source costs

- `append_u64_pair(...)` at `multiway_public_builder.cpp:210-221` is called four times per history entry and contains an eight-iteration byte loop.
- Each byte calls `append_byte(...)` twice in source. Neither helper is marked `noinline`, so source inspection cannot establish that calls remain in optimized code. Conversely, `inline` would not guarantee removal. MSVC RelWithDebInfo disassembly is required.
- The redesign did not reduce FNV arithmetic. Old and current valid paths both perform 64 dependent XOR/multiply updates per history entry: 32 bytes for each of two hashes. The change only reduced descriptor loads/outer traversals and attempted to overlap the chains.
- The runtime byte loop adds induction, shift, compare, and branch structure unless MSVC fully unrolls it. Four helper invocations also create four loop setup/exit regions per entry.
- Each FNV lane is serial across bytes. The two lanes are independent, but interleaving them can improve throughput only if the compiler keeps both hashes in registers and schedules both multiply chains well.
- The recurrence prevents vectorization across bytes. Baseline SSE2/AVX2 also lacks a direct packed low-64-bit integer multiply, so automatic vectorization of the two hash lanes is not expected from the source shape.
- The structural-validity branch moved into the hashing loop. It performs the same field checks as before, but it may inhibit unrolling/scheduling across entries. This is a hypothesis until generated code is inspected.
- The public-state betting/board prefix and legal-menu suffix are still computed exactly once. They did not increase relative to the old `stable_public_state_id(...)` call. Both paths initialize two fingerprints and append two history prefixes.
- `MultiwayPublicIdentityInspection` contains two 64-bit values and a `bool`. On Windows x64 it may use a hidden aggregate-return buffer and adds one post-call validity branch per descriptor. The source does not prove a measurable cost, and this bounded overhead cannot by itself explain the observed `19.8 ms` shift. The proposed API removes the risk.
- `public_state_history_identity_validation_nanoseconds` starts before the inspector and ends after metadata extraction. It includes the nested `catalog_menu_key_nanoseconds`, both inner clock calls, the metadata writes, and its own clock/counter overhead. That telemetry topology existed before `23c64c4`, so it does not explain the regression.

## Call path and ownership boundaries

1. `MultiwayRootExternalSamplingTraversal::traverse_decision(...)` in `src/solver/multiway/engine/multiway_traversal.cpp` creates action children through `MultiwayPublicBuilder::make_action_child(...)`.
2. `MultiwayRootExternalSamplingTraversal::traverse_public_chance(...)` creates board-chance and street-transition children through `make_board_chance_child(...)` and `make_street_transition_child(...)`.
3. Those builder functions in `src/solver/multiway/abstraction/multiway_public_builder.cpp` compute producer-side `stable_history_id(...)` and `stable_public_state_id(...)`. Admission must not trust them.
4. `MultiwayDiscoveryStream::try_append_public_state(...)` copies descriptors into worker-owned vectors. `prepare_for_merge(...)` sorts/deduplicates by ID and checks descriptor compatibility.
5. `MultiwayRootBatchRunner::run(...)` merges discovery streams, builds parent-first order, and calls `MultiwaySolverCoordinator::admit_prepared_batch(...)`.
6. `admit_prepared_batch(...)` classifies new states and calls the internal `validate_public_state_descriptor(...)` in `src/solver/multiway/engine/multiway_solver.cpp:295` before parent validation and any catalog/storage mutation.
7. `validate_public_state_descriptor(...)` owns the betting/board, menu, and history/identity telemetry. Parent transition validation remains a later, disjoint state-preflight child.
8. Catalog and storage mutation begin only after state and row preflight. The redesign must not move identity work past that boundary.

All new verifier state is stack-owned. Input vectors remain caller-owned and are observed through const references or non-owning pointer/count views valid only for the call.

### Exact file/symbol boundary

| File | Symbol | Planned action |
|---|---|---|
| `include/core/fingerprint.hpp` | `FNV1A_OFFSET`, `FNV1A_PRIME`, `append_u64(...)` | Read-only byte-order and arithmetic contract |
| `include/solver/multiway/abstraction/multiway_public_builder.hpp` | `MultiwayPublicIdentityInspection`, `MultiwayPublicBuilder::inspect_schema_v2_identity(...)` | Add the compatibility-preserving out-parameter overload |
| `src/solver/multiway/abstraction/multiway_public_builder.cpp` | `append_history(...)`, `stable_history_id(...)`, `stable_public_state_id(...)` | Keep unchanged as reference paths |
| `src/solver/multiway/abstraction/multiway_public_builder.cpp` | `append_u64_pair(...)`, `valid_history_action(...)`, `inspect_schema_v2_identity(...)` | Replace the runtime pair loop with validated, unrolled scalar encoding |
| `src/solver/multiway/engine/multiway_solver.cpp` | `validate_public_state_descriptor(...)` | Use the out-parameter overload; preserve errors, timers, and metadata |
| `src/solver/multiway/engine/multiway_solver.cpp` | `MultiwaySolverCoordinator::admit_prepared_batch(...)` | No semantic change; retain preflight-before-commit boundary |
| `src/solver/multiway/engine/multiway_traversal.cpp` | traversal child creation, `try_append_public_state(...)`, `prepare_for_merge()`, `MultiwayRootBatchRunner::run(...)` | No change |
| `tests/test_multiway_public_builder.cpp` | schema-v2 identity tests | Add golden, differential, transition, menu, and malformed cases |
| `tests/test_multiway_solver.cpp` | admission and prepared-batch tests | Add ID mutation, error-order, and transactional rejection cases |

## Ranked redesigns

| Rank | Design | Expected result | Risk | Decision |
|---:|---|---|---|---|
| 1 | Validate first, then a branch-free compile-time-unrolled scalar dual-lane encoder for the four fixed 64-bit history fields | Removes current byte-loop/helper risk, retains dual-chain instruction-level parallelism, and still uses two rather than three history traversals | Moderate code duplication; generated code must be checked | Implement |
| 2 | One-pass direct scalar dual-lane encoding with eight explicit byte steps after each entry's validation | One history traversal and no runtime byte loop | Validation branch remains interleaved with hashing and may reproduce the current scheduling problem | Keep as the first measured fallback experiment |
| 3 | Validate first, then hash the two identities sequentially with specialized unrolled scalar helpers | Predictable scalar code and two traversals | Gives up dual-chain overlap; may improve current results but is less likely to beat the old 74.3 ms bucket by the required margin | Safe fallback, not the primary design |
| 4 | Small `DualFnv1a64` state type with a generic shared-field API | Cleaner reuse and central byte-order logic | Abstraction may recreate call boundaries unless disassembly proves complete inlining | Use only if generated code is identical to rank 1 |
| 5 | SSE2/AVX2 packed two-lane FNV | Potential lane parallelism | Requires 64-bit multiply decomposition, shuffles, and dispatch; likely more instructions than two scalar `imul` chains | Do not implement initially |
| 6 | Producer/admission encoded-history intermediate representation | Could avoid decoding fields twice | Duplicates descriptor memory, extends lifetime/copy costs, and still requires admission to prove bytes match structured history | Reject |

No cache is recommended. A cache would need bounded lifetime, invalidation, collision-safe descriptor matching, memory accounting, and protection against stale-state admission. It cannot replace verification of caller-owned descriptors.

## Recommended API and implementation

### Public result/API

`multiway_public_builder.hpp` is an installed public header. Preserve the
current value-returning API and result shape, then add an out-parameter overload
for the admission hot path:

```cpp
struct MultiwayPublicIdentityInspection
{
    std::uint64_t canonical_history_id = 0U;
    MultiwayPublicStateId public_state_id{};
    bool history_valid = false;
};

[[nodiscard]] static bool inspect_schema_v2_identity(
    const MultiwayBettingSnapshot& betting,
    const std::vector<std::uint8_t>& board,
    const std::vector<MultiwayPublicHistoryEntry>& history,
    const std::vector<MultiwayActionDescriptor>& legal_actions,
    MultiwayPublicIdentityInspection& output) noexcept;
```

- Return `false` only for malformed history shape.
- Zero `output` before validation and leave it zero on failure.
- Return `true` only after both exact IDs are written and
  `output.history_valid` is set.
- The function owns no memory and remains `noexcept`.
- `validate_public_state_descriptor(...)` uses this overload, avoiding the
  aggregate return in the hot path and making validity explicit.
- Keep both reference hash APIs and their implementation independent. Do not make them wrappers around the optimized encoder.
- Keep the existing value-returning overload as a compatibility wrapper over
  the new overload. It is not used by admission.

### Scalar encoder

In `src/solver/multiway/abstraction/multiway_public_builder.cpp`:

1. Keep `append_history(...)`, `stable_history_id(...)`, and `stable_public_state_id(...)` unchanged.
2. Keep `valid_history_action(...)`; add a narrow `valid_schema_v2_history(...)` helper that checks, in current order:
   - actor is non-negative and below `betting.stacks.size()`;
   - action is one of Fold, Check, Call, Bet, Raise, AllIn;
   - history `action_index == 0`;
   - `action_menu_id != 0`;
   - target contribution is non-negative.
3. Return `false` before any identity result is published if a check fails.
4. Initialize the history and public hashes independently with the exact current schema field.
5. Append betting and board only to the public hash, using the existing prefix helpers.
6. Append the exact History tag and `history.size() * 4 * sizeof(uint64_t)` length to both hashes.
7. Iterate history again. Convert actor, action, target, and menu ID to the same four `uint64_t` values used by `append_history(...)`.
8. For each value, execute eight compile-time-unrolled little-endian byte steps. Each step must directly perform:

   ```text
   history_hash = (history_hash XOR byte) * FNV1A_PRIME
   public_hash  = (public_hash  XOR byte) * FNV1A_PRIME
   ```

   Use an eight-step template/fold or eight explicit statements. Do not leave a runtime byte loop or an `append_byte(...)` call inside it. Do not rely on the `inline` keyword alone; operator disassembly must show no helper call in the history loop.
9. Append the legal menu only to the public hash and apply `non_zero(...)` to both results.
10. Do not allocate, format, log, perform hash-map lookup, dispatch virtually, or retain scratch.

In `src/solver/multiway/engine/multiway_solver.cpp`, change only `validate_public_state_descriptor(...)`:

```cpp
MultiwayPublicIdentityInspection identity;
if (!MultiwayPublicBuilder::inspect_schema_v2_identity(
        state.betting, state.board, state.history, state.legal_actions, identity))
{
    throw std::invalid_argument(
        "multiway public state history contains an invalid action descriptor");
}
if (state.canonical_history_id != identity.canonical_history_id ||
    state.id != identity.public_state_id)
{
    throw std::invalid_argument(
        "multiway public state descriptor does not match its schema-v2 fingerprint");
}
```

Preserve this error ordering. Leave metadata extraction and its nested timer where they are. Do not change `admit_prepared_batch(...)`, parent validation, catalog APIs, or storage APIs.

The `59 ms` gate requires about a `37.3%` reduction from the current mean and
about a `20.6%` reduction from the old reference. Rank 1 has a credible path
because it removes one old descriptor traversal, removes all runtime byte-loop
control, presents two independent multiply chains to the scheduler, and avoids
the admission aggregate return. The capture must prove the gain; source-level
operation counting is not acceptance evidence.

## SIMD decision

SIMD is not justified for the next implementation.

- FNV-1a has a byte-to-byte dependency chain, so bytes within one hash cannot execute in parallel.
- There are only two independent 64-bit lanes.
- SSE2 has 32-by-32-to-64 integer multiply for selected lanes, not packed low-64-bit multiplication.
- AVX2 retains that limitation. Exact low-64-bit products require decomposing each operand into 32-bit halves, cross-products, shifts/shuffles, and adds. With a constant multiplier this can be simplified, but it is still likely more work than two scalar x64 multiplies.
- AVX-512DQ supplies packed 64-bit multiplication, but the repository has no AVX-512 target or runtime dispatch. `src/util/simd.cpp` selects SSE2/AVX2 at compile time; `detect_simd_backend()` is compile-feature detection, not CPUID dispatch.
- `CMakeLists.txt` adds `/W4` for MSVC but no repository-wide `/arch:AVX2` or AVX-512 contract. The captures are MSVC RelWithDebInfo.

Reconsider SIMD only if the rank-1 scalar code is correctly inlined/unrolled and still misses `59 ms` in two matched captures. A SIMD experiment then requires:

- a separately guarded implementation;
- runtime feature detection before executing non-baseline instructions;
- the scalar implementation as the mandatory fallback and oracle;
- exact golden/differential equality for every ID bit;
- no ABI-visible vector types;
- no target-feature requirement for the installed library;
- a measured improvement in the full 69,194-state bucket, not only a microbenchmark.

## Focused tests

### `tests/test_multiway_public_builder.cpp`

Extend the current inspector tests to cover:

- fixed golden schema-v2 history and public-state IDs produced by the unchanged reference functions before the optimized implementation is shared;
- empty root history with non-empty menu;
- one-entry and multi-entry action histories;
- empty and non-empty legal menus;
- descriptors produced by `make_board_chance_child(...)`;
- descriptors produced by `make_street_transition_child(...)`;
- equality with both reference functions for every fixture;
- output is zero and return value is false for negative actor, actor equal to seat count, invalid action enum, non-zero history action index, zero menu ID, and negative target;
- valid boundary actor and zero target remain accepted.

Keep the optimized and reference implementations structurally independent. Golden constants prevent a shared encoding mistake from passing differential tests.

### `tests/test_multiway_solver.cpp`

Add or strengthen admission cases for:

- independently mutated `canonical_history_id`;
- independently mutated public-state `id`;
- changed actor/action/target/menu ID with both stored IDs stale;
- malformed history plus stale IDs reports the malformed-history message before fingerprint mismatch;
- valid-shaped history plus either stale ID reports the fingerprint message;
- a prepared batch whose final new descriptor is malformed leaves catalog size, exported/checkpoint descriptors, storage shapes, and counters unchanged;
- the same transactional check for a late fingerprint mismatch;
- root, action, board-chance, and street-transition admission;
- existing fixed/dynamic deterministic merge coverage remains unchanged.

Capture exception text locally in the test because `EXPECT_THROW` checks only the exception type.

### Existing suites to retain

- `tests/test_multiway_public_catalog.cpp`: sorted reconstruction and transactional catalog behavior.
- `tests/test_multiway_terminal_adapter.cpp`: board chance and street transition fixtures.
- `tests/test_multiway_qualification_reports.cpp`: unchanged telemetry schema.
- Existing checkpoint, scheduler, exact-mode, and deterministic fixture tests.

No unit test should use a wall-clock threshold.

## Operator-only commands

Do not run these commands during analysis or implementation sessions that retain the current restrictions.

### Build and focused tests

```powershell
powershell -ExecutionPolicy Bypass -File scripts\codex_powershell.ps1 python scripts\full_build.py

cmake --build build --config Debug --target `
  test_multiway_public_builder test_multiway_solver `
  test_multiway_public_catalog test_multiway_terminal_adapter `
  test_multiway_qualification_reports --parallel

ctest --test-dir build -C Debug --output-on-failure -R `
  "^(test_multiway_public_builder|test_multiway_solver|test_multiway_public_catalog|test_multiway_terminal_adapter|test_multiway_qualification_reports)$"

cmake --build build-tracy --config RelWithDebInfo `
  --target texas_multiway_train_tracy --parallel
```

For generated-code review, use an isolated MSVC RelWithDebInfo build configured to emit assembly for `multiway_public_builder.cpp`. Verify that the optimized history loop contains no call to the pair/byte helpers and no runtime eight-byte loop. Do not change production compiler flags merely to make the optimization pass.

### Tracy capture template

```powershell
powershell -ExecutionPolicy Bypass -File scripts\run_multiway_tracy_training_capture.ps1 `
  -CaptureId <id> -Workers <workers> -Batches <batches> `
  -TrajectoriesPerBatch 1000 -Scheduler <scheduler> -CaptureSeconds 60
```

Run the full matrix once, then repeat both fixed one-worker captures. Repeat any threshold-borderline cold capture.

| ID | Workers | Batches | Scheduler | Fingerprint | Checkpoint bytes |
|---|---:|---:|---|---:|---:|
| `ad7-identity-redesign-cold-fixed-1w-r1` | 1 | 1 | fixed | `9806774443936985566` | `137380835` |
| `ad7-identity-redesign-cold-fixed-4w-r1` | 4 | 1 | fixed | `9806774443936985566` | `137380835` |
| `ad7-identity-redesign-cold-dynamic-1w-r1` | 1 | 1 | dynamic | `9806774443936985566` | `137380835` |
| `ad7-identity-redesign-cold-dynamic-4w-r1` | 4 | 1 | dynamic | `9806774443936985566` | `137380835` |
| `ad7-identity-redesign-steady-fixed-1w-r1` | 1 | 4 | fixed | `3496596242701454594` | `387676624` |
| `ad7-identity-redesign-steady-fixed-4w-r1` | 4 | 4 | fixed | `3496596242701454594` | `387676624` |
| `ad7-identity-redesign-steady-dynamic-1w-r1` | 1 | 4 | dynamic | `3496596242701454594` | `387676624` |
| `ad7-identity-redesign-steady-dynamic-4w-r1` | 4 | 4 | dynamic | `3496596242701454594` | `387676624` |

Analyze each capture with the intentionally allowed missing artifact-validation zone:

```powershell
powershell -ExecutionPolicy Bypass -File scripts\analyze_multiway_checkpoint_telemetry.ps1 `
  -ReportPath tmp\tracy\<id>\report.json `
  -CsvPath tmp\tracy\<id>\<id>.csv `
  -ExpectedMergeFingerprint <expected-fingerprint> `
  -AllowMissingArtifactValidation
```

`acceptance_pass: true` remains required. The optional missing `mw.checkpoint.artifact_validation` zone is unrelated to this optimization.

## Measurement matrix

Record raw values rather than sums of nested fields.

| Field | Reference fixed 1w | Current r2 | Current r3 | Redesign r1 | Redesign r2 |
|---|---:|---:|---:|---:|---:|
| New public-state validations | 69,194 | 69,194 | 69,194 | fill | fill |
| Coordinator admission | 264.549 ms | 293.565 ms | 299.590 ms | fill | fill |
| Discovery merge | 71.849 ms | 77.268 ms | 81.681 ms | fill | fill |
| Parent order | 12.228 ms | 12.443 ms | 12.459 ms | fill | fill |
| State preflight | 149.387 ms | 172.655 ms | 174.667 ms | fill | fill |
| Public-state validation | 117.315 ms | 137.768 ms | 138.502 ms | fill | fill |
| Betting/board validation | 24.898 ms | 25.757 ms | 25.827 ms | fill | fill |
| Menu validation | 13.302 ms | 12.857 ms | 13.839 ms | fill | fill |
| History/identity validation | 74.328 ms | 94.214 ms | 94.042 ms | fill | fill |
| Catalog menu-key extraction | 1.126 ms | 1.163 ms | 1.137 ms | fill | fill |
| Parent transition validation | 23.507 ms | 24.754 ms | 25.755 ms | fill | fill |
| Row preflight | 2.663 ms | 2.692 ms | 2.684 ms | fill | fill |
| Catalog commit | 24.008 ms | 24.117 ms | 23.920 ms | fill | fill |
| Storage commit | 4.173 ms | 4.134 ms | 3.866 ms | fill | fill |
| Fingerprint | expected | expected | expected | fill | fill |
| Checkpoint bytes | expected | expected | expected | fill | fill |
| Failed/discarded | false / 0 | false / 0 | false / 0 | fill | fill |
| Phase coverage / byte accounting | pass | pass | pass | fill | fill |

Calculate only source-proven exclusive values:

```text
coordinator_admission_exclusive = coordinator_admission
  - discovery_prepare
  - discovery_merge
  - parent_order
  - state_preflight
  - row_preflight
  - catalog_commit
  - storage_commit

state_preflight_exclusive = state_preflight
  - state_catalog_classification
  - public_state_validation
  - parent_transition_validation

public_validation_exclusive = public_state_validation
  - public_state_betting_board_validation
  - public_state_menu_validation
  - public_state_history_identity_validation

history_identity_exclusive_of_menu_key =
  public_state_history_identity_validation
  - catalog_menu_key
```

Do not add child values to parent values. Do not subtract catalog menu lookup, index lookup, equality, or append fields from catalog commit unless source inspection proves the selected fields are mutually disjoint.

## Acceptance gates

Every cold qualification capture must satisfy:

- coordinator admission `< 250 ms`;
- history/identity validation `<= 59 ms`;
- public-state validation `< 102 ms`;
- catalog commit `<= 25 ms`;
- no unaffected top-level phase regression greater than `10%` against its matching baseline without a source-supported explanation.

Every cold and steady capture must also preserve:

- cold fingerprint `9806774443936985566`;
- steady fingerprint `3496596242701454594`;
- cold checkpoint bytes `137380835`;
- steady checkpoint bytes `387676624`;
- `acceptance_pass: true`, `failed: false`, discarded trajectories `0`, phase coverage at least `0.95`, and byte-accounting pass;
- fixed/dynamic scheduler equivalence for admitted state/row counts, fingerprints, checkpoint bytes, and checkpoint section bytes;
- exact schema-v2 IDs, deterministic merge, exact-mode behavior, and unchanged telemetry schema;
- no persistent or retained scratch-memory increase.

## Safety and rollback boundaries

- Make the API/test adaptation one commit and the scalar encoder one commit so the byte contract is independently reviewable.
- Keep the unchanged reference functions available throughout implementation and qualification.
- Before performance capture, require golden, differential, malformed-input, error-order, and transactional tests to pass.
- Any ID mismatch, checkpoint-byte change, fingerprint change, scheduler divergence, or post-rejection mutation rejects the redesign immediately.
- If the optimized helper is not fully unrolled/inlined in MSVC RelWithDebInfo, fix the scalar source shape before measuring SIMD.
- If rank 1 regresses, rank 2 may be tried as a separate commit. Rank 3 is a correctness-preserving scalar fallback experiment, not the final release solution unless it meets all gates.
- Reverting only the new redesign commit to the current correct-but-slow verifier is an emergency development rollback, not the final AD7 solution. Do not propose reverting `23c64c4` as completion.
- Parent transition validation, discovery merge, catalog/storage commit, checkpoint code, and telemetry schema remain outside the change boundary.

## Stop rule

Stop after two matched cold fixed one-worker captures if the redesign has no credible path to all three validation/admission gates. Do not combine parent-transition, discovery-merge, catalog, scheduler, or checkpoint changes with it.

Use the exclusive matrix to identify the next isolated bottleneck. If history/identity reaches `<= 59 ms` but admission still misses `250 ms`, rank the remaining disjoint top-level phases and create a separate plan for the largest one. If history/identity remains above `59 ms` despite verified scalar code generation, document the scalar instruction count and measured result before considering a separately qualified SIMD experiment.
