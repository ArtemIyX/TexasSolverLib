# TexasLogging Implementation Plan

## Goal

Add an application-level `TexasLogging` module with:

- simultaneous console and file logging;
- fixed logging categories and severity levels;
- strict TOML configuration;
- global and per-category filtering;
- automatic or forced console colors;
- live status and progress rendering;
- clean interaction between progress and ordinary logs;
- runtime and compile-time disable paths;
- no logging work inside solver hot paths;
- preservation of existing machine-readable output and workflow artifacts.

## Architecture decision

Build logging as an internal application module, separate from the installed
`TexasSolver::texas` solver library.

```text
TexasSolver::texas       Solver and game library. No logging dependency.
TexasSolver::logging     Application logging, TOML, terminal, and progress.
Workflow executables     Link both targets.
```

This boundary prevents logging policy and third-party dependencies from
entering traversal, terminal evaluation, row update, and merge kernels. Solver
code continues to expose typed status and progress callbacks. Applications
decide how those events are displayed and recorded.

Use these external libraries:

- `spdlog` for formatting, filtering, category loggers, and file sinks;
- `toml++` for strict logging configuration parsing.

Do not add a progress-bar dependency initially. A small internal renderer is
required even with an external progress library because logging and live output
must share one terminal lock and redraw policy.

Use synchronous logging first. Asynchronous logging can be evaluated later if
measurement shows that cold-boundary logging affects throughput.

## Proposed files

```text
include/logging/config.hpp
include/logging/log.hpp
include/logging/progress.hpp

src/logging/config.cpp
src/logging/log.cpp
src/logging/progress.cpp
src/logging/terminal.cpp
src/logging/terminal.hpp

tests/test_logging_config.cpp
tests/test_logging.cpp
tests/test_logging_progress.cpp
tests/test_logging_disabled.cpp

config/logging.example.toml
docs/texas_logging.md
```

Public headers in `include/logging/` are internal application headers. They are
not installed or added to `include/core/lib.hpp` in the first release.

## Public API

### Types

```cpp
namespace texas::logging
{
	enum class Level : std::uint8_t
	{
		Trace,
		Debug,
		Info,
		Warning,
		Error,
		Critical,
		Off
	};

	enum class Category : std::uint8_t
	{
		Core,
		Config,
		Workflow,
		Solver,
		Storage,
		Checkpoint,
		Performance,
		Validation,
		Count
	};

	enum class ColorMode : std::uint8_t
	{
		Automatic,
		Always,
		Never
	};

	enum class ProgressMode : std::uint8_t
	{
		Automatic,
		Live,
		Lines,
		Off
	};

	struct Config;
	struct InitializeResult;

	InitializeResult initialize(const Config& config) noexcept;
	void flush() noexcept;
	void shutdown() noexcept;
	bool should_log(Category category, Level level) noexcept;
}
```

Use a fixed category enum and array lookup. Do not use a string hash lookup on
each log call. TOML category names are converted once during initialization.

### Logging macros

```cpp
TEXAS_LOG_TRACE(Performance, "Batch {} merge took {} us", batch, elapsed_us);
TEXAS_LOG_INFO(Workflow, "Training started with {} workers", workers);
TEXAS_LOG_WARNING(Storage, "Checkpoint {} is incomplete", path);
TEXAS_LOG_ERROR(Config, "Invalid option {}", option);
```

The macros must:

- include source file and line metadata;
- check `should_log()` before evaluating formatting arguments;
- compile to `((void)0)` when logging is disabled at build time;
- avoid static initialization order dependencies;
- never be used in solver hot loops.

### Progress API

```cpp
auto progress = texas::logging::Progress::start(
	"Training", requested_batches);

progress.update(completed_batches)
	.field("trajectories", trajectories)
	.field("workers", workers)
	.rate(trajectories_per_second);

progress.finish("Training complete");
```

`Progress` is movable, non-copyable, and owns no solver state. The first
implementation supports one active live line. It uses ASCII by default and
does not hide the cursor, avoiding crash-time cursor restoration problems.

## TOML configuration

Track `config/logging.example.toml`:

