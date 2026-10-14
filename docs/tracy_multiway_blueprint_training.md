# Tracy multiway training operator guide

The repository uses Tracy 0.14.2 only in diagnostic binaries. The normal
`texas` target remains the authoritative correctness and performance target.

## Built tools

Diagnostic solver binaries:

```text
build-tracy/RelWithDebInfo/texas_multiway_train_tracy.exe
build-tracy/RelWithDebInfo/texas_solver_multiway_training_scaling_tracy.exe
```

Matching Tracy tools:

```text
build-tracy-profiler/RelWithDebInfo/tracy-profiler.exe
build-tracy-capture/RelWithDebInfo/tracy-capture.exe
build-tracy-csvexport/RelWithDebInfo/tracy-csvexport.exe
```

All tools must be from Tracy 0.14.2. The client is configured for on-demand,
localhost-only, IPv4 collection. Sampling, call stacks, locks, and Tracy
memory hooks are disabled by default.

## Manual profiler connection

To connect by hand, open `tracy-profiler.exe`, choose Connect, and enter:

```text
127.0.0.1:8086
```

Then start one `_tracy` diagnostic binary. Connect before the measured batches
so on-demand collection does not omit the beginning of the run. Save the trace
from the profiler UI when the run is complete.

The profiler and capture utility are alternative consumers of one diagnostic
client connection. Do not start both against the same process.

## Bounded capture script

For an unattended bounded capture, use the repository wrapper. It starts the
capture utility and a one-worker, one-batch smoke benchmark, then writes a
trace and manifest under `tmp/tracy/`:

```powershell
& .\scripts\codex_powershell.ps1 powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\run_multiway_tracy_capture.ps1
```

The wrapper checks the capture tool version, records binary hashes and the
capture ID, and fails if the client does not connect within the timeout.
Loopback socket access may require running this command outside a restricted
process sandbox.

## CSV export

The CSV exporter writes to standard output. For the captured trace:

```powershell
& .\scripts\codex_powershell.ps1 powershell -NoProfile -Command '$p = Start-Process -FilePath ''.\build-tracy-csvexport\RelWithDebInfo\tracy-csvexport.exe'' -ArgumentList @(''-f'',''mw.'',''tmp\tracy\pair.tracy'') -RedirectStandardOutput ''tmp\tracy\pair.csv'' -Wait -PassThru; if ($p.ExitCode -ne 0) { exit $p.ExitCode }'
```

The exported rows include the coarse worker, coordinator, trainer, and
telemetry zones. JSON reports remain authoritative for counts, fingerprints,
memory admission, and unprofiled throughput.

## Interpretation rules

- Do not use `_tracy` timings as throughput qualification data.
- Check the JSON fingerprint and accepted/discarded counts first.
- Compare `mw.worker.active`, `mw.worker.traversal`, coordinator admission,
  merge, and trainer batch zones against the matching report.
- Keep captures and exports under `tmp/`; they are not committed.
