---
name: update-project-log
description: Record each completed TexasSolver task in docs/PLURIBUS_LOG.md with its scope, changed files, actual verification, and limitations. Use after implementation, bug-fix, documentation, or roadmap work is genuinely complete, and when the user explicitly asks to update the project log.
---

# Update Project Log

Update `docs/PLURIBUS_LOG.md` only after the current task is complete. Keep the log factual, concise, newest-first, and consistent with the repository roadmap.

Do not log planning, partial work, failed work, or unverified claims. Do not run builds, tests, benchmarks, installs, or solver jobs solely to populate the log. Follow the repository's `AGENTS.md` instructions.

## Workflow

1. Confirm completion from the task result and inspect the relevant working-tree diff. Include only files changed for this task. Preserve unrelated user changes.
2. Read `docs/PLURIBUS_LOG.md`. If the task implements a roadmap item, read `docs/implementation_roadmap.md` and use the exact roadmap identifier and title when one exists.
3. Add one entry immediately after the log introduction so the newest completed task remains first. Use this shape:

```markdown
## P#.#[ - Roadmap task title]

**Status:** Complete
**Completed:** YYYY-MM-DD
**Implementation commit:** `hash message`  <!-- include only when known -->

- Completed scope and behavior.
- Important compatibility or architectural notes.

### Files

- `path/to/file`

### Validation

- List only checks actually run and their results.
- If none were run, state that explicitly and give the reason.

### Limitations

- State known limitations or deferred work. Write `None identified.` when applicable.
```

4. Use the current date in ISO format. Never invent commit hashes, test results, files, roadmap status, or completion claims.
5. Review the resulting log diff for duplicate entries, accidental edits to prior entries, incorrect Markdown, and private data. Do not stage or commit unless separately requested.
