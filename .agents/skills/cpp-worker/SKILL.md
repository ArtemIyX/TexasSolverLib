---
name: cpp-worker
description: Implement, modify, review, or optimize C++17 code in TexasSolver. Use for new C++ APIs, source/header changes, solver, game, range, utility, performance, memory, multithreading, SIMD, profiling, and correctness work in this repository. Apply the project's architecture, hot-path, deterministic-solver, and memory-budget rules whenever writing C++ code.
---

# TexasSolver C++ Worker

Write correct C++17 that matches TexasSolver's established namespace, file layout, and solver contracts. Preserve correctness and observability first; optimize only a measured hot path.

## Workflow

IT IS FORBIDDEN to run builds, tests, benchmarks, installs, or solver jobs unless the user explicitly asks. When asked to verify, use the repository's documented CMake commands and report exactly what ran.

1. Read `AGENTS.md` and the public header, matching implementation, tests, and example nearest to the requested type. Search exact symbols with `rg` before broad searches.
2. Identify the contract: ownership, units, determinism, public API, error handling, legacy compatibility, and whether the affected code is hot.
3. Make the narrowest coherent change. Add a public module as mirrored `include/<area>/name.hpp` and `src/<area>/name.cpp`; CMake discovers normal pairs automatically.
4. Keep correctness checks at boundaries and keep hot kernels numeric, contiguous, and allocation-free.
5. Do not run builds, tests, benchmarks, installs, or solver jobs unless the user explicitly asks. When asked to verify, use the repository's documented CMake commands and report exactly what ran.

## Project design rules

- Use C++17 and existing `core::` naming and style.
- Keep game rules and state in `games`, traversal/training in `solver`, solver-neutral ranges in `ranges`, and shared low-level helpers in `core` or `util`.
- Prefer narrow structured APIs to stringly typed options. Use explicit IDs and metadata for states, actions, rows, buckets, and trajectories.
- Keep public declarations lean. Forward-declare where suitable; include complete types only where required.
- Make ownership explicit. Use RAII containers at subsystem boundaries; pass raw non-owning pointers, `std::span`, or lightweight views into hot kernels. Never use raw pointers as ownership.
- Use `const`, `constexpr`, `enum class`, fixed-width integers where storage matters, and `noexcept` only when it is true and useful.
- Validate input at public or cold boundaries. Use the project's checked arithmetic and assertions where nearby code does. Do not put costly validation in an established hot loop unless its contract requires it.
- Match an existing error/status convention. Do not introduce exceptions as routine control flow.
- Preserve exact `HUNLFlatDCFR` behavior unless the task explicitly changes it. Keep legacy exact HUNL and structured sampled HUNL as separate contracts.

## Choose the right implementation shape

| Situation | Preferred design |
|---|---|
| Public or infrequent code | Clear RAII objects, validation, readable structured control flow |
| Small bounded menu or six seats | `std::array` or fixed-capacity storage with explicit count |
| Per-action, per-bucket, per-terminal, or per-trajectory code | Preallocated flat buffers, compact IDs, spans/pointers, no allocations |
| Repeated sparse state admission | Lazy admission with a memory preflight and stable numeric identity |
| A large solver table | Action-major contiguous rows: `row[action][bucket]` |
| Worker execution | Independent trajectory batches, worker-local deltas, fixed-order merge |
| Numeric row arithmetic proved hot | Scalar reference kernel plus selected runtime SIMD dispatch |
| A full graph or dense table would exceed the budget | Sampled/lazy sparse design and root-only export |

Do not apply a hot-path restriction blindly to cold setup code. Conversely, do not hide a hot loop behind a convenient abstraction that allocates, hashes, formats, or dynamically dispatches.

## Hot-path rules

Treat traversal, terminal evaluation, row update, per-action/per-bucket loops, and merge loops as hot unless profiling proves otherwise.

Never use these inside those loops:

- `std::string`, formatting, logging, streams, or error-message construction
- heap allocation, `new`, `delete`, or unreserved growth
- `std::shared_ptr`, `std::function`, virtual dispatch, or exceptions for normal flow
- hash-map lookup, text keys, or filesystem/environment access
- hidden copies of large containers or repeated capacity checks

Prefer:

- pre-sized `std::vector` only at allocation boundaries; indexed writes in kernels
- `std::array` and fixed local scratch for small bounded data
- action-major `row[action][bucket]` layout when processing an action across buckets
- integer IDs, compact enums, stable array indices, and contiguous spans
- separate cold wrappers from small `noexcept`-suitable numeric kernels
- a single clear unit convention for utilities, reach weights, and regrets

