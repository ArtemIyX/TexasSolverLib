# AGENTS.md

Guidelines for LLM coding agents working in this repository.
You are an assistant optimized for minimal token usage and concise communication.

## Agent workflow
- Handle simple and tightly coupled tasks directly; do not spawn subagents by default.
- For complex tasks, first identify dependencies, affected files, and acceptance tests.
- Delegate bounded independent work to the available roles when useful.
- Specify task, allowed scope, required context, expected artifacts and tests.
- Avoid parallel edits to the same files; coordinate sequentially or use worktrees.
- Have a separate reviewer validate risky changes when warranted.
- Merge results only after tests and acceptance criteria pass.
- Keep agent reports concise: changed paths, relevant evidence, failures, blockers.
- Write a short durable handoff for unfinished multi-session work.

### Available agents

Project agent definitions live in `.codex/agents/` and are registered in
`.codex/config.toml`. Select roles based on the task, not as a mandatory pipeline:

| Agent | Use |
| --- | --- |
| `coordinator` | Scope substantial work, coordinate dependencies and ownership, and check acceptance. |
| `explorer` | Read-only mapping of files, symbols, call paths, tests, and dependencies. |
| `investigator` | Read-only root-cause investigation and architecture tradeoffs. |
| `implementer` | Implement a bounded change and relevant tests. |
| `build_repair` | Run requested builds and tests, repair failures, and rerun the requested scope. |
| `reviewer` | Independently review changes for actionable defects without edits. |
| `archivist` | Update assigned documentation, plans, and handoffs; create scoped commits when authorized. |

The coordinator may create a narrowly scoped custom agent when existing roles do
not fit. Default custom work to `gpt-6-luna` with `medium` reasoning when
available; use another model or effort when the task warrants it. Provide scope,
file ownership, constraints, authorized checks, and expected output. Avoid adding
persistent agent files for one-off tasks unless reuse justifies them. Respect
runtime delegation and permission rules. Subagents inherit the parent sandbox;
read-only agent instructions do not enforce a separate sandbox. Run builds, tests,
benchmarks, installs, or solver jobs only when explicitly authorized by the user.

### Agent introduction

At the start of each new task, each agent should introduce itself in its first
message: `Hey. I'm [name]. Using [model] [effort]`. Use its assigned name, or
`Codex` for the main agent. Report the actual model and reasoning effort only
when known; use `unknown` for either value that is unavailable. Subagents should
include this introduction in their first message to the coordinator.

### Skill-set

Use project skills to complete the task at hand

Use [texas-guide](.agents/skills/texas-guide/SKILL.md) at the start of
TexasSolver repository work, alongside the specialized skills below.

|Skill|When to use|
| -------- | ------- |
|texas-guide| Navigate TexasSolver architecture, tools, workflows, and project guidance|
|[readme-authoring](.agents/skills/readme-authoring/SKILL.md)| Create, update, restructure, or review project, module, example, and research READMEs|
|git-commit-hygiene| Commit small changes|
|cpp-worker| Implement features|
|cpp-unit-tests| Make unit tests for new features|
|cpp-code-review| Review fresh-made code|
|update-project-log| Update LOG file after finishing any task|
|cmake-maintenance| Any CMakeLists.txt change, CMake module/package/toolchain change, or CMake preset change|

Before editing any repository `CMakeLists.txt`, read and apply
[cmake-maintenance](.agents/skills/cmake-maintenance/SKILL.md), even when the
CMake edit is a small part of another task. Also apply it to related `.cmake`,
`.cmake.in`, `CMakePresets.json`, and `CMakeUserPresets.json` changes.
This is required without an explicit skill invocation. For vendored CMake files,
apply the skill's dependency guidance before deciding whether an edit is needed.


## General rules

Check [CODEX_TOKEN_EFFICIENT_WORKFLOW.md](CODEX_TOKEN_EFFICIENT_WORKFLOW.md) for LLM-Agent Token-Efficient Workflow Instructions.

1. Write the shortest correct answer possible.
2. Use plain sentences. Avoid decorative language.
3. Do not use emojis.
4. Do not use em-dashes.
5. Do not include filler phrases such as:
    - "Certainly"
    - "Here is"
    - "Let me explain"
    - "In conclusion"
6. Be concise.
7. No long explanations.
8. Show only the patch and the reason.
9. Do not restate the whole plan unless it changed.
10. Avoid repetition and paraphrasing.
11. Do not restate the user question.
12. Prefer short sentences instead of long paragraphs.
13. Prefer bullet points only when they reduce length.
14. Do not add explanations unless explicitly requested.
15. If the question requires explanation, give the minimal explanation needed for correctness.
16. Do not add warnings, disclaimers, or background information unless required for correctness.
17. Do not include motivational text, politeness phrases, or conversational padding.


## Hot-Path Performance Rules

In traversal, row update, terminal inner loops, and merge inner loops and hot-pathes:

- no `std::string`;
- no formatting/logging;
- no heap allocation;
- no `new` / `delete`;
- no `std::shared_ptr`;
- no `std::function`;
- no virtual dispatch;
- no exceptions as normal control flow;
- no hash-map lookup inside per-action or per-bucket loops;
- no unreserved `std::vector::push_back`.

Preferred hot-path style:

- flat arrays;
- compact integer ids;
- raw pointer or `std::span` non-owning views;
- preallocated worker scratch;
- stack/fixed-size arrays for small action menus;
- action-major contiguous rows: `row[action][bucket]`;
- scalar reference kernels first, optional SIMD kernels later.

Ownership rule:

- own memory with RAII containers at subsystem boundaries;
- pass raw pointer/span views into hot kernels;
- raw pointers must not own memory;
- avoid shared ownership in solver internals.

## Style

- Use C++17.
- Declare project structs under `#pragma pack(push, 8)` and restore the previous packing with `#pragma pack(pop)` after the declarations. Keep includes outside the packing scope.
- Preserve explicit cache-line alignment where required. Eight is a member packing limit, not a cap on an explicitly aligned struct.
- Treat packing changes as ABI changes: rebuild consumers and verify member alignment, serialized layouts, and compatibility on supported platforms. Packing does not define a portable wire format.
- Keep comments short and useful.
- Prefer clear structured APIs over ad hoc strings.
- Use integer ids and explicit metadata for states, rows, actions, buckets, and trajectories.
- Keep scalar validation paths even when adding SIMD.
- Keep exact-mode behavior deterministic.
