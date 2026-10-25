# TexasSolver application and script I/O reference

## Scope

This document describes the command-line inputs, environment inputs, standard-stream outputs, files read, and files written by the repository's executable targets and scripts.

The reference is derived from `CMakeLists.txt`, the executable entry points, and the scripts in `scripts/`. It is an interface map, not a runtime-generated schema. No build, test, benchmark, or solver run was performed while creating it.

Unless stated otherwise:

- A relative path is resolved by the caller or by the script's repository root logic.
- Normal progress and result messages go to stdout. Usage text, invalid-argument messages, and failures generally go to stderr.
- A non-zero process exit code means argument validation, an input/artifact contract, a runtime guard, or a result gate failed.
- Binary `.bin` files are versioned internal artifacts. They should be consumed by the matching TexasSolver workflow rather than edited manually.
- JSON reports are evidence and telemetry artifacts. Their exact field set is controlled by the report schema in the corresponding C++ implementation and can evolve.

## Stage map

The main multiway workflow is:

```text
workflow .cfg
    |
    +--> texas_multiway_buckets --> buckets.bin + buckets.manifest
    |                                      |
    +--> texas_multiway_inspect <----------+
    |             |
    |             +--> bucket inspection report JSON
    |
    +--> texas_multiway_train --> blueprint.bin + training report + checkpoints + log
                                      |
                                      +--> texas_multiway_evaluate --> lookup_report.json
                                      |
                                      +--> texas_multiway_finalize --> final_acceptance.json
```

The Tracy training workflow wraps the same training stage:

```text
tracy-capture + texas_multiway_train_tracy
    --> capture.tracy
    --> tracy-csvexport output.csv
    --> run_manifest.json and training artifacts
```

## CMake options and target availability

The root CMake project always provides the `texas` static library. Executable families are conditional:

| Option | Enables |
|---|---|
| `TEXASSOLVER_BUILD_EXAMPLES=ON` | `texas_solver_example` and the research executable family |
| `TEXASSOLVER_BUILD_RESEARCH_EXAMPLES=ON` | Multiway workflow executables and benchmark/back-end comparison executables; requires examples |
| `TEXASSOLVER_BUILD_TESTS=ON` | One executable per `tests/test_*.cpp` source plus boundary CTest scripts |
| `TEXASSOLVER_BUILD_HEAVY_TESTS=ON` | Heavy boundary tests that are otherwise not registered |
| `TEXASSOLVER_BUILD_LEGACY_RESEARCH=ON` | Legacy vector research library support |
| `TEXASSOLVER_BUILD_HUNL_FIXED_RESEARCH=ON` | Fixed-private sampled HUNL support and its dependent test |
| `TEXASSOLVER_BUILD_TRACY_DIAGNOSTICS=ON` | Tracy-enabled multiway diagnostic executables |

The external Tracy tools are separate CMake projects under `external/tracy/capture`, `external/tracy/csvexport`, and `external/tracy/profiler`. They are not root TexasSolver application targets.

## C++ executable targets

### `texas_solver_example`

Source: `examples/solve_kuhn.cpp`

| Input | Output |
|---|---|
| No command-line arguments. The program uses a fixed Kuhn configuration: 10 iterations, alpha 1.5, beta 0.0, gamma 2.0. | stdout lines: `iterations`, `game_value`, and `exploitability`. No files are written. |

### Multiway workflow executable family

The following targets use `examples/multiway_workflow_main.cpp` and select their workflow from the executable name:

- `texas_multiway_train`
- `texas_multiway_buckets`
- `texas_multiway_inspect`
- `texas_multiway_evaluate`
- `texas_multiway_finalize`
- `texas_multiway_train_tracy` when Tracy diagnostics are enabled

Common options:

