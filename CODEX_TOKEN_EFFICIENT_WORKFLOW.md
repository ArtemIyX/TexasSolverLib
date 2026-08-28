# Codex Token-Efficient Workflow Instructions

## Purpose

Work efficiently and minimize unnecessary token usage while preserving correctness.

The main principle is:

> Do not read, print, or reason over large raw inputs when a smaller targeted view is enough to complete the task.

Prefer targeted inspection, compact summaries, diffs, helper scripts, and incremental discovery over broad repository scans or full-file dumps.

---

## 1. Read Only What Is Needed

Do not scan the whole repository unless the task explicitly requires it.

Before opening files:

1. Identify the likely subsystem, module, class, function, test, or configuration involved.
2. Search for relevant symbols, filenames, errors, references, or strings.
3. Open only the smallest useful file range.
4. Expand the search only if the current evidence is insufficient.

Prefer:

```text
Search -> inspect relevant lines -> inspect surrounding function/class -> modify
```

Avoid:

```text
Read repository -> read many full files -> summarize everything -> start task
```

Do not repeatedly reread files already understood unless:

- they changed;
- a new issue specifically requires them;
- previous context is insufficient.

---

## 2. Never Dump Large Files Without a Reason

Do not print entire source files, logs, JSON documents, CSV files, generated code, build output, or large configuration files unless explicitly necessary.

Prefer:

- relevant functions;
- relevant classes;
- relevant line ranges;
- matching search results;
- diffs;
- file metadata;
- summaries.

When inspecting a large file, first locate the relevant symbol or text.

Examples:

```bash
rg -n "FunctionName|ClassName|ErrorText" .
```

```bash
grep -n "pattern" file.cpp | head -n 50
```

Then inspect only the relevant range.

---

## 3. Limit Command Output Aggressively

Assume command output should be bounded unless complete output is required.

Use limits such as:

```bash
head -n 100
tail -n 100
```

```bash
rg -n "pattern" . | head -n 100
```

```bash
find . -type f | head -n 200
```

```bash
git status --short
```

```bash
git diff --stat
```

```bash
git diff -- path/to/relevant/file
```

For verbose build or test commands, prefer quiet modes or filtered output.

If output may be large:

1. redirect it to a file;
2. inspect only errors, warnings, failures, summaries, or selected ranges.

Example:

```bash
command > output.log 2>&1
```

Then:

```bash
rg -n "error|failed|failure|warning" output.log | head -n 100
```

Do not automatically print the complete log afterward.

---

## 4. Prefer Search Before Reading

Use repository search tools before opening files.

Prefer tools such as:

```bash
rg
git grep
find
```

Examples:

```bash
rg -n "MyClass"
```

```bash
rg -n "SomeFunction\("
```

```bash
rg -n "exact error message"
```

```bash
git grep "ConfigVariable"
```

The goal is to identify the smallest relevant set of files before reading their contents.

---

## 5. Exclude Irrelevant Directories

Do not inspect generated, vendor, cache, dependency, or build directories unless the task explicitly concerns them.

Common exclusions include:

```text
.git/
.vscode/
.idea/
.vs/
node_modules/
.venv/
venv/
dist/
build/
out/
bin/
obj/
Intermediate/
Binaries/
DerivedDataCache/
Saved/
coverage/
logs/archive/
vendor/
third_party/
generated/
```

Respect project-specific ignore files and repository structure.

When searching, exclude irrelevant directories when possible.

Example:

```bash
rg "pattern" . \
  -g '!build/**' \
  -g '!node_modules/**' \
  -g '!Intermediate/**' \
  -g '!Binaries/**'
```

---

## 6. Use Helper Scripts for Large or Repeated Data Processing

If large raw data must be inspected repeatedly, create or use a small helper script that produces a compact working view.

Suitable script/ helpers include:

```text
full_build.py

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

A helper should output only what is useful for the current task.

Examples:

- top N errors;
- unique errors and occurrence counts;
- selected JSON fields;
- CSV row count plus a small sample;
- changed files only;
- source symbols without source bodies;
- test failures without successful test output;
- timestamps around the failure;
- deduplicated stack traces;
- dependency relationships.

Prefer deterministic, reusable scripts over repeatedly asking the model to manually inspect large inputs.

---

## 7. Compact Structured Data Before Reading It

For JSON, CSV, XML, logs, database dumps, and similar data, avoid reading the full raw document when possible.

Prefer:

### JSON

Extract:

- keys;
- relevant nested objects;
- counts;
- selected records;
- fields related to the issue.

Example:

```bash
jq '{name, version, dependencies}' package.json
```

### CSV

Inspect:

- header;
- row count;
- selected columns;
- first/last few records;
- rows matching the issue.

### Logs

Inspect:

- errors;
- warnings;
- failures;
- timestamps near the failure;
- unique messages;
- first and last relevant occurrences.

### Repository Trees

Prefer compact summaries such as:

```text
src/
  networking/
  gameplay/
  ui/
