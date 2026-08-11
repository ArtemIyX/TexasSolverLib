# P2.3 Execution Plan: Session-Local Deterministic Search Rows

**Status:** Complete
**Roadmap task:** P2.3 - Wire worker-local deltas into session rows
**Prerequisites:** P0.1-P0.4, P1.1-P1.4, P2.1-P2.2
**Scope:** Multiway runtime search only. No HUNL behavior changes.

## Decision

Implement P2.3 before beginning P3. The resolver currently creates a request-local
`MultiwaySearchSession`, but row ownership and post-merge observability are implicit
through `MultiwaySolverCoordinator`. Make that boundary explicit, measurable, and
testable before a full-blueprint artifact depends on it.

The session owns mutable search rows. Workers own only preallocated delta streams.
Only the coordinator merge applies deltas, in worker-index then trajectory-ID order.
Rows are never shared between requests and no partial batch is exported.

## Non-goals

- Do not change exact HUNL or `HUNLFlatDCFR`.
- Do not introduce floating-point atomics, shared worker writes, or dynamic
  scheduling.
- Do not add full-blueprint serialization. That starts in P3.1.
- Do not change resolver default behavior outside opt-in search modes.

## Commit plan

1. `docs(pluribus): define P2.3 session-row contract`
   - Add ownership, merge-order, and numerical-comparison rules to the roadmap.

2. `test(multiway): characterize root row admission`
   - Capture row count, value count, and root-row availability after one clean batch.

3. `feat(multiway): expose session storage views`
   - Add read-only session accessors for row/value counters and root-row lookup.

4. `test(multiway): cover session storage view bounds`
   - Reject invalid infosets, stale row handles, and unavailable root rows.

5. `refactor(multiway): name session-local row ownership`
   - Move resolver references from coordinator internals to explicit session APIs.

6. `test(multiway): assert no cross-session row visibility`
   - Run equal roots with distinct seeds/ranges and confirm separate row ownership.

7. `feat(multiway): add batch merge accounting`
   - Record admitted, updated, rejected, and merged delta counts per clean batch.

8. `test(multiway): verify merge accounting conservation`
   - Verify worker-stream totals match merge results and discarded streams change no rows.

9. `feat(multiway): add immutable clean-batch snapshot`
   - Capture root policy, counters, and root revision only after a successful merge.

10. `test(multiway): reject incomplete batch snapshots`
    - Ensure failed workers, zero accepted trajectories, and over-limit batches expose none.

11. `refactor(multiway): export policy from clean snapshot`
    - Make runtime search export the latest accepted snapshot, not live coordinator state.

12. `test(multiway): preserve last clean snapshot on expiry`
    - Force expiration after a clean batch and verify the earlier policy remains intact.

13. `feat(multiway): persist deterministic merge sequence metadata`
    - Store worker count, first trajectory, count, and merge sequence in diagnostics only.

14. `test(multiway): replay identical worker layout`
    - Require bitwise-identical row snapshots and root policies for identical inputs.

15. `test(multiway): compare one and multi-worker layouts`
    - Define and enforce the documented tolerance for differing floating-point reduction order.

16. `feat(multiway): add fixed-order delta merge helper`
    - Centralize worker-index then trajectory-ID stream validation before coordinator merge.

17. `test(multiway): reject malformed delta ordering`
    - Exercise duplicate, descending, wrong-worker, and foreign-coordinator stream cases.

18. `refactor(multiway): preflight session row capacity`
    - Reject unsafe row/value capacity before traversal begins and avoid mid-merge growth.

19. `test(multiway): cover capacity boundary fallback`
    - Verify max-row/max-value exhaustion leaves the previous clean snapshot unchanged.

20. `feat(multiway): add root-row export provenance`
    - Include root revision and accepted batch identity in search diagnostics without private data.

21. `test(multiway): cover resolver session-row provenance`
    - Verify active and shadow modes report distinct provenance while legacy output is unchanged.

22. `docs(multiway): document P2.3 determinism contract`
    - Update release-facing documentation with clean snapshot and worker-layout semantics.

23. `test(multiway): add repeated bounded-search regression fixture`
    - Run fixed public fixtures repeatedly to detect accidental state retention between requests.

24. `docs(pluribus): record completed P2.3 verification`
    - Update the progress log only after code review and the requested verification pass.

## File ownership map

| Area | Expected files |
| --- | --- |
| Session API | `include/solver/multiway_search_session.hpp`, `src/solver/multiway_search_session.cpp` |
| Traversal and merge | `include/solver/multiway_traversal.hpp`, `src/solver/multiway_traversal.cpp` |
| Sparse row boundary | `include/solver/multiway_solver.hpp`, `src/solver/multiway_solver.cpp` |
| Resolver export | `include/solver/multiway_resolver.hpp`, `src/solver/multiway_resolver.cpp` |
| Regression coverage | `tests/test_multiway_search_session.cpp`, `tests/test_multiway_recursive_traversal.cpp`, `tests/test_multiway_resolver.cpp` |
| Roadmap/progress | `docs/implementation_roadmap.md`, `docs/PLURIBUS_LOG.md` |

## Acceptance gates

1. No worker mutates coordinator/session rows directly.
2. Each accepted batch has a complete, immutable export snapshot.
3. A failed, cancelled, or capacity-rejected batch changes neither exported policy nor
   its recorded snapshot.
4. Identical request, seed, worker count, and limits reproduce row snapshots bitwise.
5. Different worker counts either meet the documented numerical tolerance or report a
   reproducible difference. They must never be described as bitwise equivalent by default.
6. Diagnostics contain no cards, ranges, raw worker deltas, or sampling seeds.
7. Active search, shadow search, and fallback retain their established resolver semantics.

## Exit and follow-up

The session-row snapshot boundary, resolver export path, focused regressions,
code review, and project-log entry are complete. P2.4 can expand shadow diagnostics
using the clean-snapshot provenance. P3.1 remains blocked on this contract because a
blueprint runtime reader must not rely on live mutable search rows.
