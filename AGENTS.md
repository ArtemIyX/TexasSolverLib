# AGENTS.md

Guidelines for LLM coding agents working in this repository.
You are an assistant optimized for minimal token usage and concise communication.

Use ``scripts/codex_powershell.ps1`` to execute powershell commands (fixed PATH/path mismatch).
Example:
```bash
powershell -ExecutionPolicy Bypass -File scripts\\codex_powershell.ps1 cmake --build build --config Debug
```

### General rules

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

## Testing And Verification

DO NOT execute run, build or test commands unless the user explicitly asks.

When asked to test, typical commands are:

```bash
cmake -S . -B build -DTEXASSOLVER_BUILD_TESTS=ON
cmake --build build --config Debug -- /nologo /v:q "/clp:ErrorsOnly;NoSummary"
python .agents\\skills\\cpp-build-test-fixer\\scripts\\ctest_compact.py
```

## Scripts

Use python scripts when u need something from OS before using own commands.

```text
repo_summary.py
cmake_summary.py
cpp_file_summary.py
compact_build.py
scan_build_errors.py
compact_ctest.py
scan_ctest_errors.py
list_recent_changes.py
extract_symbols.py
extract_includes.py
find_large_files.py
```

## Hard Rules

- Do not run build, test, benchmark, install, or long-running solver commands unless the user explicitly asks.
- Do not add poker client automation, screen scraping, clicking, stealth, evasion, account/session code.
- Do not revert user changes or unrelated dirty files.
- Keep exact `HUNLFlatDCFR` behavior intact unless the task explicitly targets it.
- For large HUNL work, prefer a new sampled/lazy solver module over mutating the exact full-tree solver.


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
- Keep comments short and useful.
- Prefer clear structured APIs over ad hoc strings.
- Use integer ids and explicit metadata for states, rows, actions, buckets, and trajectories.
- Keep scalar validation paths even when adding SIMD.
- Keep exact-mode behavior deterministic.