| Option | Input |
|---|---|
| `--help` | Prints workflow-specific usage and exits successfully. |
| `--config <path>` | Versioned workflow `.cfg` file. Used to resolve model identity, bucket sizes, capacities, seeds, default paths, and limits. |
| `--seed <integer>` | Seed override accepted by the dispatcher. |
| `--tiny` | Runs a self-contained tiny pipeline in a temporary directory. It is a diagnostic path, not a normal artifact production path. |

The workflow also reads these provenance environment variables when present:

- `TEXASSOLVER_GIT_COMMIT`
- `TEXASSOLVER_BUILD_CONFIGURATION`
- `TEXASSOLVER_COMPILER_IDENTITY`
- `TEXASSOLVER_TRACE_CAPTURE_ID` for Tracy training evidence

#### `texas_multiway_buckets`

Purpose: generate and atomically publish the canonical multiway bucket artifact.

Inputs:

- `--config <path>`
- `--output <path>`: destination bucket artifact; an existing destination is rejected.
- `--checkpoint-dir <path>`: optional resumable progress directory.
- `--threads <1..16>`
- `--benchmark-start <integer>`
- `--benchmark-tables <integer>`
- `--benchmark-mode build-only|end-to-end`

Files read:

- The workflow configuration.
- An existing temporary artifact and `latest.progress` only when resuming a checkpointed generation.

Files written:

- `<output>`: published bucket binary.
- `<output-with-extension-replaced-by-.manifest>`: bucket manifest JSON.
- `<output>.tmp` during atomic generation.
- `<checkpoint-dir>/latest.progress` when checkpointing is enabled.

The benchmark mode writes a temporary bucket file below the system temporary directory, reports a CSV-like single-line summary to stdout, and removes the temporary file before exit. It does not publish a requested output artifact.

stdout includes the resolved workflow identity, bucket counts, expected table count, thread selection, progress, and final byte/table counts.

#### `texas_multiway_inspect`

Purpose: verify bucket coverage, identity, and payload hashes.

Inputs:

- `--config <path>`
- `--input <path>`: bucket binary to inspect.
- `--report <path>`: optional JSON report destination.
- `--progress true|false`
- `--progress-every <integer>`
- `--hash parallel|legacy|both`
- `--threads <1..16>`

Files read: the workflow `.cfg` and bucket binary.

Files written: the optional report JSON. The report contains the model identity, evidence header, flop/turn/river table counts, live assignments, payload hashes, selected hash algorithm, effective thread count, and identity/hash booleans.

stdout reports the table counts, live assignments, payload hashes, and effective threads. A report with incomplete street coverage or an invalid artifact contract fails.

#### `texas_multiway_train` and `texas_multiway_train_tracy`

Purpose: train a full multiway blueprint from a verified bucket artifact.

Inputs:

- `--config <path>`
- `--input <path>`: bucket artifact; defaults to the path in the workflow config.
- `--output <path>`: full blueprint artifact; defaults to the workflow config.
- `--report <path>`: training report JSON; defaults to `<output>.json` when no configured report is supplied.
- `--checkpoint-dir <path>`: checkpoint directory; defaults to the workflow config.
- `--resume <path>`: checkpoint to resume.
- `--batches <integer>`
- `--threads <1..16>`
- `--pin-workers true|false`
- `--scheduler fixed|dynamic`

Files read:

- Workflow `.cfg`.
- Bucket binary.
- Resume checkpoint when `--resume` is supplied.
- Existing checkpoint state when the configured checkpoint directory supports resumption.

Files written:

- `<output>`: final full blueprint binary.
- `<report>`: training evidence and telemetry JSON.
- `<checkpoint-dir>/latest.bin` and checkpoint/progress sidecars while training.
- `<report>.log` by default, or the configured training log path.
- Temporary files used for atomic publication.

The report includes the training counts and gates, process and worker timing, memory/preflight values, capacity telemetry, allocation probe state, scheduler/affinity information, deterministic merge fingerprint, blueprint/checkpoint sizes, and Tracy provenance fields when applicable. stdout mirrors the important configuration, progress, gate, and timing information; the same training lines are appended to the training log.

