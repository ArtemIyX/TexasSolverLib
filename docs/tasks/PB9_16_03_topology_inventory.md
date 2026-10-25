# PB9.16.03 Topology inventory

Status: DONE
Depends on: PB9.16.01

Owner: Codex
Started: 2026-10-25
Completed: 2026-10-25
Implementation commit: `1014b4d PB-9 Add topology inventory: Discover Windows and synthetic identities`

## Scope

- Add a cold-path, synthetic-testable topology record model.
- Discover Windows CPU-set, core, SMT, NUMA, and cache-domain identities.
- Preserve a documented fallback when Windows topology APIs are unavailable.

## Implementation

- Added a validated, deterministically ordered topology inventory with unique logical processor identities.
- Added Windows CPU-set discovery and `GetLogicalProcessorInformationEx` core relationship discovery.
- Kept physical-core identity separate from SMT-sibling index.
- Preserved NUMA and `LastLevelCacheIndex` values as OS-reported facts. No V-Cache or cache-name inference is performed.
- Added a host-independent fallback that reports logical identities and leaves unavailable topology identities unknown.
- Kept discovery as a cold-path API with no scheduler, placement, merge, checkpoint, artifact, or telemetry integration.

## Files

- `include/solver/multiway/engine/multiway_worker_topology.hpp`
- `src/solver/multiway/engine/multiway_worker_topology.cpp`
- `CMakeLists.txt`
- `tests/test_multiway_parallel_training.cpp`

## Acceptance

- Tests can model topology without host-machine dependence.
- Physical-core and SMT-sibling identities are distinguishable.
- Cache-domain values are reported as Windows topology facts, never inferred V-Cache labels.

## Validation

- Focused `test_multiway_parallel_training`: passed.
- `scripts/codex_powershell.ps1 python scripts/python/full_build.py`: passed.
- `scripts/codex_powershell.ps1 python scripts/python/compact_ctest.py`: passed.
- `git diff --check`: passed.
- C++ review: no actionable findings.

## Limitations

- No safe placement policy, coordinator reservation, scheduler, benchmark, qualification matrix, or solver training was implemented or run.
- Windows discovery is exposed for later opt-in consumers; existing worker behavior remains unchanged.
