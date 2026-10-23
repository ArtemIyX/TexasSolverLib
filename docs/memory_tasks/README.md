# Multiway memory task index

Use these files as standalone implementation prompts. A request such as
`fully implement A` means complete the whole task file, including production
code, focused tests, review, prescribed verification, documentation, and the
project-log update.

Current state and the next action are maintained in [TRACKER.md](TRACKER.md).
Every agent must update the tracker before starting and when finishing or
becoming blocked.

Read `docs/multiway_memory_measurement_and_reduction_plan.md` before starting
any task.

## Execution order

| Task | Title | Dependency | Kind |
| --- | --- | --- | --- |
| A | Process memory and phase snapshots | None | Required foundation |
| B | Retained subsystem capacity attribution | A | Required foundation |
| C | Tracy allocation lifetime capture | A | Required foundation |
| D | Bounded attribution matrix and analyzer | A, B, C | Required evidence |
| E | Correct memory preflight accounting | D | Required accounting fix |
| F | Bound bucket validation residency | D selects F | Optimization candidate |
| G | Right-size worker stream capacities | D selects G | Optimization candidate |
| H | Page compact row metadata | D selects H | Optimization candidate |
| I | Release oversized admission scratch | D selects I | Optimization candidate |
| J | Reduce checkpoint/export peak memory | D selects J | Optimization candidate |
| K | Final bounded memory qualification | E and selected candidates | Closure |

Tasks A through E are sequential. Tasks F through J are not a sequence. D must
select them using measured evidence. Implement one candidate, measure it, then
select another only if the remaining evidence supports it.

## Common workflow for every implementation task

1. Read `AGENTS.md`, `CODEX_TOKEN_EFFICIENT_WORKFLOW.md`, the master memory
   plan, this index, and the selected task file.
2. Inspect `git status --short --ignore-submodules=all`. Preserve unrelated
   changes.
3. Use the project skills required by `AGENTS.md`:
   - `project-navigation` for discovery;
   - `cpp-worker` for production C++;
   - `cpp-unit-tests` for tests;
   - `cpp-code-review` after implementation;
   - `update-project-log` after the task is genuinely complete;
   - `git-commit-hygiene` only when the user requests a commit.
4. Implement only the selected task. Do not opportunistically start the next
   task.
5. Keep hot paths allocation-free and free of formatting, logging, hash-map
   lookup, virtual dispatch, and exceptions as normal control flow.
6. Run focused verification first. Run `full_build.py` last when required by
   the task.
7. Review the final diff and record actual results in
   `docs/PLURIBUS_LOG.md`.
8. Do not stage or commit unless explicitly requested.

## Common completion contract

A task is complete only when:

- all required production changes are present;
- all required focused tests exist and pass;
- prescribed build and CTest verification passes;
- runtime evidence required by that task passes;
- deterministic fingerprints and other stated invariants match;
- an independent C++ review has no unresolved actionable findings;
- `git diff --check` passes;
- the newest project-log entry states scope, actual validation, and
  limitations;
- no unrelated file is modified.

If required runtime artifacts or tools are missing, the task is blocked, not
complete. Report the exact missing prerequisite and stop.

## Common commands

Run Python scripts through the repository wrapper:

```powershell
.\scripts\codex_powershell.ps1 .\scripts\compact_build.py <arguments>
.\scripts\codex_powershell.ps1 .\scripts\compact_ctest.py <arguments>
.\scripts\codex_powershell.ps1 .\scripts\full_build.py
```

Do not run the 50-million-trajectory workload in tasks A through J.