`texas_multiway_train_tracy` has the same workflow I/O as the normal trainer and adds Tracy instrumentation. The executable itself does not create a `.tracy` file. `tracy-capture` creates that file externally.

#### `texas_multiway_evaluate`

Purpose: replay required lookups against a bucket artifact and full blueprint.

Inputs:

- `--config <path>`
- `--artifacts <directory>`: directory containing `buckets.bin` and `blueprint.bin`.
- `--duplicates <integer>`: duplicate deal/lookup count.
- `--report <path>`: optional report destination.

Files read:

- `<artifacts>/buckets.bin`
- `<artifacts>/blueprint.bin`
- Workflow `.cfg`

Files written:

- `<report>` or `<artifacts>/lookup_report.json` by default.

The lookup report records qualification evidence, replay fingerprints, lookup hits, missing infosets/buckets, and action-menu mismatches. The command fails if qualification does not pass or a repeated lookup is not equivalent.

#### `texas_multiway_finalize`

Purpose: combine stage evidence into the final F1 acceptance report.

Inputs:

- `--output <path>`: required final report path.
- `--bucket-report <path>`
- `--training-report <path>`
- `--equivalence-report <path>`
- `--lookup-first <path>`
- `--lookup-second <path>`
- `--operator <string>`
- `--start-utc <timestamp>`
- `--end-utc <timestamp>`
- `--command <string>`
- `--machine-report <path>`
- `--exit-code <integer>`

Files read: the supplied stage reports and optional human/machine-run evidence.

Files written: the requested final acceptance JSON, published atomically. stdout confirms the published path.

#### `--tiny` pipeline outputs

The tiny path creates a temporary directory named `texas_solver_f2_tiny` and writes:

- `future-buckets.bin`
- `root.bin`
- `full-blueprint.bin`

It prints a pass summary and cleans up according to the temporary-directory implementation.

### `texas_solver_hunl_mccfr_postflop`

Source: `examples/hunl_mccfr_postflop_main.cpp`

Purpose: run the sampled/exact HUNL postflop solver and print a diagnostic profile.

Inputs:

`--street flop|turn|river`, `--mode exact|public-chance|external|average-strategy`, `--iterations N`, `--traversals N`, `--batch-size N`, `--time-budget-ms N`, `--seed N`, `--workers N`, `--stack N`, `--range-hands N`, `--buckets N`, `--flop-buckets N`, `--turn-buckets N`, `--river-buckets N`, `--precision float64|float32|compressed16`, `--sparse`, `--keep-dense-validation`, `--discounting`, `--variance-reduction`, `--single-player`, `--iterative-external-dense`, `--as-epsilon X`, `--as-tau X`, and `--as-beta X`.

Files read/written: no application artifact files are created by this entry point. It prints configuration, traversal counters, strategy/root diagnostics, timing, and solver profile data to stdout. Invalid options and failures go to stderr.

### `texas_solver_hunl_mccfr_scaling`

Source: `examples/benchmarks/hunl_mccfr_scaling_main.cpp`

Inputs:

- `--preset tiny|scaling`
- `--iterations N`, `--traversals N`, `--batch-size N`, `--seed N`
- `--batch-sweep`
- `--batch-sizes 64,128,256,512,1024`
- `--workers 1,2,4,8,16`
- `--mode exact|public-chance|external|average-strategy`
- `--sparse`, `--discounting`, `--single-player`

Outputs: stdout benchmark rows, timing/counter summaries, and CSV-style sections for scaling and batch-sweep comparisons. No files are written by the executable.

### `texas_solver_hunl_random_flat`

Source: `examples/benchmarks/hunl_random_flat_main.cpp`

Inputs:

- `--preset conservative|balanced`, or explicit `--workers N`, `--iterations N`, `--sample-traversals N`, `--time-budget-ms N`, `--sampling exact|public-chance|external|average-strategy`, `--depth-limit N`, `--buckets N`, `--streets flop|turn|river`, `--precision double|float`, `--seed N`, `--debug`.

