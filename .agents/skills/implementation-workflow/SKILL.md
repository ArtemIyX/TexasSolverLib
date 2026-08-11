---
name: implementation-workflow
description: Orchestrate complete TexasSolver C++ implementation tasks from repository navigation through focused tests, independent review, project-log update, and a clean commit. Use when the user says "implement a change" or asks to add, modify, fix, or optimize C++ code in TexasSolver and expects the standard project workflow. Do not use for explanation-only, inspection-only, or standalone Git requests.
---

# TexasSolver Implementation Workflow

Complete the requested implementation as one cohesive, committed change. Do not run builds, tests, benchmarks, installs, or solver jobs unless the user explicitly asks.

## Required workflow

1. Use `$project-navigation` to locate the owning API, implementation, callers, nearest tests, examples, and placement. Search with `rg` before reading broadly.
2. Inspect branch, status, staged diff, and unstaged diff. Treat pre-existing unrelated changes as user-owned. Do not alter, stage, or commit them.
3. Use `$cpp-worker` to implement the narrowest coherent change. Follow `AGENTS.md`, preserve contracts, and apply hot-path, memory, determinism, and legacy-HUNL rules.
4. Use `$cpp-unit-tests` to add or update focused coverage for each changed observable behavior and confirmed regression. Do not run it unless explicitly requested.
5. Use `$cpp-code-review` on the implementation and its focused tests. If it reports a finding, fix the minimal root cause, update tests where needed, and review the affected patch again. Continue until there are no actionable findings.
6. Use `$update-project-log` after the task is complete. Add a factual newest-first entry to `docs/PLURIBUS_LOG.md`, including only files changed for this task and only validation actually run. State that validation was not run when it was not requested.
7. Use `$git-commit-hygiene` to inspect the final scope, stage only task files, review the staged diff, and create one clean commit. The invocation of this workflow authorizes that commit. Do not include unrelated work.

## Boundaries

- Stop and ask for direction when the requested behavior, compatibility contract, or scope is materially ambiguous.
- Do not make destructive Git changes, rewrite history, or push unless explicitly requested.
- Report changed files, tests added or updated, review result, validation status, and commit hash.
- For a request that explicitly excludes tests, log the stated reason and do not add test files.