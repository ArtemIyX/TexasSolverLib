# Task F: Bound bucket artifact validation residency

## Trigger

`Fully implement F.`

## Objective

Preserve eager bucket artifact validation while preventing validation from
warming the complete persistent mapping into the process working set.

## Evidence gate

Task D must select F. The evidence must show that
`after_bucket_assignment_validation - after_bucket_map_metadata` is the
dominant working-set jump and that the corresponding private-byte jump is
comparatively small.

If D does not select F, do not implement this task.

## Required design

Preferred design:

1. Open the artifact and validate it using a bounded streaming buffer or
   bounded temporary file mappings.
2. Parse and retain compact table metadata and assignment offsets.
3. Validate every assignment using the same validity rules as the current
   parallel scan.
4. Release each temporary buffer/window after validation.
5. Establish the final read-only full-file mapping only after successful
   validation.
6. Point table assignment views at the final mapping without rescanning the
   assignment payload.

The validation buffer must have a fixed documented ceiling independent of
artifact size. A checksum can supplement validation but cannot replace the
range and blocked-hole contracts unless a separately versioned artifact
contract proves equivalence.

Do not use lazy validation unless bounded eager validation is impossible.

## Required invariants

- malformed headers, boards, counts, truncation, trailing data, and invalid
  bucket IDs are rejected at the same boundaries as the current loader;
- rejection occurs before the registry is returned;
- valid artifact table/model identity remains unchanged;
- lookup remains a direct mapped read after construction;
- no new branch or validation state enters the steady lookup hot path;
- Windows and Linux cleanup is exception-safe;
- mapping/file handles never leak;
- old supported artifact schema remains supported unless the user approves a
  version break.

## Likely files

- `src/solver/multiway/abstraction/multiway_bucket_artifact.cpp`
- `include/solver/multiway/abstraction/multiway_bucket_model.hpp`
- `src/solver/multiway/abstraction/multiway_bucket_model.cpp`
- `tests/test_multiway_bucket_artifact.cpp`
- `src/solver/multiway/engine/multiway_memory.cpp` only if mapping telemetry
  needs a narrow update.

Avoid changing bucket generation unless a versioned integrity section is
strictly required.

## Tests

Add fixtures for:

- valid artifact at buffer/window boundaries;
- truncated header, metadata, and assignment payload;
- trailing bytes;
- invalid board metadata;
- invalid bucket count;
- out-of-range assignment near the start, middle, and end;
- cleanup after an exception;
- table identity and lookup equivalence with the old loader;
- bounded validator memory independent of artifact size;
- final mapping exists and remains cold except for metadata pages before first
  lookup, where the platform test can observe this reliably.

Do not make unit tests depend on exact OS page-replacement timing.

## Execute

```powershell
.\scripts\codex_powershell.ps1 .\scripts\python\compact_build.py `
  --target test_multiway_bucket_artifact `
  --target test_multiway_bucket_generation `
  --target test_multiway_memory `
  -o tmp\memory-task-f-build.log

ctest --test-dir build -C Debug --output-on-failure `
  -R "^(test_multiway_bucket_artifact|test_multiway_bucket_generation|test_multiway_memory)$"

.\scripts\codex_powershell.ps1 .\scripts\python\full_build.py
```

Rerun Task D cases A1 and A3 for three normal repeats and one Tracy capture.
Use the same artifact, build configuration, seed, and workload as the baseline.

## Acceptance

- All corruption tests pass with equivalent rejection coverage.
- The final registry and lookups match baseline results.
- Peak and after-validation working set decrease beyond Task D noise.
- Private committed bytes do not materially increase.
- Training has zero discards and matching fingerprint.
- Throughput does not materially regress.
- Full build/CTest and C++ review pass.
- Project log records before/after medians, ranges, and artifact size.

## Completion statement

Task F is complete only after bounded before/after runtime evidence proves the
working-set reduction and all validation contracts pass.

## Stop conditions

Stop if eager corruption detection cannot be preserved, if window cleanup is
not exception-safe, if final lookup needs a validation branch, or if the
working-set change is within baseline noise.