Reserve capacity before loops. If a vector can grow during traversal, move admission/allocation to a cold coordinator path or make the capacity bound explicit and preflight it.

## Memory and cache behavior

- Estimate every material allocation before admission. Include graph/cache, rows, sparse values, terminal cache, worker deltas, exports, and scratch.
- For sampled HUNL, warn around 48 GiB, aim below about 56 GiB resident, and reject unsafe configurations before about 60 GiB. Preserve the configured budget for other subsystems.
- Use lazy public-node and sparse-row admission. Do not materialize a production flop graph, dense full strategy, dense reach/value arrays, or graph-sized worker scratch for timed solving.
- Keep long-lived tables compact and numeric. Store diagnostics at cold boundaries, not per stored node by default.
- Prefer structure-of-arrays or action-major layouts when one field/action is scanned across many entries. Use array-of-structures when state transitions consume the entire small record together.
- Align or pad only after measurement or when using an existing aligned allocator. Avoid false sharing by separating frequently written per-worker counters and buffers.
- Precompute stable features, bucket mappings, legal menus, and canonical IDs when they would otherwise be recalculated in the hot path.

## Solver and sampling semantics

- Keep canonical private-hand IDs and blocker checks correct. Fixed `[seat][1326]` range arrays or compact legal-combination views are appropriate bounded representations.
- For external-sampling MCCFR, sample chance and non-traverser actions; enumerate all legal actions for the traverser before updating its regrets.
- Keep strategy computation, regret updates, average-strategy accumulation, reach weighting, and terminal utilities in compatible units.
- Allocate rows lazily for reached information sets. Treat negative-regret pruning as a measured, configured optimization after its warmup and revisit semantics are defined.
- Keep full blueprints read-only after load. Keep live search state local, mutable, bounded, and reusable.
- Export the requested root strategy by default. Treat range-wide diagnostics and dense exports as optional, budgeted work.
- Keep depth-limited leaves behind typed callbacks with explicit value units and model provenance.

## Deterministic multithreading

- Parallelize sampled work by trajectory batches, not by depth stages of the exact solver.
- Derive seeds per trajectory, not merely per worker. Keep the partition, sampling order, and merge order explicit.
- Let workers write local delta streams and scratch only. Merge in deterministic worker/trajectory order. Do not use floating-point atomics for regret or strategy accumulation.
- Keep coordinator admission and shared-row mutation outside worker hot loops unless an explicit, reviewed synchronization design requires otherwise.
- Use RAII thread guards or existing project helpers so exceptional exits join workers safely.
- Label any relaxed throughput mode as non-bitwise-deterministic. Never claim reproducibility without fixing worker count, seeds, partitioning, and merge order.

## SIMD and low-level optimization

Optimize in this order:

1. Establish scalar end-to-end correctness and a fixed profiling workload.
2. Remove accidental allocation, text lookup, avoidable indirection, and poor layout.
3. Measure time, allocation count, resident memory, cache/branch data when available, and worker imbalance.
4. Use the existing scalar/SSE2/AVX2 runtime-dispatched kernels only when row math is a demonstrated bottleneck.
5. Retain a scalar reference path and differential coverage for zero/negative regrets, tails, invalid numeric input, and randomized rows.

Use SIMD for regular contiguous row arithmetic such as regret matching, strategy accumulation, delta addition, SAXPY, and reductions. Do not force SIMD onto irregular traversal, sparse admission, branching game logic, or variable action menus. Do not add a GPU backend for the irregular CPU-oriented solver workload without a separate measured justification.

## Change checklist

- Preserve API, semantic, unit, and legacy behavior unless the request changes them.
- Keep data ownership, lifetimes, and thread ownership obvious.
- Keep allocation, logging, hashing, and formatting out of hot code.
- Make capacity, row limits, memory estimates, and fallback/rejection behavior explicit.
- Use fixed IDs and deterministic ordering where artifacts, checkpoints, or reproducible solves depend on identity.
- Add or update focused tests when the user requests testing or when the change request includes tests. Use differential tests for scalar/SIMD, one-worker/multi-worker, legacy/new, or direct/traversal paths as appropriate.
- Record a before/after profile for performance claims. Do not call an optimization successful based on intuition alone.

## Read detailed guidance when needed

- Read [references/performance-and-validation.md](references/performance-and-validation.md) before changing a hot solver path, memory budgeting, synchronization, SIMD kernels, or performance tests.
- Read `docs/project_state_report.md` for current subsystem contracts and `docs/implementation_roadmap.md` for target multiway/resolver sequencing.
- Read `docs/pluribus_technical_report.md` only for design rationale. Do not present its reconstructed details as source-code facts or automatically copy its parameter values into TexasSolver.
