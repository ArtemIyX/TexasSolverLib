# Prompt for the Tracy Integration Planning Agent

Copy the prompt below into the next AI-agent task.

---

You are a senior C++17 performance-instrumentation architect working in the TexasSolver repository. Create a detailed implementation plan for integrating the Tracy profiler into the project. Do not implement code yet. The output must be an evidence-based plan tied to the current repository state.

## Objective

Determine whether and how Tracy should be added to TexasSolver so that we can diagnose multiway training scaling and memory behavior, especially why throughput currently does not improve monotonically with worker count. The desired direction is to make measurements actionable while preserving solver correctness, determinism, memory limits, and production performance when profiling is disabled.

## Current context

The repository already contains:

- An explicit CMake source and installed-header classification in `CMakeLists.txt`.
- A lightweight internal profiling utility in `include/util/profiling.hpp` and `src/util/profiling.cpp`.
- Multiway training and traversal code under `include/solver/multiway` and `src/solver/multiway`.
- Worker scheduling in `multiway_traversal.hpp/.cpp` and `multiway_scheduler.hpp/.cpp`.
- Multiway batch/report telemetry in:
  - `include/solver/multiway/engine/multiway_traversal.hpp`
  - `src/solver/multiway/engine/multiway_traversal.cpp`
  - `include/solver/multiway/blueprint/multiway_blueprint_trainer.hpp`
  - `src/solver/multiway/blueprint/multiway_blueprint_trainer.cpp`
  - `include/solver/multiway/workflow/multiway_training_report.hpp`
  - `src/solver/multiway/workflow/multiway_training_report.cpp`
- PowerShell measurement wrappers:
  - `scripts/run_multiway_ram_1000.ps1`
  - `scripts/run_multiway_worker_matrix.ps1`
- A multiway scaling benchmark at `examples/benchmarks/multiway_training_scaling_main.cpp`.
- Existing JSON/CSV reports and deterministic fingerprint gates.
- Windows as the primary development environment, with CMake support for other platforms.
- A requirement to keep exact-mode behavior and deterministic multiway merge identity unchanged.

Verify all of these facts against the current worktree. Do not rely on this prompt if the source has changed.

## Required investigation

Inspect and report:

1. The current build graph and target boundaries.
   - `texas` static library.
   - Test-support sources versus production sources.
   - Research and benchmark targets.
   - Existing options for Debug, Release, profiling, and research builds.
   - Where a Tracy dependency can be added without leaking test-only or research-only code into the stable library.

2. The current profiling and telemetry architecture.
   - Existing profiling macros and their runtime cost.
   - Multiway worker-loop, traversal, scheduler, coordinator admission, merge, and trainer phase boundaries.
   - Current report schema and artifact paths.
   - Existing allocation, CPU-time, RSS, working-set, affinity, and per-worker measurements.
   - Which metrics Tracy can replace, complement, or should not duplicate.

3. Tracy integration alternatives.
   Compare at least:
   - Vendoring Tracy as a pinned source dependency.
   - Adding Tracy through an external package or FetchContent.
   - Keeping Tracy optional and only linking it into benchmark/research targets.
   - Using Tracy client instrumentation in the stable library with a compile-time disabled path.
   - Using Tracy zones only in the benchmark executable while retaining lightweight production counters.

   For each alternative, evaluate reproducibility, offline builds, Windows/MSVC support, Linux support, dependency ownership, licensing, binary size, link-time impact, runtime overhead, and maintenance cost.

4. Tracy features relevant to this project.
   Evaluate the need for:
   - CPU zones and zone naming conventions.
   - Worker thread naming and thread registration.
   - Plot markers for trajectories, delta sizes, discoveries, allocations, RSS, and CPU time.
   - Lock and condition-variable tracing.
   - Memory allocation hooks and their safety with the current allocation probe.
   - Sampling versus instrumentation overhead.
   - Call stacks, fibers, frame markers, and GPU features only if justified by repository evidence.
   - Tracy server/version compatibility and how trace files or captures should be retained.

## Instrumentation placement to analyze

For each proposed zone or plot, identify the exact owning function and explain why it belongs there. At minimum, consider:

- `MultiwayRootBatchRunner::worker_loop()`.
- Worker CV wait and wake-up boundaries.
- Worker traversal execution around `MultiwayRootExternalSamplingTraversal::run()`.
- Delta sorting.
- Worker completion signaling and tail-idle intervals.
- Immutable discovery collection and coordinator admission.
- Deterministic worker-stream merge.
- Trainer discounting/pruning and other trainer-only work.
- Process CPU and working-set sampling boundaries.
- Worker allocation-probe scopes.
- The benchmark and PowerShell wrappers that retain JSON/CSV evidence.

Do not place Tracy zones inside per-action, per-bucket, per-update, or other very hot inner loops unless the plan proves the overhead is acceptable and proposes a disabled/default-off path. Preserve the repository hot-path rules: no strings, formatting, logging, heap allocation, hashing, virtual dispatch, or per-item clock reads in hot loops.

## Required design decisions

The plan must make explicit recommendations for:

- Tracy version pinning and dependency location.
- CMake options and defaults, including the exact option names and which targets receive Tracy.
- Whether profiling is enabled at compile time, runtime, or both.
- How a disabled Tracy build becomes a near-zero-cost no-op.
- How to prevent profiling from changing deterministic seeds, scheduling, merge order, checkpoint contents, or report identity.
- How to avoid profiling overhead invalidating throughput measurements.
- How to distinguish profiled diagnostic runs from unprofiled performance runs.
- How allocations from Tracy itself are excluded from solver allocation measurements.
- How Windows processor/affinity data and Tracy worker names should be represented.
- How traces relate to, but do not replace, JSON/CSV telemetry.
- How to avoid duplicate symbols or dependency conflicts between production, test-support, and benchmark targets.
- Whether Tracy should be available in normal training binaries or only in a dedicated diagnostic binary.

## Required deliverables

Produce a plan with these sections:

1. Executive recommendation.
2. Evidence found in the current repository, with file and symbol references.
3. Alternatives considered and rejected.
4. Proposed target/dependency architecture.
5. Exact file-by-file change list.
6. Proposed CMake options, target links, compile definitions, and default behavior.
7. Tracy zone/thread/plot naming scheme.
8. Runtime and memory overhead controls.
9. Interaction with current JSON/CSV telemetry and schema versioning.
10. Test and validation plan.
11. Benchmark protocol, including profiled versus unprofiled runs.
12. Rollback and failure criteria.
13. Ordered implementation tickets, each small enough for a separate commit.
14. Risks, open questions, and assumptions.

Each ticket must include:

- Goal.
- Exact files and symbols.
- API/configuration changes.
- Determinism and hot-path impact.
- Focused tests or static checks.
- Build/verification commands from the repository instructions.
- Expected artifacts under `tmp/`.
- Acceptance criteria.
- Rollback condition.

## Constraints

- Do not modify code while producing the plan.
- Do not run builds, tests, benchmarks, installs, or solver jobs unless explicitly authorized by the user.
- Do not assume network access or an installed Tracy server.
- Do not replace existing correctness gates with Tracy output.
- Do not recommend optimizing a bottleneck until the trace and existing telemetry can demonstrate it.
- Preserve C++17 compatibility.
- Preserve exact HUNL paths and unrelated solver behavior.
- Keep raw trace artifacts under `tmp/` and do not commit generated captures.
- Follow repository `AGENTS.md`, `CODEX_TOKEN_EFFICIENT_WORKFLOW.md`, and the applicable C++ skills.

End with one concrete recommendation for the first implementation ticket and explain why it is the smallest safe step.

---
