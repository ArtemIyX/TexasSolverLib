Task: Profile remaining traversal costs and evaluate topology-aware worker pinning; Codex can prepare diagnostic and placement changes, but an operator must run profiling captures and compare results.

# TH4-TH5 - Traversal profiling and topology-aware pinning

## Goal

Investigate remaining worker CPU costs only after checkpoint and coordinator
overhead are controlled, then determine whether topology-aware pinning helps.

## Scope

### TH4 - Traversal profiling

Use the diagnostic binary to inspect:

- dynamic public descriptor construction;
- history and action-menu allocation;
- discovery-stream copies;
- bucket lookup locality;
- terminal evaluation;
- false sharing in worker scratch and telemetry.

If descriptor allocation is confirmed, consider packed fixed-size discovery
records, parent-linked history, reusable worker buffers, and materialization
only at coordinator validation boundaries.

### TH5 - Topology-aware pinning

- Do not default to logical CPUs `0..N-1`.
- Identify physical cores and processor groups.
- Place workers on physical cores first.
- Keep the coordinator off the busiest worker core where possible.
- Report requested and resolved placement.
- Fall back to unpinned execution if topology discovery fails.

## Agent work

- Add only profiling zones justified by prior measurements.
- Keep detailed profiling out of production hot paths.
- Implement placement discovery without changing strategy identity.
- Preserve an unpinned fallback.

## Operator work

- Run post-admission Tracy captures.
- Compare normal and diagnostic binaries.
- Compare pinned and unpinned execution on the same workload.
- Provide CPU, throughput, tail-idle, placement, and correctness results.

## Acceptance

- Every optimization is supported by measured evidence.
- Pinning is retained only if it improves repeatable results.
- Worker and coordinator placement are reported accurately.
- Merge fingerprints and accepted trajectory counts remain identical.

## Dependency

TH3 must be complete. Do not optimize traversal or pinning while coordinator
admission remains the dominant batch cost.

## Implementation status

Completed 2026-10-23:

- Added Tracy zones for traversal bucket lookup, action-menu buffers, public
  descriptor construction, street transitions, terminal resolution, and leaf
  model/continuation evaluation.
- Added Linux physical-core discovery from the allowed CPU set and worker
  affinity through `sched_setaffinity`.
- Reused Windows processor-group placement and excluded the coordinator CPU
  when enough worker CPUs remain.
- Added requested and resolved processor telemetry with unpinned fallback.
- Added focused affinity and traversal-profile tests.

The full pinned-versus-unpinned operator profiling comparison remains
deferred. The short Tracy capture binary ran, but the external capture tool
did not connect.

The Linux affinity path is source-implemented but was not runtime-tested on
this Windows host.