Outputs: stdout configuration, preflight memory estimate, timing, sampled counters, root/strategy information, and profile sections. `--debug` enables `TEXASSOLVER_PROFILE=1` and sets `TEXASSOLVER_PROFILE_DIR=artifacts/prof`; the profiling subsystem may write profiling artifacts there. Without debug, no explicit result file is produced by this entry point.

### `texas_solver_benchmark`

Source: `examples/benchmarks/main.cpp`

Inputs:

- Normal mode: `<game> <iterations> <workers> [frontier_multiplier] [--force-parallel] [--profile]`.
- `<game>` is `kuhn`, `leduc`, or `hunl`.
- Layout mode: `layout [iterations]`.

Outputs: stdout benchmark rows, durations, solver result values, and optional profile output. `--profile` enables `TEXASSOLVER_PROFILE=1`; the profiling subsystem controls any profile files. Unknown games and argument errors print usage to stderr.

### `texas_solver_flat_scheduler_benchmark`

Source: `examples/benchmarks/flat_scheduler_main.cpp`

Inputs:

```text
<iterations> <workers>
[action-hand|hand-action]
[conservative|balanced]
```

`workers` is a comma-separated positive list. The optional layout selects infoset/action/hand storage order. The optional preset selects the RTA flop conservative or balanced configuration; without a preset the benchmark-turn configuration is used.

Outputs: stdout graph dimensions, memory estimates, guardrail results, total duration, expected backward cost by worker/depth, stage timings, worker timings, and imbalance ratios. No files are written.

### `texas_solver_hunl_backend_compare`

Source: `examples/benchmarks/hunl_backend_compare_main.cpp`

Input: optional `--iterations N`, default 20.

Output: stdout comparison table for old recursive sequential/parallel and flat sequential/parallel backends at worker counts 1, 2, 4, 8, and 16. No files are written.

### `texas_solver_multiway_training_scaling`

Source: `examples/benchmarks/multiway_training_scaling_main.cpp`

Inputs:

- `--workers 1,2,4,8,16`
- `--batch-sizes 1000,4000,16000`
- `--warmup-batches N`
- `--timed-batches N`
- `--repeats N`
- `--seed N`

Outputs: stdout `sample` rows and `summary` rows containing throughput, wait/lock/active/sort/merge timings, worker trajectory ranges, delta sizes, and deterministic merge fingerprints. No files are written. The Tracy target `texas_solver_multiway_training_scaling_tracy` has identical command-line and stdout I/O with Tracy zones enabled.

### Tracy external executables

These are built from the vendored Tracy source, not from the root application CMake target list.

| Executable | Inputs | Outputs |
|---|---|---|
| `tracy-capture` | Tracy capture options. The repository wrappers use `-o <trace> -f -s <seconds>`. | `.tracy` capture file plus tool stdout/stderr. |
| `tracy-csvexport` | The repository wrapper uses `-f mw. <trace>`. | CSV text on stdout, redirected by the wrapper to `<capture-id>.csv`, plus stderr log. |
| `tracy-profiler` | Tracy UI/runtime options. | Interactive profiler UI; no TexasSolver workflow artifact contract. |

The repository wrappers require Tracy capture version 0.14.1.

## C++ test executables

When `TEXASSOLVER_BUILD_TESTS=ON`, CMake creates a target named `test_<source-stem>` for each test source except the shared `tests/test_main.cpp` harness and links the harness into every target. Each test executable accepts at most one positional substring filter. With a filter, only registered test cases whose name contains that substring execute.

Test stdout uses `[RUN]`, `[PASS]`, and `All tests passed (N)`. Failures use `[FAIL]` on stderr and return 1. No test executable writes a report file by default.

The current test target inventory is:

