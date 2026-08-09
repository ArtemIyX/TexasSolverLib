---
name: development-workflow
description: Orchestrate scoped software changes with a fixed local subagent roster, parallel correctness/performance and unit-test reviews, tests, verification, concise reporting, and small focused commits. Use when implementing, fixing, refactoring, or reviewing a repository change that benefits from a repeatable multi-agent workflow.
---

# Development Workflow

Use this workflow for a bounded repository change. Read repository instructions
and inspect the worktree before assigning tasks. Preserve unrelated changes.

## Fixed agent roster

Use only these exact task names. Reuse an existing agent with a follow-up task
when it is idle. Do not create role-specific agent names.

| Exact task name | Responsibilities | May edit? |
| --- | --- | --- |
| `root` | Explorer, architect, final verifier, and report. | Yes, only when needed |
| `cpp_pro` | Production worker and fix worker. | Yes |
| `code_reviewer` | Correctness and performance review. | No |
| `test_automator` | Unit-test review and regression-test author. | Yes, tests only |
| `build_test_fixer` | User-authorized build/test execution and scoped fixes. | Yes |
| `git_expert` | Focused staging and commits. | Yes, Git only |

Use short, bounded prompts. State permitted actions and expected deliverables.
If a named agent does not exist, create it with the exact name above. Reuse it
afterward. Do not create variants such as `code_reviewer_p0` or `worker_2`.

## Workflow

1. **`root` as explorer**: inspect repository instructions, current behavior,
   tests, relevant files, and `git status`. Report evidence and risks.
2. **`root` as architect**: define the smallest useful scope, unchanged
   contracts, files likely affected, acceptance criteria, and validation
   commands. Skip only for a trivial one-file change.
3. **`cpp_pro`**: implement the approved scope. Do not run builds or tests
   unless authorized. Do not commit.
4. **Reviews in parallel**: after edits stabilize, run `code_reviewer` for
   correctness/performance and `test_automator` for unit-test review. Neither
   edits during this review pass or runs expensive commands.
5. **`cpp_pro` as fix worker**: consolidate actionable findings, fix them in
   one pass, and report each disposition. Repeat the relevant review if a fix
   is material.
6. **`test_automator` as unit-test worker**: add tests for every fixed bug and
   the accepted behavior. Keep tests deterministic and scoped. Do not hide a
   production bug with a weaker assertion.
7. **`build_test_fixer`**: run the repository's user-authorized build and test
   commands. Fix only failures caused by the scoped change. Re-run affected
   verification until clean. Record commands, failures, and fixes.
8. **`root` as final verifier**: confirm requirements, test evidence,
   documentation, no accidental API/privacy regressions, and a clean scoped
   diff. Do not reimplement the feature.
9. **`git_expert`**: create small focused commits. Stage only scoped files,
   run a staged diff check, and never include unrelated dirty files.
10. **`root` report**: lead with outcome. List feature scope, commits, tests
    written, commands and results, failures fixed, deferred work, and
    unrelated changes intentionally left untouched.

## Parallelism and ownership

- Run `code_reviewer` and `test_automator` in parallel only after
  implementation is stable.
- Never run `cpp_pro`, `test_automator`, and `build_test_fixer` concurrently
  when any could edit the same workspace.
- Keep exploration, architecture, final verification, and reporting in `root`.
- Reuse `cpp_pro` for each production edit and `test_automator` for each test
  edit. Use a final verification pass before committing.

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
