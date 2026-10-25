# Task A: Artifact streaming writer

## Objective

Provide the schema-2-compatible, bounded `MultiwayFullBlueprintArtifact` stream-writer foundation. Preserve the materialized API and loader behavior.

## Dependencies

None.

## Scope

- Add plan/result and non-owning row-view types in `multiway_artifact.hpp`.
- Add one shared incremental logical payload hasher for materialized and streamed paths.
- Add bounded little-endian row encoding, streamed row validation, emitted-key order/uniqueness checks, byte/count verification, and RAII `.tmp` cleanup.
- Remove `save_atomic()`'s artifact deep copy. Validate materialized rows in place; use only a 32-bit row-index vector when unsorted input requires duplicate detection.
- Make full-blueprint publication reject any non-atomic copy fallback before it can replace a destination.

## Required invariants

- Keep `MWFB0002`, schema version 2, field order/widths, and trailing payload hash byte-identical.
- Hash canonical logical `uint64_t` fields, not serialized bytes.
- Never materialize all streamed rows, actions, or validation-store rows.
- On any generation, validation, write, close, or publish failure, remove `.tmp` and preserve the previous destination.

## Tests

Update `tests/test_multiway_artifact.cpp` for golden schema-2 bytes, materialized/streamed byte and hash equivalence, incremental hash equivalence, malformed/duplicate/unsorted row rejection, injected write/close/publication failures, cleanup, destination preservation, and loader compatibility.

## Validation

Run the focused artifact build/test, then the repository full build, compact CTest, `git diff --check`, and the C++ review required by the architecture.

## Acceptance

- Existing materialized artifacts and new streamed artifacts are byte-identical for the same rows.
- The materialized save path no longer owns a deep artifact copy.
- Failure paths leave no temp file and never partially replace the destination.
- No row-count-proportional allocation is introduced to the streaming path.

## Stop conditions

Stop if schema-2 bytes/hash change, loader compatibility breaks, or strict atomic publication cannot be guaranteed for a missing destination.