```text
test_abstraction_loader             test_abstraction_platform       test_abstraction
test_arena                          test_br_walk_mode_contract       test_canonical_combo
test_class169_solver_contract       test_core_facade                  test_core_fingerprint
test_core_poker_domain              test_dcfr_discount_contract       test_dcfr
test_eval                           test_exact_oracle_count_contract  test_hunl_bucket_model
test_hunl_card_validation           test_hunl_eval                    test_hunl_flat_dcfr
test_hunl_flat_expected_value       test_hunl_flat_graph               test_hunl_flat_mccfr
test_hunl_flat_pipeline             test_hunl_flat_state                test_hunl_p75_row_math
test_hunl_regressions               test_hunl_sampled_solver             test_hunl_sampled_stable
test_hunl_solver_storage_loader     test_hunl_solver_storage              test_hunl_state
test_hunl_tree                      test_infoset_lookup                  test_infoset_registry
test_iteration_range                test_kuhn                           test_leduc
test_multiway_action_abstraction     test_multiway_action_calibration     test_multiway_aivat_record
test_multiway_artifact_preflight    test_multiway_artifact
test_multiway_audit_round_4         test_multiway_baseline                 test_multiway_blueprint_store
test_multiway_blueprint_training    test_multiway_bucket_artifact          test_multiway_bucket_catalog
test_multiway_bucket_generation     test_multiway_bucket_model              test_multiway_cfr
test_multiway_checkpoint_equivalence test_multiway_compact_storage          test_multiway_continuation_cache
test_multiway_continuation_policy   test_multiway_continuation_selector     test_multiway_evaluation
test_multiway_evidence              test_multiway_f1_acceptance              test_multiway_f3_regressions
test_multiway_fixed                 test_multiway_full_hand_session          test_multiway_future_bucket
test_multiway_leaf_evaluator        test_multiway_memory                    test_multiway_model_identity
test_multiway_numerical_contract    test_multiway_p7_determinism_contracts   test_multiway_p7_hot_path_contracts
test_multiway_p7_memory_contracts   test_multiway_p7_profile_contracts       test_multiway_p8_differential
test_multiway_parallel_training     test_multiway_phase4                     test_multiway_phase5_p53_p56
test_multiway_private               test_multiway_public_builder               test_multiway_public_catalog
test_multiway_public_chance_sampler test_multiway_public_chance_traversal     test_multiway_qualification_reports
test_multiway_range_belief           test_multiway_recursive_traversal         test_multiway_replay
test_multiway_resolver_budget        test_multiway_resolver_policy             test_multiway_resolver
test_multiway_rollout_leaf           test_multiway_rules_contract              test_multiway_search_session
test_multiway_sizing_report          test_multiway_solver                       test_multiway_state
test_multiway_terminal_adapter       test_multiway_terminal                      test_multiway_workflow_config
test_parallel_dcfr                   test_pcs                              test_portable_binary
test_preflop_equity_validation       test_preflop_tree_action_contract      test_ranges_solver_integration
test_ranges_threading                test_simd                             test_solver_exploitability_contract
test_thread_join_guard
```

In addition to these executables, root CMake registers boundary tests that invoke scripts or standalone fixtures. Their exact registration is conditional on the build options and platform.

## Python scripts

All Python scripts are invoked with Python 3. They print a compact result to stdout and use the process exit code for success/failure unless noted otherwise.