tests/
tools/
CMakeLists.txt
README.md
```

Do not emit thousands of paths unless explicitly requested.

---

## 8. Keep a Compact Handoff File

When the task spans many steps or sessions, maintain a small context file such as:

```text
CODEX_HANDOFF.md
```

Keep it concise.

Recommended structure:

```md
# Current Goal

Short description of what is being implemented or fixed.

# Relevant Files

- path/to/file.cpp — reason
- path/to/file.h — reason
- tests/test_file.cpp — reason

# Known Facts

- ...
- ...

# Decisions

- ...
- ...

# Commands

- build command
- test command

# Current Failure

Short exact failure summary.

# Completed

- ...
- ...

# Next Steps

1. ...
2. ...

# Avoid Rechecking

- Things already verified that should not be reread unless relevant.
```

Do not turn the handoff into a full development diary.

Keep only information useful for continuing the current task.

Remove:

- dead ends;
- repeated explanations;
- obsolete hypotheses;
- verbose command output;
- unnecessary implementation history.

---

## 9. Compact Context Periodically

For long tasks, periodically reduce the working context.

Create or update a concise handoff containing:

- current objective;
- current state;
- important findings;
- files changed;
- unresolved issue;
- next actions.

Do not repeat information that is already stable and recorded.

Prefer a short factual state snapshot over a narrative summary.

---

## 10. Prefer Diffs Over Full Files

When reviewing or explaining changes, use:

```bash
git diff -- path/to/file
```

or:

```bash
git diff --stat
```

Prefer showing only the changed sections.

Do not print an entire source file merely because a few lines changed.

After editing, inspect the diff rather than rereading the whole file.

---

## 11. Prefer Targeted Git Inspection

Use compact Git commands.

Prefer:

```bash
git status --short
```

```bash
git diff --stat
```

```bash
git diff --name-only
```

```bash
git log -n 10 --oneline
```

```bash
git show --stat --oneline <commit>
```

Only inspect full commit diffs when they are relevant.

Do not read large historical commit ranges unless required.

---

## 12. Be Selective With Tests

Run the smallest useful test scope first.

Prefer:

```text
single failing test
-> related test suite
-> affected module tests
-> full test suite
```

Do not repeatedly run every test when only one subsystem is being changed unless regression coverage requires it.

For failures, prefer output containing:

- test name;
- failure reason;
- source location;
- assertion;
- short stack trace if useful.

Suppress successful test noise when possible.

---

## 13. Be Selective With Builds

Use incremental builds and narrow targets when supported.

Prefer:

- affected target;
- affected module;
- incremental build;
- quiet output;
- errors-only output.

Avoid full clean rebuilds unless necessary.

Do not perform destructive cleanup such as deleting the entire build directory unless there is evidence that stale artifacts are causing the problem.

---

## 14. Investigate Incrementally

Use this investigation loop:

1. Reproduce or understand the issue.
2. Identify the most likely subsystem.
3. Search for relevant symbols or error text.
4. Read the smallest relevant source range.
5. Form a hypothesis.
6. Inspect only the next evidence needed to validate it.
7. Implement the smallest correct change.
8. Review the diff.
9. Run the smallest meaningful verification.
10. Expand only if necessary.

Do not perform broad exploratory reading "just in case."

---

## 15. Do Not Repeatedly Rediscover Repository Structure

If repository structure has already been summarized, reuse that summary.

Create a compact project map when useful:

```md
# Repository Map

## Entry Points

- ...

## Core Modules

- ...

## Configuration

- ...

## Tests

- ...

## Build Commands

- ...

## Important Generated/Ignored Directories

