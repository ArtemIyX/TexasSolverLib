---
name: git-commit-hygiene
description: Inspect Git repositories, find and review changes, and create clean, consistent commits. Use for any Git request, including status, diff review, staging, committing, commit-message writing, log/history, branches, merges, reverts, or repository cleanup; explicitly invoke with $git-commit-hygiene.
---

# Git Commit Hygiene

IT IS FORBIDDEN to run builds, tests, benchmarks, installs, or solver jobs unless the user explicitly asks. When asked to verify, use the repository's documented CMake commands and report exactly what ran.
Use this workflow to make Git actions safe, scoped, and easy to understand.

## Inspect first

1. Read repository guidance (`AGENTS.md`, `CONTRIBUTING.md`, commit history) when present.
2. Check the current branch, worktree status, and both staged and unstaged diffs.
3. Separate the user's requested changes from unrelated pre-existing changes. Never alter, stage, discard, or commit unrelated work.

## Commit workflow

Only create a commit when the user asks to commit or clearly authorizes it.

1. Stage only the files and hunks belonging to the requested change.
2. Review the staged diff and run relevant, proportionate validation when feasible.
3. Write the commit subject in this format:

   ```text
   [BranchName] (optional scope) [Target]: [Short description]
   ```

   Example:

   ```text
   pb-1 Multiway: Add documentation for the RAM pilot
   ```

   Rules:

   - Use the branch identifier as the first token. Strip a standard prefix such as `feature/`, `fix/`, or `codex/`; for `feature/pb-1`, use `pb-1`.
   - Add one short parenthesized scope only when it materially narrows the change, for example `(memory)`.
   - Use a clear, title-cased target followed by a colon, for example `Multiway:` or `Build:`.
   - Start the short description with an imperative verb and keep it specific.
   - Keep the subject ideally within 72 characters.
   - Add a short body only when it explains a meaningful reason, tradeoff, or compatibility concern.

4. Report the commit hash, message, affected files, and validation result.

## Safety

- Do not commit secrets, generated artifacts, lockfile churn, or unrelated formatting changes without explicit authorization.
- Do not rewrite published history, force-push, discard changes, or use destructive Git commands unless the user explicitly requests it.
- For a request that only asks to inspect, explain, or draft a message, make no repository changes.
