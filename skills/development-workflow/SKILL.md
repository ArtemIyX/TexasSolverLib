---
name: development-workflow
description: Orchestrate scoped software changes with the project-local agent roster, parallel architecture/correctness/performance/unit-test reviews, tests, verification, concise reporting, and small focused commits. Use when implementing, fixing, refactoring, or reviewing a repository change that benefits from a repeatable multi-agent workflow.
---

# Development Workflow

Use this workflow for a bounded repository change. Read repository instructions
and inspect the worktree before assigning tasks. Preserve unrelated changes.

## Project-local roster

Use only the agent definitions in `,codex/agents/`. The TOML stem is the exact
agent identity. When using the collaboration task API, use the matching
underscore task name shown below. Reuse an idle named agent with a follow-up
task. Do not create suffixed variants.

| Agent identity | Collaboration task name | Role | May edit? |
| --- | --- | --- | --- |
| `root` | `root` | Explorer, final verifier, report | Only when needed |
| `architect-reviewer` | `architect_reviewer` | Architecture and scope review | No |
| `cpp-pro` | `cpp_pro` | Production implementation and review fixes | Yes |
| `code-reviewer` | `code_reviewer` | Correctness, API, safety, compatibility review | No |
| `performance-engineer` | `performance_engineer` | Performance, hot-path, memory, concurrency review | No |
| `test-automator` | `test_automator` | Unit-test review and regression-test implementation | Tests only |
| `build-test-fixer` | `build_test_fixer` | Authorized verification and scoped build/test fixes | Yes |
| `git-expert` | `git_expert` | Focused staging and commits | Git only |

Use short, bounded prompts. State the phase, permitted actions, affected scope,
and expected deliverable.

## Workflow

1. **`root` explores**: inspect instructions, current behavior, tests, relevant
   files, and `git status`. Report evidence and risks.
2. **`architect-reviewer` scopes**: define the smallest useful design, unchanged
   contracts, acceptance criteria, and validation. Skip only for trivial work.
3. **`cpp-pro` implements**: make the approved production change. Do not run
   builds or tests unless authorized. Do not commit.
4. **Reviews run in parallel** after editing is stable:
   - `code-reviewer`: correctness and compatibility.
   - `performance-engineer`: hot paths, allocation, memory, and concurrency.
   - `test-automator`: unit-test gaps and assertion quality, read-only in this
     review pass.
5. **`cpp-pro` fixes**: verify and resolve consolidated actionable findings.
   Re-run only materially affected reviews if needed.
6. **`test-automator` writes tests**: add deterministic regression tests for
   each fixed issue and each accepted behavior.
7. **`build-test-fixer` verifies**: run only user-authorized build/test commands,
   fix scoped failures, and re-run until clean.
8. **`root` verifies and reports**: check scope, test evidence, documentation,
   contract preservation, and unrelated changes. Report outcome, tests, fixes,
   limitations, and commits.
9. **`git-expert` commits**: stage only scoped files, inspect the staged diff,
   and create small focused commits.

## Ownership and commits

- Never run two editing agents concurrently in the same workspace.
- Run the three read-only reviewers simultaneously only after `cpp-pro` stops.
- Keep implementation and regression tests in one commit when they form one
  behavior change. Make a separate commit for independent documentation.
- Never commit generated output, local configuration, or unrelated user work.
- Do not claim success until required verification passes.
