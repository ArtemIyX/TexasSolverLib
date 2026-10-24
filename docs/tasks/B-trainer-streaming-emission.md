# Task B: Trainer streaming emission

## Objective

Emit full-policy rows directly from trained compact and Float64 storage through Task A's writer, without building a `MultiwayFullBlueprintArtifact` in production.

## Dependencies

Task A.

## Scope

- Add allocation-free cold `average_strategy_into()` APIs to compact and Float64 storage with the current arithmetic preserved statement-for-statement.
- Add exclusive export leasing, no-lock packed-menu views, and trainer/session `save_full_policy_atomic()`.
- Build an exact pre-write plan: row/action counts, serialized bytes, ordering metadata, and overflow checks.
- Emit compact rows in existing public-state-major/seat-major order. Emit Float64 rows through a sorted 32-bit metadata-index vector only.
- Generate strategy and quantized action data in fixed reusable scratch. Keep `export_full_policy()` as compatibility/test coverage.

## Required invariants

- Do not change traversal, merge, admission, Task G capacities, or quantization bytes.
- Export must exclude checkpointing and training mutation.
- Compact ordering uses no order-index allocation; Float64 never sorts row payloads.
- Quantization must use existing double operations, normalization, floor, and clamp order.

## Tests

Update `tests/test_multiway_compact_storage.cpp` for `_into` bit equivalence. Update `tests/test_multiway_blueprint_training.cpp` for compact/Float64 quantization equivalence, direct/index ordering, streamed/materialized equivalence, export/checkpoint exclusion, and repeated deterministic export.

## Validation

Run focused compact-storage and blueprint-training builds/tests, then full build, compact CTest, `git diff --check`, and C++ review.

## Acceptance

- Production training export has no materialized row/action artifact.
- Materialized and streaming exports match exactly for identical trained state.
- Fixed scratch and Float64 ordering metadata remain bounded by action count and shape count respectively.

## Stop conditions

Stop if quantization differs, a full row payload must be sorted/materialized, or export exclusion is not enforceable.
