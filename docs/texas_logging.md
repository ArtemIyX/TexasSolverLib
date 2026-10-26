# TexasLogging

`TexasSolver::logging` is an internal application target. It is not installed and `TexasSolver::texas` has no logging dependency.

## Lifetime and macros

Construct `texas::logging::LoggingSession` near the start of an application and let it shut down before application state is destroyed. Call `flush()` before a checkpoint handoff when audit ordering matters.

```cpp
texas::logging::LoggingSession logging(config);
TEXAS_LOG_INFO(Workflow, "loaded {} rows", rows);
```

Categories are `Core`, `Config`, `Workflow`, `Solver`, `Storage`, `Checkpoint`, `Performance`, and `Validation`. Levels are `trace`, `debug`, `info`, `warning`, `error`, `critical`, and `off`. Macros check the category before formatting. With `-DTEXASSOLVER_ENABLE_LOGGING=OFF`, their arguments are not evaluated.

## Configuration

Copy [logging.example.toml](../config/logging.example.toml). The parser accepts schema version 1 only and rejects unknown keys, duplicate TOML keys, invalid values, and unsafe integer conversions. Relative file paths resolve from the TOML file.

Precedence is compiled defaults, TOML, environment, command line, then the workflow training audit path. No configuration file is loaded implicitly.

Environment variables are `TEXASSOLVER_LOG_CONFIG`, `TEXASSOLVER_LOG_LEVEL`, `TEXASSOLVER_LOG_FILE`, `TEXASSOLVER_LOG_COLOR`, and `NO_COLOR`. Command-line options are `--log-config`, `--log-level`, `--log-file`, `--log-color auto|always|never`, `--no-console-log`, `--no-file-log`, `--no-log`, and `--quiet`.

`--no-log` disables general and training-audit sinks. `--quiet` disables console logs and progress but preserves configured file and audit output. A category set to `off` is independently disabled.

Console and file thresholds are independent. A file sink can rotate and append. Optional file failures leave console logging available; a required file failure aborts initialization. Console colors are emitted only to interactive terminals. `NO_COLOR` disables automatic color. Files and redirected output never receive ANSI codes.

```toml
# Console only
[logging.console]
enabled = true
level = "info"

# File only, unattended
[logging.console]
enabled = false
[logging.file]
enabled = true
path = "logs/run.log"
level = "debug"

# Warnings on console, detailed file
[logging.console]
level = "warning"
[logging.file]
enabled = true
level = "debug"
[logging.categories]
Storage = "debug"

# Fully quiet
[logging]
enabled = false
```

## Progress

`Progress::start("Training", batches)` owns no solver state. Update it from application callback adapters with `update`, `field`, and `rate`, then call `finish`. One live ASCII line is supported. Ordinary console logs clear and redraw that line under the same lock. Non-interactive and CI output uses complete newline records. Existing `--progress`, `--progress-every`, `--progress-memory`, and `--progress-speed` options remain workflow controls.

The initial release is synchronous, supports one progress line, has no Unicode width handling, structured JSON logs, network sinks, runtime category registration, or asynchronous backend.