```toml
schema_version = 1

[logging]
enabled = true
default_level = "info"
flush_level = "warning"

[logging.console]
enabled = true
stream = "stdout"
level = "info"
color = "auto"
pattern = "[%H:%M:%S.%e] [%^%l%$] [%n] %v"

[logging.file]
enabled = false
level = "debug"
path = "logs/texassolver.log"
required = false
append = true
rotate_size_mb = 20
rotate_files = 5
pattern = "[%Y-%m-%d %H:%M:%S.%e] [%l] [%n] [%t] %v"

[logging.categories]
Core = "info"
Config = "info"
Workflow = "info"
Solver = "warning"
Storage = "debug"
Checkpoint = "info"
Performance = "off"
Validation = "warning"

[progress]
enabled = true
mode = "auto"
refresh_ms = 100
line_interval = 1
show_elapsed = true
show_eta = true
show_rate = true
```

Configuration rules:

- Reject unsupported `schema_version` values.
- Reject unknown keys, levels, categories, color modes, and progress modes.
- Reject duplicate keys using TOML parser diagnostics.
- Validate sizes and intervals before narrowing integer conversions.
- Resolve relative log paths against the TOML file directory.
- Do not log while parsing logging configuration.
- Return structured bootstrap errors containing the file, key, and reason.
- An explicitly requested missing or invalid config is fatal.
- No config path means compiled defaults. Do not silently load a file from the
  current working directory.
- A file-sink failure falls back to console unless `required = true`.

Precedence, from lowest to highest:

```text
compiled defaults
< TOML file
< environment variables
< command-line options
< existing workflow compatibility overrides
```

Initial environment variables:

```text
TEXASSOLVER_LOG_CONFIG
TEXASSOLVER_LOG_LEVEL
TEXASSOLVER_LOG_FILE
TEXASSOLVER_LOG_COLOR
```

Initial command-line options:

```text
--log-config <path>
--log-level <level>
--log-file <path>
--log-color auto|always|never
--no-console-log
--no-file-log
--no-log
--quiet
```

Keep the existing `--progress`, `--progress-every`, `--progress-memory`, and
`--progress-speed` options. `--quiet` disables interactive console logs and
progress but may preserve an explicitly enabled file sink. `--no-log` disables
all log sinks. Progress remains independently configurable.

## Console and file behavior

Create one `spdlog` logger per category. All category loggers share:

- one progress-aware console sink;
- one rotating general file sink when configured;
- an optional plain training-run sink for the existing workflow audit log.

A record is accepted only when it passes both filters:

```text
record level >= category level
and
record level >= sink level
```

The console and file sinks may therefore use different levels. For example,
the console can show warnings while the file records debug messages.

The general file sink uses timestamps and thread identifiers. Console color
codes are never written to files. The existing training log remains append-only
and uses pattern `%v` during migration so its plain-message artifact remains
compatible.

Install an `spdlog` error handler that writes one bounded fallback message
directly to `stderr`. It must not call the logger recursively. Disk-full and
sink-write failures disable the failing optional sink and allow solving to
continue. A required sink failure ends initialization with a clear error.

Call `flush()`:

- for records at or above the configured flush level;
- before checkpoint handoff where audit ordering matters;
- before normal process exit;
- in caught top-level failure paths.

Use explicit `initialize()` and `shutdown()` through an application-owned RAII
session. Do not depend on global destructor order.

## Terminal and live-output behavior

`TerminalConsoleSink` and `ProgressManager` share one mutex and one terminal
state object.

For an ordinary console log:

1. Lock the terminal state.
2. Clear the active live line.
3. Write the complete formatted log and newline.
4. Redraw the latest live line.
5. Unlock.

For a progress update:

1. Store the latest counters and fields.
2. Apply `refresh_ms` throttling.
3. Lock the terminal state.
4. Replace the line with `\r`.
5. Pad the output if the new line is shorter than the old line.
6. Unlock.

The final progress update bypasses throttling and writes exactly one newline.

Terminal detection:

- Windows: use `GetConsoleMode` and enable virtual-terminal processing when
  available.
- POSIX: use `isatty`.
- `color = "auto"` enables colors only for an interactive terminal.
- Honor `NO_COLOR` in automatic mode.
- Redirected output and CI use newline-based progress records.
- ANSI escape sequences must not appear in redirected output or files.

The first release keeps console output on `stdout` for the multiway workflow
because `docs/application_and_script_io.md` currently documents progress and
normal operational messages there. Moving diagnostics to `stderr` is a
separate interface change. JSON, reports, benchmark tables, and other
machine-readable outputs must remain untouched.

