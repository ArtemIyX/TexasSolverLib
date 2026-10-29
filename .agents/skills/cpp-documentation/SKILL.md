---
name: cpp-documentation
description: Write, improve, or review TexasSolver C++17 API documentation and developer guides for new or existing code, using strict scoped coverage and evidence-based Doxygen contracts.
---

# TexasSolver C++ Documentation

Document the behavior a reader must rely on, not the implementation's spelling. Follow `AGENTS.md`, the user's requested scope and the project skills. Documentation work does not authorize changes to code behavior, CMake, CI, publishing, dependencies, or unrelated content.

## Required workflow

1. Identify the requested files, symbols and audience. For a new or changed API, include affected public declarations and adjacent documentation; for a legacy or rewrite request, use only the agreed scope. Distinguish installed headers from research, diagnostic, compatibility and implementation-only code by checking current CMake and callers; `include/` alone does not prove a supported public API.
2. Read [coverage and declaration rules](references/coverage.md). Inventory every in-scope public declaration and file, including overloads and enum values. Complete 100% of that inventory unless an explicit exception applies; distinguish an inherited/shared contract from an undocumented declaration. Never silently broaden the task to all headers.
3. Inspect the declaration and relevant implementation, callers, tests and existing docs needed to substantiate the contract. Code and tests show observable behavior, not necessarily intended guarantees. Do not infer safety, performance, determinism, numerical accuracy or production readiness from names, assertions or passing tests. If a required fact cannot be established, do not invent it: flag it for review and report the coverage gap.
4. Put canonical API comments beside declarations; use local conventions where compatible. Correct stale or contradictory text, preserve accurate rationale and links, and avoid repeating contracts. For Markdown guides or examples, consult [writing and examples](references/writing-examples.md).
5. Inspect the scoped changes for coverage, correctness, Doxygen association, duplicates and unsupported promises. Build or run tests, documentation generators, benchmarks, installs, or solver jobs only when explicitly requested and authorized by applicable project skills. Report checks actually run and unresolved gaps. Apply `update-project-log` only after the task is complete.

## Read as needed

- [Coverage and declaration rules](references/coverage.md): mandatory baseline, conditional contracts, exceptions, inheritance, constructors, destructors, macros and pragmas.
- [Writing and examples](references/writing-examples.md): templates, verified project examples, prose, guides and maintainability.
- [Generation and sources](references/automation.md): Doxygen behavior, future CI/CD triggers, versioning, provider manuals and researched examples. Read when generation, tooling or automation is requested.

Use `cpp-worker` for implementation changes, `cpp-code-review` for review requests, and `cmake-maintenance` before any applicable CMake change. This skill does not by itself request those changes or grant execution permission.
