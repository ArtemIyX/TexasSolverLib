---
name: cpp-code-review
description: Independently review selected or newly changed C++17 code in TexasSolver for actionable correctness, safety, contract, concurrency, performance, and maintainability defects. Use for code-review requests, review of a diff or file range, verification of a proposed fix, or deciding whether a C++ change is ready. Return zero findings when the selected code is sound; do not invent review churn.
---

# TexasSolver C++ Code Review

Review the selected change independently. The goal is to find real, actionable regressions, not to keep a review cycle alive.

## Review boundary

IT IS FORBIDDEN to run builds, tests, benchmarks, installs, or solver jobs unless the user explicitly asks. When asked to verify, use the repository's documented CMake commands and report exactly what ran.

1. Identify the review target: supplied diff, selected files/ranges, or explicitly named change. If no target is provided, inspect the current uncommitted C++ change only.
2. Read the target and only the immediate declarations, callers, tests, and contracts required to verify it. Do not broaden into a general repository audit.
3. Compare the change with its base behavior when a diff exists. Distinguish a defect introduced by the change from pre-existing code.
4. Do not run builds, tests, benchmarks, or solver jobs unless the user explicitly asks.

Derive conclusions from the code and its contracts, not from the implementer's explanation or previous review conclusions.

## Finding standard

Report a finding only when all of these are true:

- It is caused by the selected change, or the selected code fails an explicit existing contract.
- It has a concrete execution path, input, state, or invariant that demonstrates the problem.
- It has material impact: wrong result, undefined behavior, corruption, crash, race, resource failure, API break, meaningful performance regression in a known hot path, or missing required validation.
- The report names a precise location and explains why the current code fails.

Do not report:

- hypothetical risks without a reachable scenario
- style preferences, alternative designs, or refactors that do not fix a defect
- existing out-of-scope issues, unless they make the selected change unsafe
- a concern already fixed in the reviewed diff
- duplicate formulations of one root cause
- performance guesses without evidence that the code is hot or changes a documented budget

It is correct to produce zero findings. When the selected code meets its contracts, output exactly `No actionable findings.` and stop.

## C++ review checklist

Check only the items relevant to the target:

- Public API, caller expectations, invariants, value units, and backward compatibility.
- Bounds, overflow/underflow, signedness, initialization, invalid inputs, aliasing, and undefined behavior.
- Ownership, lifetime, moves/copies, exceptions/status handling, and cleanup on early exit.
- State transitions, poker-rule semantics, range/blocker correctness, terminal utility accounting, and error/fallback paths.
- Thread ownership, data races, synchronization, cancellation/deadline behavior, seed/order determinism, and worker merge behavior.
- New or changed hot loops: allocation, growth, strings, logging, hashing, virtual dispatch, layout, cache behavior, and memory-budget admission.
- Scalar/SIMD equivalence, tail handling, invalid numeric values, and numeric-tolerance claims when relevant.

For solver work, preserve `HUNLFlatDCFR` unless the change explicitly targets it. Treat legacy exact HUNL and structured sampled HUNL as separate contracts. Require memory preflight and deterministic worker-local merge behavior for new sampled-solver capacity or concurrency changes.

## Performance standard

Call out performance only when the review target changes an established hot path, allocation boundary, memory preflight, timed export, or synchronization design.

- Require an identifiable hot call path or a direct violation of repository hot-path rules.
- Prefer a specific claim such as “this vector can reallocate once per trajectory” over “this may be slow.”
- Do not demand SIMD, caching, pooling, or a different data layout without profile evidence.
- Treat preallocated flat buffers, compact IDs, action-major rows, worker-local deltas, and fixed-order merges as established solver rules, not optional style.

## Prevent review churn

Keep a stable finding identity based on root cause and location.

1. Assign each actionable finding a short ID, such as `R1`.
2. On a follow-up fix, verify `R1` against the exact cited behavior. Mark it `fixed`, `remaining`, or `withdrawn`.
3. Review only the patch that resolves `R1` and any directly affected code needed to prove the fix. Do not reopen unrelated reviewed areas.
4. Do not replace a fixed finding with a new opinion about preferred structure. Raise a new finding only if the follow-up patch introduces a separate, demonstrable defect.
5. Do not resurrect a closed finding unless new code or new evidence makes its original failure path valid again. State what changed.
6. If all tracked findings are fixed and the follow-up adds no regression, finish with `No remaining actionable findings.`

Never ask for a revert merely because a later implementation uses a different valid design. Review behavior and contracts, not the history of attempts.

## Output format

Start with one verdict:

- `No actionable findings.`
- `Findings: N.`

For each finding, use only this form:

```text
[R1] P1 — <short defect>
Location: <file:line>
Evidence: <reachable scenario and failing behavior>
Impact: <concrete consequence>
Fix: <minimal corrective direction>
```

Use priorities only for real defects:

- `P0`: data loss, security issue, or release-blocking failure.
- `P1`: incorrect behavior, crash, undefined behavior, race, or material contract break.
- `P2`: bounded edge-case defect or proven hot-path/memory regression.

After reviewing a fix, include a compact disposition list only when prior finding IDs exist:

```text
R1: fixed
R2: remaining — <one-line reason>
```

Do not add a “nice to have” section. Ask one focused question only when essential missing context prevents determining whether an otherwise plausible failure path is real.