## Runtime and compile-time disabling

Support three independent controls:

1. `logging.enabled = false` or `--no-log` disables all log sinks.
2. A category level of `off` disables one category.
3. `TEXASSOLVER_ENABLE_LOGGING=OFF` compiles logging macros to no-ops.

Disabled macros must not evaluate arguments. Add a dedicated translation-unit
test using a side-effect counter to prove this contract.

Progress is separate from logging. `progress.enabled = false` disables it, and
`--quiet` disables interactive output. This permits file-only logging during
unattended runs.

## Step-by-step implementation

### Step 1: Freeze current output contracts

Before editing behavior:

- Inventory `std::cout` and `std::cerr` in `examples/`, `src/`, and public
  headers.
- Classify each occurrence as result, report, help, error, diagnostic, or
  progress.
- Record the current training log path, append behavior, format, and flushing.
- Record stdout requirements from `docs/application_and_script_io.md`.
- Mark benchmark CSV/table output and `tests/test_main.cpp` output as excluded.

Acceptance:

- Every migrated line has an explicit category and level.
- No result, report, help, benchmark, or test-runner output is classified as a
  log by accident.

### Step 2: Add dependencies and CMake target

Update `vcpkg.json` with `spdlog` and `tomlplusplus`.

Update `CMakeLists.txt`:

- Add `find_package(spdlog CONFIG REQUIRED)` and
  `find_package(tomlplusplus CONFIG REQUIRED)` in the application-logging
  branch.
- Add `TEXASSOLVER_ENABLE_LOGGING`, defaulting to `ON`.
- Add `TEXASSOLVER_APPLICATION_SOURCES` for `src/logging/*.cpp`.
- Add the three `include/logging/*.hpp` files to
  `TEXASSOLVER_STABLE_INTERNAL_HEADERS`.
- Include `TEXASSOLVER_APPLICATION_SOURCES` in source classification.
- Add static target `texas_logging` and alias `TexasSolver::logging`.
- Give the target build-tree include access to `include/`.
- Link `spdlog::spdlog` publicly because logging macros use its formatting API.
- Link `tomlplusplus::tomlplusplus` privately because TOML types do not appear
  in public headers.
- Link application targets to `TexasSolver::logging`.
- Do not install or export `texas_logging` initially.

Acceptance:

- A library-only `TexasSolver::texas` target contains no logging calls.
- The installed package does not require spdlog or toml++.
- Every new source and header belongs to exactly one CMake classification.

### Step 3: Implement configuration types and defaults

Add dependency-free `Config` structures for logging, console, file, category,
and progress settings. Add exact string conversions for enums and fixed
category iteration.

Keep defaults safe:

- logging enabled;
- console enabled at `info`;
- file disabled unless requested;
- automatic colors;
- automatic progress mode;
- synchronous backend;
- warnings flush immediately.

Acceptance:

- Default construction is deterministic.
- All enum conversions round-trip.
- Invalid strings return structured errors rather than silently falling back.

### Step 4: Implement strict TOML loading

Implement `load_config(path)` in `src/logging/config.cpp` using toml++.
Separate parsing from precedence merging so tests can supply synthetic TOML,
environment maps, and CLI overrides without modifying process-global state.

Acceptance:

- Full example TOML parses.
- Invalid schema, types, names, sizes, and unknown keys fail with useful paths.
- Relative file paths resolve from the configuration directory.
- Explicit missing files fail; absent optional configuration uses defaults.

### Step 5: Implement bootstrap override parsing

Perform a small first pass over `argv` for logging options before normal
workflow parsing:

1. Read compiled defaults.
2. Resolve `TEXASSOLVER_LOG_CONFIG` or `--log-config`.
3. Parse TOML.
4. Apply environment overrides.
5. Apply CLI overrides.
6. Apply the existing workflow `log_path` compatibility override when the
   workflow configuration becomes available.

The normal CLI parser still owns validation of workflow options. The bootstrap
pass must ignore unrelated arguments and never consume them permanently.

Acceptance:

- Precedence is deterministic and tested.
- Logging is available for normal argument-validation failures.
- `--help` works without creating a log file.

### Step 6: Implement category logging and sinks

