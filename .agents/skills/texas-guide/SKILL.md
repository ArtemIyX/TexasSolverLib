---
name: texas-guide
description: Orient and route work in the TexasSolver repository, including its solver architecture, CMake boundaries, project tools, and specialized skills. Use for TexasSolver repository tasks; verify current files before relying on project-specific details.
---

# TexasSolver Guide

Use this skill at the start of TexasSolver repository work. Read `AGENTS.md` and `CODEX_TOKEN_EFFICIENT_WORKFLOW.md`, honor the user's scope and execution permissions, and use targeted `rg` searches. This guide routes work; it does not replace applicable specialized skills or grant permission to run commands or change unrelated files.

## Route the task

1. Locate the public type or behavior, its implementation, callers, and relevant tests. Start with [architecture and navigation](references/architecture.md) when the owning subsystem is unclear.
2. Load the applicable project skills: `project-navigation` for tracing; [readme-authoring](../readme-authoring/SKILL.md) for creating, updating, restructuring, or reviewing READMEs; `cpp-worker` for C++ changes; `cpp-unit-tests` for behavioral tests; `cpp-code-review` for C++ review; `cmake-maintenance` before CMake, package, toolchain, or preset changes; `git-commit-hygiene` for Git work; and `update-project-log` after completed implementation or documentation work. Read their current instructions instead of assuming this list supersedes them.
3. Identify API, lifetime, numerical, deterministic, memory, platform, and packaging contracts affected by the task. Use [CMake and tests](references/cmake.md) for build changes and [tools and workflows](references/tools.md) for commands and scripts.
4. Make a focused change or review. For examples of routing concrete tasks, see [task examples](references/examples.md). Verify paths, options, and script behavior in the checkout before use.
5. Report actual checks and limitations. The relevant project skills prohibit running builds, tests, benchmarks, installs, and solver jobs without explicit user authorization; `cmake-maintenance` also requires authorization for configuration. This guide does not change that boundary.

## Non-obvious project constraints

- C++17; keep exact `HUNLFlatDCFR` behavior unless expressly changed. Large scalable heads-up work normally belongs in sampled/lazy modules.
- Keep traversal, terminal, row-update, and merge hot loops free of allocations, logging, strings, shared ownership, virtual calls, and per-action or per-bucket hash lookups. Prefer flat buffers, compact IDs, worker-local scratch, and action-major rows.
- Use RAII for ownership; pass non-owning pointer-and-size or existing C++17-compatible views to kernels. Preserve scalar references for SIMD and deterministic merge/seed contracts.
- Production sources and installed headers are explicitly classified in root CMake. Test source discovery is separate. Public, research, compatibility, diagnostic, and application boundaries matter.
- Current README describes implementation as complete but strategy quality as unproven. Do not infer production performance or playing strength from passing software tests or historical benchmarks.
