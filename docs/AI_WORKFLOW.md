# AI and repository workflow

This repository contains instructions and skills intended to help coding agents work consistently. They are project guidance, not proof that a change is correct or that a particular change was authored by AI.

## Starting points

- [AGENTS.md](../AGENTS.md) describes repository workflow, C++ style, hot-path constraints, and routes to project skills.
- [Token-efficient workflow](../CODEX_TOKEN_EFFICIENT_WORKFLOW.md) describes targeted inspection, concise verification, and reporting practices.
- [Project skills](../.agents/skills/) contain task-specific instructions. For example, `texas-guide` routes repository work; `readme-authoring`, `cpp-worker`, `cpp-unit-tests`, `cpp-code-review`, and `cmake-maintenance` cover specialized work. Read the current `SKILL.md` for the task rather than assuming a skill's description is a validation result.
- [Project guide](PROJECT_GUIDE.md) maps the code, tools, and contracts. It is a dated inspection and should be checked against current files.

## Progress and evidence

The [Pluribus progress log](PLURIBUS_LOG.md) records completed work, files, checks, and limitations. Entries are historical reports for their stated scope; they do not establish that the current checkout was built or tested, or that a solver has proven playing strength.

The [technical report](pluribus_solver_technical_report.md) includes research discussion, proposed designs, and guidance for coding agents. Its evidence labels distinguish external research and recommendations from current implementation. The [application and script I/O reference](application_and_script_io.md) documents operational tools and their side effects.

Repository instructions specify when project skills apply and when execution needs explicit authorization. Review the actual change and the checks performed for each task; instructions and logs do not replace code review, reproducible experiments, or independent validation.
