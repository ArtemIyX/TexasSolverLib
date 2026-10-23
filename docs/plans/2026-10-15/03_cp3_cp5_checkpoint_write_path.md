Task: Remove redundant checkpoint scans, buffer serialization, and avoid Windows full-file publication copies; Codex can implement source changes, but an operator must run compatibility and Windows performance checks.

# CP3-CP5 - Checkpoint write path

## Goal

Reduce checkpoint CPU and I/O overhead without changing schema-2 bytes,
resume state, or atomic publication guarantees.

## Scope

### CP3 - Sealed checkpoint path

- Validate the constructed snapshot once.
- Compute the payload hash while encoding.
- Remove a separate full-array hash pass.
- Keep public `save_atomic()` validation for arbitrary callers.
- Use the sealed path only from the trusted training workflow.

### CP4 - Buffered serialization

- Replace per-byte `ostream::put()` calls with a 64 KiB to 1 MiB buffer.
- Encode arrays in blocks.
- Update the payload hash during encoding.
- Check stream state after each block.
- Do not add compression in this task.

### CP5 - Windows publication

- Report whether rename or copy fallback was used.
- Use a checked Windows replacement operation for existing destinations.
- Avoid copying the complete temporary file a second time.

## Agent work

- Preserve little-endian schema-2 encoding exactly.
- Add invalid/truncated/non-finite rejection coverage.
- Keep temporary-file and atomic replacement behavior explicit.

## Operator work

- Run byte-compatibility and save/load tests.
- Run replacement tests with an existing checkpoint.
- Run the Windows diagnostic workflow and provide publication mode and timing.

## Acceptance

- Identical payload hash for compatible data.
- Resumed training state is identical.
- Existing destination replacement remains safe.
- Buffered output materially improves write throughput.
- No schema change is introduced.

## Dependency

CP1 and CP2 should land first. CP6, the optional compact schema, is deferred
unless this task is insufficient.
