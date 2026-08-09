---
name: development-workflow
description: Orchestrate scoped software changes with local subagents, parallel correctness/performance/unit-test reviews, tests, verification, concise reporting, and small focused commits. Use when implementing, fixing, refactoring, or reviewing a repository change that benefits from a repeatable multi-agent workflow.
---

# Development Workflow

Use this workflow for a bounded repository change. Read repository instructions
and inspect the worktree before assigning tasks. Preserve unrelated changes.

## Roles

Use short, bounded prompts. Give each agent its permitted actions and a clear
deliverable.

| Role | Purpose | May edit? |
| --- | --- | --- |
| Explorer | Locate code paths, contracts, tests, risks, and dirty files. | No |
| Architect | Turn evidence into a small implementation plan and acceptance criteria. | No |
| Worker | Implement one approved scope. | Yes |
| Correctness reviewer | Find behavioral, API, safety, concurrency, and compatibility defects. | No |
| Performance reviewer | Inspect hot paths, allocation, complexity, memory, and scheduling. | No |
| Unit-test reviewer | Find missing coverage and weak assertions. | No |
| Fix worker | Fix consolidated review findings. | Yes |
| Unit-test worker | Add regression tests for fixed findings and acceptance criteria. | Yes |
| Test-fixer | Run only user-authorized verification and fix feature-caused failures. | Yes |
| Final verifier | Check diff scope, test results, documentation, and release readiness. | No |
| Git expert | Stage only scoped files and create focused commits. | Yes, Git only |

## Workflow

1. **Explorer**: inspect repository instructions, current behavior, tests,
   relevant files, and `git status`. Report evidence and risks.
2. **Architect**: define the smallest useful scope, unchanged contracts,
   files likely affected, acceptance criteria, and validation commands. Skip
   only for a trivial one-file change.
3. **Worker**: implement the approved scope. Do not run builds or tests unless
   authorized. Do not commit.
4. **Reviewers in parallel**: after the worker stops editing, run correctness,
   performance, and unit-test reviews simultaneously. Reviewers do not edit
   or run expensive commands.
5. **Fix worker**: consolidate actionable findings, fix them in one pass, and
   report each disposition. Repeat the relevant review if a fix is material.
6. **Unit-test worker**: add tests for every fixed bug and the accepted
   behavior. Keep tests deterministic and scoped. Do not hide a production bug
   with a weaker assertion.
7. **Test-fixer**: run the repository's user-authorized build and test
   commands. Fix only failures caused by the scoped change. Re-run affected
   verification until clean. Record commands, failures, and fixes.
8. **Final verifier**: confirm requirements, test evidence, documentation,
   no accidental API/privacy regressions, and a clean scoped diff. Do not
   reimplement the feature.
9. **Git expert**: create small focused commits. Stage only the scoped files,
   run a staged diff check, and never include unrelated dirty files.
10. **Report**: lead with outcome. List feature scope, commits, tests written,
    commands and results, failures fixed, deferred work, and unrelated changes
    intentionally left untouched.

## Parallelism and ownership

- Run the three reviewers in parallel only after implementation is stable.
- Never run two editing agents concurrently in the same files.
- Explorer and architect may run in parallel only when their inputs do not
  overlap; architect must wait for exploration evidence before finalizing scope.
- Keep a single fix worker and a single test writer for each change.
- Use a final verifier after all edits and before committing.

## Commit policy

- Make one small commit per independently reviewable change.
- Keep implementation and its regression tests together when they form one
  behavior change.
- Put documentation-only updates in a separate commit when they are not needed
  to explain the code change.
- Do not commit generated output, local configuration, or unrelated user work.

## Guardrails

- Respect repository instructions, permissions, and explicit test limits.
- Prefer read-only exploration first.
- Preserve legacy behavior unless the task explicitly changes it.
- Treat reviewer findings as hypotheses; verify them before fixing.
- Do not claim success until required verification passes.