| Script | Inputs | Outputs and side effects |
|---|---|---|
| `scripts/python/full_build.py` | No CLI arguments. | Runs `compact_build.py`, then `compact_ctest.py`; on failure invokes the matching scanner. Creates/updates `tmp/build_debug.log`, `tmp/ctest_debug.log`, `tmp/build_errors.txt`, and `tmp/ctest_errors.txt`. This is a build/test orchestrator. |
| `scripts/python/compact_build.py` | `--root`, `--build-dir`, `--configuration Debug|Release|RelWithDebInfo`, `--parallel N`, repeatable `--target`, `-o/--output`. | Invokes CMake build or MSBuild discovery on Windows. Writes the complete build transcript to the output log and prints a PASS/FAIL summary. It changes build outputs. |
| `scripts/python/compact_ctest.py` | `--root`, `--parallel N`, `-o/--output`. | Runs CTest against `build` in Debug configuration, writes the full transcript, and prints a summary. |
| `scripts/python/scan_build_errors.py` | Optional positional input log; `-o/--output`, `--warnings`, `--last N`, `--context N`. | Reads a build/test/runtime log and writes a deduplicated Markdown failure report, default `tmp/build_errors.txt`. Returns 1 when failures are found. |
| `scripts/python/scan_ctest_errors.py` | Optional positional CTest log; `-o/--output`. | Extracts `[FAIL]` records into a Markdown report, default `tmp/ctest_errors.txt`. Returns 1 when failures are found. |
| `scripts/python/list_recent_changes.py` | `--root`, `--commits N`, `--files N`. | Reads Git branch/status/history and prints a compact report. No files are written. |
| `scripts/python/repo_summary.py` | Optional repository root positional argument. | Scans source/configuration areas, skips generated/vendor trees, and prints a repository map. No files are written. |
| `scripts/python/cmake_summary.py` | Optional root positional argument; `--target`, `--tests`, `--all-files`. | Parses CMake files and prints target, source, link, property, and test maps. No files are written. |
| `scripts/python/cpp_file_summary.py` | Required C++ file positional argument; `--members`, `--signatures`, `--top N`. | Prints includes, types, functions, and TODOs for one C++ file. No files are written. |
| `scripts/python/extract_includes.py` | Optional root positional argument; `--reverse NAME`, `--modules`, `--missing`, `--top N`. | Prints include/import dependency maps, reverse dependencies, missing includes, and cycles. No files are written. |
| `scripts/python/extract_symbols.py` | Optional root; `--file`, `--module`, repeatable `--type`, `--signatures`, `--inheritance`, `--public`, `--compact`, `--top N`, repeatable `--area`, `--sort`. | Prints a symbol index. No files are written. |
| `scripts/python/find_large_files.py` | Optional root; `--min-size MB`, `--top N`, repeatable `--ext`, `--dir`, `--exclude`, `--include-generated`, `--all`, `--tracked`, `--sort`, `--json`, `--summary`. | Scans files and prints a table or JSON. No files are written. |
| `scripts/python/format_cpp.py` | Required subcommand `file <path>`, `folder <path>`, or `project`. Requires `clang-format` on PATH. | Runs `clang-format -i` and modifies selected `.h`, `.hpp`, and `.cpp` files in place. Prints per-file results and a count. |

`full_build.py`, `compact_build.py`, and `compact_ctest.py` execute build/test commands. They are listed here for completeness but are not part of the no-build/no-run inspection performed for this documentation task.

## PowerShell workflow and measurement scripts

### `scripts/codex_powershell.ps1`

Input: one required command followed by arbitrary command arguments.

Behavior: normalizes process `PATH` handling case-insensitively, invokes the requested command, and returns the child exit code. It does not create files itself.

### `scripts/run/run_multiway_ram_1000.ps1`

Purpose: run a bounded multiway training workload while collecting process memory and report telemetry.

Parameters:

- `-Workers 1..16`, default 8
- `-Batches 1..100`, default 1
- `-TrajectoriesPerBatch`, default 1000
- `-PinWorkers`
- `-Scheduler fixed|dynamic`, default fixed
- `-BinaryKind normal|tracy`, default normal
- `-BinaryPath`
- `-TraceCaptureId`
- `-RunDirectory`

Default inputs:

- Normal trainer: `build\Debug\texas_multiway_train.exe`
- Tracy trainer: `build-tracy\RelWithDebInfo\texas_multiway_train_tracy.exe`
- Source config: `artifacts\f1_sizing_v1\pilot_batch1.cfg`
- Bucket artifact: `artifacts\f1_sizing_v1\buckets\buckets.bin`

