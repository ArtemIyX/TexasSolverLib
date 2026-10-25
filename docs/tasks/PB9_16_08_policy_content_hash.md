# PB9.16.08 Policy-content hash

Status: DONE  
Owner: Codex  
Started: 2026-10-25  
Completed: 2026-10-25  
Depends on: PB9.16.01

## Scope

- Add a canonical hash over policy rows only.
- Retain the existing full payload hash and worker-sensitive execution identity.
- Report and validate both hashes.

## Implementation

- Added versioned canonical policy-content hashing for materialized and streamed full-blueprint rows.
- Canonicalized materialized rows by `RowKey` while retaining the full payload hash's original execution-sensitive inputs.
- Propagated the policy hash through export results, telemetry, training reports, and lookup qualification reports.
- Preserved schema-2 artifact bytes and existing checkpoint/full-payload compatibility checks.
- Added focused tests for metadata and row-order independence, payload/hash distinction, and report propagation.

Implementation commits:

- `d60654c PB-9 Add policy-content hash: Preserve full payload compatibility`
- `4583804 PB-9 Add policy-content report field: Expose canonical hash`

## Files

- blueprint trainer and checkpoint/artifact files
- training report files
- blueprint-training and qualification-report tests

## Acceptance

- Identical policy rows can be compared across worker counts without weakening checkpoint compatibility.
- Existing schema-2 artifact bytes remain unchanged unless a separately approved schema change is required.

## Validation

- Focused artifact, blueprint-training, qualification-report, and F1 acceptance tests passed.
- `scripts/codex_powershell.ps1 python scripts/python/full_build.py`: passed with the build-only `/FS` configuration.
- `scripts/codex_powershell.ps1 python scripts/python/compact_ctest.py`: passed; all tests passed.
- `git diff --check`: passed.
- Independent C++ review: no actionable findings.

## Limitations

- No tournament delta merge, qualification matrices, benchmarks, or solver training were run.