- ...
```

Update it only when the structure materially changes.

---

## 16. Keep Responses Concise

Unless explicitly asked for a detailed explanation:

- do not restate the entire task;
- do not repeat the plan after every step;
- do not explain obvious commands;
- do not paste full files;
- do not include large command outputs;
- do not narrate every inspection;
- do not repeat already established facts.

Prefer:

```text
Changed X because Y.
Validation: Z passes.
Remaining issue: none.
```

For code modifications, prioritize:

1. what changed;
2. why;
3. verification result.

---

## 17. Do Not Hide Important Information Just to Save Tokens

Token efficiency must not reduce correctness.

Always surface:

- build failures;
- test failures;
- safety issues;
- API incompatibilities;
- unresolved assumptions;
- behavior changes;
- potential regressions;
- missing verification.

Compact irrelevant information, not important information.

---

## 18. Read More When Correctness Requires It

The goal is not to minimize reading at all costs.

Read broader context when required for:

- architecture changes;
- ownership/lifetime behavior;
- concurrency;
- networking;
- serialization formats;
- public APIs;
- security-sensitive code;
- cross-module refactors;
- complex inheritance;
- build-system changes;
- subtle tests or invariants.

However, expand context deliberately rather than scanning indiscriminately.

---

## 19. Recommended Default Command Behavior

When choosing shell commands, default to compact forms.

### File discovery

```bash
find . -type f | head -n 200
```

or preferably targeted search:

```bash
rg --files | rg "keyword"
```

### Text search

```bash
rg -n "pattern" relevant/path | head -n 100
```

### Git state

```bash
git status --short
git diff --stat
```

### Diff inspection

```bash
git diff -- relevant/file
```

### Logs

```bash
rg -n "error|failed|failure" log.txt | head -n 100
```

### Large outputs

```bash
command > /tmp/codex-output.log 2>&1
```

Then inspect selected parts rather than printing everything.

---

## 20. Create Compact Working Views

When raw information is large, convert it into a task-specific working view before reasoning over it.

Examples:

```text
Raw compiler output
-> unique errors + file:line + first occurrence

Large JSON
-> relevant keys + selected objects

Huge CSV
-> schema + counts + matching rows + sample

Repository
-> entry points + relevant modules + test locations

Git history
-> recent commits touching affected files

Test suite
-> failed tests only
```

The compact view should preserve enough information to make the next decision safely.

---

## 21. Preferred Response Style During Coding Tasks

Use concise progress summaries.

Good:

```text
Found the failure in `Foo.cpp:142`: the cache is invalidated after the lookup.

I changed the invalidation order and added a regression test.

Validation:
- targeted test: PASS
- FooTests: PASS
```

Avoid:

```text
I started by exploring the repository. Then I noticed several files...
```

unless that detail is necessary.

---

## 22. Before Opening More Files, Ask Internally

Before reading additional context, determine:

```text
What specific uncertainty will this file resolve?
```

If there is no clear answer, do not open it yet.

---

## 23. Before Running a Verbose Command, Ask Internally

Determine:

```text
What exact information do I need from this command?
```

Then filter the command output to return that information.

Do not generate thousands of lines and filter them mentally afterward.

---

## 24. Avoid Repeating Expensive Operations

Do not repeatedly:

- list the entire repository;
- reread unchanged large files;
- rerun full test suites;
- regenerate dependency listings;
- dump complete logs;
- re-explain known architecture;
- run broad static analysis if only one issue matters.

Cache conclusions in the handoff/context file when useful.

---

## 25. Editing Strategy

When modifying code:

1. locate exact target;
2. read the surrounding function/class;
3. inspect dependencies only if needed;
4. make focused changes;
5. inspect diff;
6. build/test narrowly;
7. expand verification if necessary.

Prefer focused patches over unrelated cleanup.

Do not refactor unrelated code unless required by the task.

---

## 26. Failure Investigation Strategy

When a build or test fails:

1. capture the command output to a file if verbose;
2. extract failure/error lines;
3. inspect the first meaningful root-cause error;
4. ignore secondary cascade errors initially;
5. locate the relevant source;
6. fix;
7. rerun the smallest affected test/build;
8. only inspect more output if the problem remains.

Do not ingest the entire log by default.

---

## 27. Repository Analysis Requests

If asked to "analyze the repository," do not automatically read every file.

Produce a compact map using:

- top-level directories;
- build/configuration files;
- entry points;
- primary modules;
- test locations;
- important dependencies;
- relevant documentation.

Exclude generated/vendor directories.

Only inspect implementation files necessary to understand the architecture.

---

## 28. Source File Analysis Requests

If asked about a behavior or bug, locate the responsible symbol first.

Prefer:

```text
Find the relevant function/class.
Inspect its implementation.
Inspect direct callers/callees only when needed.
```

Do not read unrelated portions of the same large file.

---

## 29. Documentation Lookup

When project documentation is available:

1. search headings or keywords;
2. read the relevant section;
3. avoid loading every document.

If documentation already answers the question, do not inspect implementation code unless verification is needed.

---

## 30. Token-Efficiency Priority Order

When equivalent approaches exist, prefer:

1. targeted search;
2. small relevant snippet;
3. diff;
4. compact generated summary;
5. bounded command output;
6. full file;
7. broad repository scan.

Use broader inputs only when narrower inputs are insufficient.