Files written below the run directory:

- `pilot_batch.cfg`: copied and adjusted local config.
- `blueprint.bin`: trainer output.
- `report.json`: trainer report.
- `run_metrics.json`: wrapper metrics including exit code, CPU time, peak working set, and report path.
- `checkpoints\`: trainer checkpoint files.
- `stdout.log` and `stderr.log`: redirected trainer streams.

The script also prints configuration, live working-set samples, report gates, phase timing, worker telemetry, and the final pass/fail result. It validates expected trajectory counts, zero discards, no failure/capacity exhaustion, and required telemetry.

The runner has a hard system-memory ceiling: it samples total and available physical memory while the child is running and stops the child when used physical memory exceeds total physical memory minus 1.5 GiB. Missing or invalid runner telemetry also stops the child. The production trainer applies the same fail-closed guard at batch and export boundaries and records the total, available, used, and maximum permitted values in the training report.

### `scripts/run/run_multiway_worker_matrix.ps1`

Purpose: repeat bounded runs across worker, pinning, and scheduler combinations.

Parameters:

- `-Batches`, default 3
- `-TrajectoriesPerBatch`, default 200
- `-Pinning pinned|unpinned`, default pinned
- `-Scheduling both|fixed|dynamic`, default both
- `-Repeats 1..20`, default 3
- `-Workers`, string array default `4`, `8`
- `-OutputDirectory`, default `tmp\multiway_scheduler_<total>`

Inputs: the same config, bucket artifact, and trainer selected by `run_multiway_ram_1000.ps1`.

Files written:

- One child run directory per matrix cell/repeat, containing the RAM script outputs.
- `worker_matrix.json`: normalized result rows and median/min/max throughput.
- `worker_matrix.csv`: the same result rows in CSV form.

stdout prints a matrix table and the final correctness/resource gate. The script requires one non-zero deterministic merge fingerprint and checks report schema, allocations, throughput, RSS, worker telemetry, and zero discards.

### `scripts/run/run_multiway_tracy_capture.ps1`

Purpose: capture the standalone multiway scaling diagnostic executable.

Parameters:

- `-CaptureToolPath`
- `-DiagnosticBinaryPath`
- `-OutputDirectory`
- `-CaptureId`
- `-CaptureSeconds 1..60`, default 10
- `-ConnectionTimeoutSeconds 2..60`, default 15

Defaults:

- Capture tool: `build-tracy-capture\RelWithDebInfo\tracy-capture.exe`
- Diagnostic binary: `build-tracy\RelWithDebInfo\texas_solver_multiway_training_scaling_tracy.exe`
- Output directory: `tmp\tracy\smoke`

Files written:

- `<capture-id>.tracy`
- `capture.stdout.log`, `capture.stderr.log`
- `diagnostic.stdout.log`, `diagnostic.stderr.log`
- `run_manifest.json`

The manifest records Tracy version 0.14.1, capture ID, UTC time, Git commit, build/compiler provenance, tool paths and SHA-256 hashes, arguments, and trace size. The wrapper invokes the diagnostic with one worker, batch size 1000, one warmup batch, 15 timed batches, and one repeat.

### `scripts/run/run_multiway_tracy_training_capture.ps1`

Purpose: capture a real Tracy-enabled multiway training run and export Tracy measurements.

Parameters:

- `-CaptureToolPath`, `-CsvExportToolPath`, `-TrainingScriptPath`, `-OutputDirectory`, `-CaptureId`
- `-CaptureSeconds 1..300`, default 60
- `-ConnectionTimeoutSeconds 2..120`, default 30
- `-Workers 1..16`, default 1
- `-Batches 1..10`, default 2
- `-TrajectoriesPerBatch`, default 1000
- `-Scheduler fixed|dynamic`, default fixed
- `-PinWorkers`

Defaults:

- Capture tool: `build-tracy-capture\RelWithDebInfo\tracy-capture.exe`
- CSV exporter: `build-tracy-csvexport\RelWithDebInfo\tracy-csvexport.exe`
- Training wrapper: `scripts\run\run_multiway_ram_1000.ps1`
- Output directory: `tmp\tracy\<capture-id>`

Files written:

- `<capture-id>.tracy`
- `<capture-id>.csv` and `<capture-id>.csv.stderr.log`
- `capture.stdout.log` and `capture.stderr.log`
- `capture_wrapper.log` and `orchestrator.log`
- `run.cfg`, `blueprint.bin`, `report.json`, `run_metrics.json`
- `checkpoints\`
- `run_manifest.json`

The manifest records the workload, Tracy 0.14.1, tool hashes/arguments, trace/CSV sizes, training wrapper arguments, and all principal training artifact paths. The script requires non-empty trace and CSV output plus the training report and metrics artifacts.

## Common artifact contracts

| Artifact | Producer | Consumer | Meaning |
|---|---|---|---|
| Workflow `.cfg` | Project setup or sizing workflow | Buckets, inspect, train, evaluate, finalize | Versioned model, capacity, path, seed, and execution configuration. |
| `buckets.bin` | `texas_multiway_buckets` | Inspect, train, evaluate | Canonical serialized multiway bucket tables. |
| `*.manifest` | Bucket generation | Operators and validation tooling | Bucket identity, coverage, and file metadata. |
| `latest.progress` | Bucket generation checkpointing | Bucket generation resume path | Ordered table progress and artifact identity. |
| `blueprint.bin` | Multiway training | Evaluate and downstream solver consumers | Full trained blueprint artifact. |
| `latest.bin` and checkpoint files | Multiway training | Training resume/equivalence checks | Recoverable training state. |
| `report.json` | Multiway training and RAM wrapper | Matrix scripts, finalization, operators | Training results, gates, telemetry, memory, and provenance. |
| `lookup_report.json` | Multiway evaluate | Finalization and qualification review | Required lookup/replay qualification evidence. |
| `final_acceptance.json` | Multiway finalize | Release/acceptance review | Combined F1 acceptance result. |
| `run_metrics.json` | `run_multiway_ram_1000.ps1` | Worker matrix and operators | External process measurements and report path. |
| `worker_matrix.json` / `.csv` | `run_multiway_worker_matrix.ps1` | Performance review | Cross-run worker/pinning/scheduler comparison. |
| `.tracy` | `tracy-capture` wrappers | Tracy profiler and CSV exporter | Binary Tracy capture. |
| Tracy CSV | `tracy-csvexport` wrapper | Spreadsheet/analysis tools | Exported Tracy measurement rows, filtered by `mw.` in the training wrapper. |
| `run_manifest.json` | Tracy wrappers | Reproducibility review | Tool hashes, provenance, workload, and output paths. |

## Build-related script outputs

The build helpers use these default paths:

- `compact_build.py`: `tmp/build_debug.log`
- `compact_ctest.py`: `tmp/ctest_debug.log`
- `scan_build_errors.py`: `tmp/build_errors.txt`
- `scan_ctest_errors.py`: `tmp/ctest_errors.txt`

These are generated diagnostics and should not be treated as source-controlled application artifacts unless a task explicitly requires preserving them.

## Source references

- Root target and option definitions: `CMakeLists.txt`
- Workflow CLI and artifact publication: `examples/multiway_workflow_main.cpp`
- HUNL postflop CLI: `examples/hunl_mccfr_postflop_main.cpp`
- Benchmark CLIs: `examples/benchmarks/*.cpp`
- Shared test harness: `tests/test_main.cpp`
- Python utilities: `scripts/python/*.py`
- Build helpers: `scripts/build_*.ps1`
- Analysis scripts: `scripts/analyze/*.ps1`
- Run and Tracy orchestration: `scripts/run/*.ps1`
