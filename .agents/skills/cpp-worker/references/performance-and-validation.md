# Performance and Validation Reference

## Optimization decision table

| Symptom | First action | Do not do first |
|---|---|---|
| High allocation count | Pre-size bounded buffers; move admission to cold setup | Add a custom allocator or SIMD |
| Sparse state memory growth | Add preflight, lazy rows/nodes, compact IDs, staged admission | Build a dense fallback table |
| Slow row update/reduction | Check layout and scalar loop first; then compare existing SIMD kernels | Hand-write intrinsics without a scalar reference |
| Worker imbalance or nondeterminism | Fix trajectory partition, per-trajectory seed, and merge order | Add floating-point atomics or dynamic scheduling |
| Slow terminal/leaf work | Cache stable terminal data; reuse scratch; profile collision/evaluator cost | Cache graph-sized per-terminal matrices without a budget |
| Expensive range/action lookup | Precompute canonical IDs/menu mappings; use spans and indices | Hash or format keys in traversal |
| Excessive export latency/memory | Export selected root row; make diagnostics optional | Dense-export every policy row |

## Required profile evidence

Use a fixed fixture, model identity, seed, worker count, and row/memory limits. Capture before and after:

- wall and CPU time; p50/p95/p99 for request work when relevant
- time in sampling, graph/row admission, lookup, row math, terminal/leaf work, merge, and export
- allocated bytes/count, estimated memory, and peak resident memory
- admitted rows/nodes, trajectory counts, delta volume, and worker imbalance
- output policy/value equivalence under an explicit exact or tolerance rule

Profiling can perturb timings. Mark profiled runs and repeat an equivalent non-profiled run before making latency claims.

## Differential validation matrix

| Change | Minimum comparison |
|---|---|
| Row math or SIMD | Scalar versus SIMD across tails, zero/negative regret, and random rows |
| Worker/scheduler change | One worker versus fixed multi-worker deterministic runs |
| Terminal/utility change | Direct settlement versus traversal terminal callback |
| Range/blocker change | Canonical-ID, duplicate, normalization, zero-mass, and card-removal cases |
| New sampled path | Small exact/reference fixture, then deterministic replay |
| Serialization/artifact change | In-memory versus loaded row; identity mismatch and truncation cases |
| Resolver integration | Legacy behavior/fallback versus new path on eligible and ineligible requests |

State whether the expectation is bitwise equality, numeric tolerance, or normalized-policy equivalence. SIMD and parallel reduction order can change low floating-point bits; do not conceal that distinction.

## Solver safety boundaries

- Preserve `HUNLFlatDCFR` unless the user explicitly asks to change it.
- Do not make production sampled HUNL build the full flop graph or dense full tables.
- Keep public chance isomorphism disabled in sampled HUNL until range/private-hand remapping and equal-reach closure are established.
- Treat depth-cutoff evaluation as callback-owned with explicit unit/model identity.
- Keep model identity attached to artifact boundaries: rules, action abstraction, bucket model, terminal model, resolver schema, and code schema.
- For runtime solving, stop at the last clean batch when the deadline expires. Do not export partial worker mutation as a clean result.

## Performance gates

1. Do not optimize storage or use SIMD until scalar end-to-end semantics and a profile exist.
2. Do not enable relaxed scheduling until deterministic merging passes and quality differences are measured.
3. Do not add memory mapping until the in-memory loader and row identity are verified.
4. Do not replace baseline bucket artifacts with learned clustering before artifact validation is complete.
5. Do not remove a legacy path before differential tests and rollback/fallback behavior are proven.