Implement one logger per fixed category. Share sink objects so multiple
categories writing the same file do not open it independently.

Add:

- console and rotating-file sinks;
- independent sink thresholds;
- category thresholds;
- configurable patterns;
- fallback error handling;
- explicit flush and shutdown;
- predictable repeated-initialization behavior.

Acceptance:

- One message can reach console and file simultaneously.
- Console and file levels filter independently.
- Category overrides work.
- File rotation retains the configured count.
- Shutdown flushes every accepted record.
- Sink failures do not recurse.

### Step 7: Implement full-disable paths

Add runtime checks before formatting. Add build-time definitions so
`TEXASSOLVER_ENABLE_LOGGING=OFF` removes macro calls and selects a stub backend
where required.

Acceptance:

- Runtime-disabled calls do not format arguments.
- Compile-time-disabled calls do not evaluate arguments.
- `--no-log` does not create a log file.
- The application still runs with logging disabled.

### Step 8: Implement terminal coordination and colors

Implement cross-platform terminal detection and a custom console sink that
cooperates with the progress manager.

Acceptance:

- Automatic color appears only on an interactive terminal.
- `always` and `never` override detection.
- `NO_COLOR` disables automatic color.
- Files and redirected output contain no ANSI codes.
- Concurrent console records remain complete lines.

### Step 9: Implement progress and live status

Implement `Progress` on top of a shared internal live-line renderer. Preserve
the latest state so ordinary logs can clear and redraw it. Keep computation of
rates, memory strings, and ETA at callback boundaries.

Acceptance:

- Interactive updates replace one line.
- A shorter update clears leftover characters.
- An ordinary log clears and redraws the progress line.
- Completion always renders and terminates the line once.
- Non-interactive mode emits periodic newline records.
- Concurrent log and progress calls do not interleave partial output.

### Step 10: Integrate application lifetime

Add an RAII logging session near the top of each selected executable `main()`:

1. Bootstrap configuration.
2. Initialize logging.
3. Run the command.
4. Flush on success or caught failure.
5. Shut down before local application state is destroyed.

Initialization failure uses bounded plain `stderr` output. Optional file-sink
failure falls back to console. Required sink failure returns a non-zero exit.

### Step 11: Pilot the multiway workflow

Start with `examples/multiway_workflow_main.cpp`.

Preserve solver callbacks:

- `MultiwayBucketProgressCallback`;
- `MultiwayBucketInspectionProgressCallback`;
- `MultiwayBlueprintBatchProgressCallback`.

Replace only their application adapters. Convert:

- startup and configuration summaries to `Workflow/Info`;
- recoverable resource conditions to `Validation/Warning`;
- checkpoint events to `Checkpoint/Info`;
- artifact operations to `Storage/Info`;
- failures to the owning category at `Error`;
- bucket, inspection, and training updates to `Progress`.

Remove manual console/file duplication from
`MultiwayTrainingProgressContext::log` and `log_line`. Preserve the existing
training audit path, append mode, plain messages, and flush ordering through a
dedicated run sink.

Do not change:

- workflow `.cfg` syntax;
- `--progress*` semantics;
- artifact paths;
- exit codes;
- report JSON;
- deterministic callback timing;
- solver results.

### Step 12: Migrate remaining diagnostics selectively

Review each executable after the pilot stabilizes.

Keep as normal output:

- help and usage text;
- JSON and machine-readable results;
- strategy and solver result tables;
- benchmark CSV and tables;
- profiler reports;
- test harness output.

Convert only operational diagnostics, warnings, lifecycle events, and failures.

Do not connect `include/solver/generic/dcfr.hpp` directly to TexasLogging.
Replace its direct warning later with a typed diagnostic callback or status
result so the installed solver header remains independent.

Keep `src/util/profiling.cpp` as report output. A later cleanup may add
`write_report(std::ostream&)`, but it is not part of logging migration.

### Step 13: Add focused tests

`tests/test_logging_config.cpp`:

- compiled defaults;
- complete TOML;
- invalid schema, key, category, level, mode, and integer bounds;
- explicit missing file;
- relative path resolution;
- defaults/TOML/environment/CLI precedence.

`tests/test_logging.cpp`:

