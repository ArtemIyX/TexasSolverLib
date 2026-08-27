# Pluribus Architecture Audit: Round 3

Findings: 2.

[R3] P1 — Parallel continuation learning is scheduler-dependent.
Location: `src/solver/multiway_traversal.cpp:280`
Evidence: Every worker reads the shared continuation row with `strategy`, evaluates four policies, and later mutates that row with `update_regrets` at line 292. The two operations take separate mutex acquisitions. With `worker_count > 1`, another trajectory can update the row between them, and competing updates are applied in thread arrival order rather than trajectory order. Identical seeds and trajectories can therefore use different mixtures and produce different later regret updates across runs.
Impact: Multithreaded search is not reproducible and violates the deterministic worker-local-delta and fixed-order-merge contract. Root policy can vary with OS scheduling.
Fix: Emit worker-local continuation deltas tagged with trajectory IDs and merge them in fixed trajectory order at the batch boundary. Derive each trajectory's continuation mixture from a deterministic batch snapshot.

[R4] P1 — Same-street exact rows bypass the configured memory limit.
Location: `src/solver/multiway_solver.cpp:694`
Evidence: For every row whose bucket count equals `MULTIWAY_HOLE_COMBINATION_COUNT`, `admit_infoset_row` raises `max_values_` to the current storage size plus that row's full value count before calling `admit_row`. A search configured and preflighted for one exact root row can visit many same-street descendant infosets; each new descendant raises the ceiling again, so the `max_sparse_values` admission check can never reject those exact rows.
Impact: Search can allocate far beyond its admitted sparse-value budget and memory preflight estimate, risking deadline failure, paging, or allocation failure during runtime solving.
Fix: Include the bounded number and action widths of all possible same-street exact rows in preflight, then set one immutable admitted capacity before traversal. Do not increase storage capacity during row admission.

R1: fixed
R2: fixed
R3: remaining — continuation rows are learned and mixed, but parallel updates are not deterministic.

Builds and tests were not run.