- category and sink filtering;
- console plus file fan-out;
- rotation;
- append mode;
- flush and shutdown;
- repeated initialization;
- optional and required sink failure;
- no ANSI sequences in file output;
- runtime off.

`tests/test_logging_progress.cpp`:

- live replacement;
- shorter-line cleanup;
- throttling;
- guaranteed final update;
- line-mode fallback;
- disabled mode;
- clear/log/redraw ordering;
- concurrent record integrity.

`tests/test_logging_disabled.cpp`:

- compile with `TEXASSOLVER_ENABLE_LOGGING=0`;
- pass side-effect expressions to every severity macro;
- prove that no expression is evaluated.

Use injectable streams, clocks, terminal detection, and environment maps.
Tests must not depend on the real terminal, wall-clock timing, or global user
environment. Use temporary directories for file and rotation tests.

Update CMake boundary tests:

- `tests/cmake/test_multiway_workflow_boundary.cmake` for the new application
  progress adapter while retaining callback-boundary checks;
- `tests/cmake/test_package_config_contract.cmake` to confirm the installed
  solver package does not gain logging dependencies.

### Step 14: Update documentation

Add `docs/texas_logging.md` containing:

- complete TOML schema;
- category and level list;
- precedence rules;
- CLI and environment options;
- runtime and compile-time disable behavior;
- console/file patterns;
- live versus line progress behavior;
- examples for console-only, file-only, warnings-only, category overrides, and
  fully quiet runs.

Update:

- `README.md` dependencies, CMake option, and usage examples;
- `docs/application_and_script_io.md` stream, file, environment, and CLI
  contracts;
- `config/logging.example.toml` as the copyable reference configuration;
- `docs/PLURIBUS_LOG.md` only after implementation and verification are
  complete.

### Step 15: Review and verification

After implementation is stable:

1. Review correctness, API, lifetime, fallback, and compatibility.
2. Review hot-path placement, allocation, synchronization, and terminal locks.
3. Review test gaps and deterministic assertions.
4. Fix all actionable findings.
5. Run focused logging tests.
6. Run workflow boundary and package configuration tests.
7. Run `scripts/codex_powershell.ps1 scripts/python/full_build.py`.
8. Perform a final scope and documentation review.

Required final evidence:

- exact commands executed;
- targeted test results;
- full build and CTest result;
- confirmation that no hot loop contains logging;
- confirmation that machine-readable output is unchanged;
- confirmation that runtime and compile-time off paths work;
- any known limitations.

### Step 16: Commit structure

Create small commits only after verification:

1. Build: add spdlog, toml++, and the TexasLogging target.
2. Logging: add configuration, categories, sinks, and disable paths.
3. Console: add terminal coordination and progress rendering.
4. Workflow: migrate multiway operational logs and progress.
5. Tests: add logging and progress contracts.
6. Docs: document configuration and update the completed-task log.

Keep implementation and its direct regression tests together if splitting them
would leave an unverified intermediate commit. Never stage unrelated user
changes or generated build output.

## Completion criteria

The work is complete when all conditions hold:

- Console and rotating file output can be enabled independently or together.
- Console and file thresholds are independent.
- Fixed category overrides work from TOML.
- Console colors obey automatic, always, never, and `NO_COLOR` behavior.
- Live progress and ordinary logs do not corrupt each other.
- Redirected output falls back to newline records without ANSI sequences.
- Global, per-category, runtime, and compile-time disable paths work.
- Disabled logging does not evaluate formatting arguments.
- Existing workflow progress options remain compatible.
- Existing training audit-log behavior remains compatible.
- Help, JSON, reports, benchmark output, profiler reports, and test output are
  not incorrectly converted to logs.
- No logging, strings, formatting, allocation, or terminal work is added to
  traversal, row-update, terminal, or merge inner loops.
- `TexasSolver::texas` and its installed package remain independent of logging
  dependencies.
- Focused tests, boundary tests, full build, and CTest pass.
- Documentation and `docs/PLURIBUS_LOG.md` reflect only verified behavior.

## Deferred work

- Multiple simultaneous progress bars.
- Unicode width-aware rendering.
- Structured JSON log files.
- Network or system logging sinks.
- Runtime category registration.
- Asynchronous logging.
- Installing `TexasSolver::logging` as a public package component.
- Moving default application diagnostics from stdout to stderr as a separately
  reviewed I/O contract change.
