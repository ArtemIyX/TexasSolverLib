# PB-12 post-stop diagnostic draft

Status: preparation only. The user reports that the focused monitor checks
passed. A full monitor/trainer integration and disk scan under concurrent
mutation remain unverified. A diagnostic run needs separate authorization;
this is not a Stage 9.4 retry or Stage 9.5.

The focused monitor checks, reported passed by the user, are:

```powershell
./scripts/run/test_pb12_diagnostic_monitor.ps1
```

Those checks use temporary files and two short-lived PowerShell processes;
they do not start the solver. They cover strict memory and disk boundaries,
P99 cohort and lower-bound decisions, incomplete journal records, truncation,
referenced segment lengths, unreferenced file accounting, and stopping one
process while leaving another running. A successful test is necessary but does
not validate a full disk scan under concurrent mutation or a real trainer
integration.

The prepared configuration is `tmp/pb12_diagnostic_20261029_guardfix/training.conf`.
It selects three 1,000-trajectory batches, one fixed worker, seed 1, PB-12
targeted coverage, and a checkpoint after every batch. Storage, cache, WAL,
segment, fan-in, scratch, and durability settings are copied from the stopped
Stage 9.4 configuration. Output paths point only into the fresh diagnostic
directory. `plan.json` records the configuration hash and stop limits.
`preparation_evidence.json` records the observed binary hash and bucket-file
metadata. The bucket's size and timestamp match the stopped run's preflight;
its content hash was not measured, so that is not proof of identical contents.
At launch the runner rejects a changed binary hash, configuration hash, or
bucket size or timestamp, and requires the recorded metadata comparison to
the Stage 9.4 preflight to match.

The existing `tmp/ctest_debug.log` reports 128/128 tests passed, including the
external-storage, solver, journal, and blueprint-training executables. This is
saved prior evidence, not a test run performed during this preparation. The
saved `tmp/build_debug.log` is empty, so it does not independently establish a
build result. Neither log covers the new PowerShell monitor checks.

After review and separate authorization, the proposed command is:

```powershell
./scripts/run/run_pb12_diagnostic.ps1 -RunName pb12_diagnostic_20261029_guardfix
```

The runner refuses existing output and a changed configuration. It starts one
hidden trainer and holds that process's handle. It does not search for or stop
other trainers. Resource polling is nominally every 250 ms; disk accounting is
asynchronous and fails closed on errors or a 30-second observation deadline.
Once the training manifest exists, the disk sampler treats never-published
components as empty and counts every physical store file. A previously seen
manifest disappearing causes a stop. An empty store may remain while the bucket
artifact loads. If files appear in the store and its training manifest remains
missing for 30 seconds, disk telemetry is unavailable and the run stops.
The sampler verifies component and pin manifest checksums and referenced file
lengths. Each disk-sampling job loads a small checksum helper; its elapsed
time is recorded and must be assessed as part of monitor overhead.
Samples and monitor events are flushed to `monitor.jsonl`. The trainer writes
its own exclusive, synchronously flushed `training.log.diagnostics.*.jsonl`.
The live loop reads only the checkpoint header; full checkpoint validation is a
separate post-run requirement.
The monitor validates complete journal events and does not infer a commit from a
tentative component-publication event. A partial final line is not a completed
record. If the monitor stops or fails, preserve both journals and the store.
The launcher reports a nonzero exit for a hard stop, trainer failure, missing
report, or incomplete required observations. A zero exit does not qualify the
P99 gate; it remains inconclusive for this short workload.

The existing stops remain: working set above 40 GiB, private commit above
48 GiB, available physical memory below 3 GiB, disk amplification above 3x,
and increasing P99 across three successive two-cycle cohorts. The current
runner applies no temporary-compaction exception. This diagnostic can produce
at most two checkpoint-to-checkpoint cycles, so the P99 trend is
**inconclusive**, not a pass. Live sampling is not instantaneous; a limit may
be crossed between samples. Missing checkpoint, journal, disk, or process
observations invalidate the diagnostic rather than establish a pass.

The monitor cannot establish from a temporary filename alone that a bounded
compaction is in flight. It therefore stops conservatively at any observed
amplification above 3x, including when such a temporary exists.

## Writer-side 3x bound

The external writer now has an opt-in `--pb12-disk-amplification-bound` flag;
the diagnostic launcher passes it and fixes the bound at 3x. Generic external
storage runs keep the default unbounded setting. Before each component append,
the writer counts every file under the store and reads active component
manifests. Synchronous bounded training uses the already durable aggregate
transaction WAL and component-manifest backups for append recovery, so it does
not write a duplicate payload-sized component WAL. Its append preflight counts
the staged data and index plus the next component manifest; the aggregate
transaction WAL and backups are already in the scanned store total. The later
aggregate-manifest temporary is preflighted at commit, against the updated live
segment denominator. Generic component stores retain their local WAL. Later
chunks recompute the budget after earlier publications. The single root record
is published before `TRAINING_CURRENT` exists, when the monitor allows an empty
store; all later appends are bounded.

Compaction performs an index-only, fan-in-bounded merge pass to count distinct
output keys before writing. It preflights the exact output size and checks both
the staging peak and the post-manifest state while source runs remain on disk.
This covers retained runs needed for aggregate rollback and checkpoint-pinned
runs. New batch runs are consolidated to one suffix run; a further fan-in merge
keeps its inputs until aggregate commit, then orphan cleanup runs. Suffix merge
input streams close before deleting their runs; bounded-mode cleanup errors fail
closed so repeated merges cannot silently accumulate orphan bytes. The merge
order and synchronous file and directory flushes remain deterministic. The
writer fails closed if a legacy run count cannot be reduced within the
rollback-safe fan-in bound. The extra count pass rereads the selected index
runs and can affect diagnostic latency; it uses the existing bounded
compaction cursors and payload scratch.

The stop sample's 1.5-second scan read manifests and file sizes across multiple
filesystem operations. Its recorded total is evidence for that sample only;
the post-stop WAL and temporary file lengths do not provide an instantaneous
component breakdown. The prepared `tmp/pb12_diagnostic_20261029_postfix` run
stopped on its first catalog append with `external storage disk amplification
budget cannot admit a record`. Its two monitor scans each recorded 397 live
bytes and 581 stored bytes, below 3x. The trainer exited before the first batch
or checkpoint, and the monitor's completion reason was overwritten by its
generic incomplete-observation reason. This is not a completed-run result.
The follow-up fix removes duplicate component-WAL accounting only for
synchronous bounded aggregate training, adds append and commit-phase
reservations, and preserves outer-journal rollback recovery. The user-reported
Debug build, compact CTest (128/128), and focused PowerShell monitor checks
passed; CTest includes all 61 storage cases. An earlier post-fix build attempt
failed in `test_multiway_phase4`'s `vcpkg z-applocal` step (exit code 32), then
passed on rerun.

The first post-fix launch in `tmp/pb12_diagnostic_20261029_verifiedprep`
stopped when the monitor observed 3,000,545,280 bytes of available physical
memory, below its unchanged 3 GiB floor. Its log had reached batch 3/3, but the
process exited -1 without a final report, so preserve it as incomplete. The
fresh `tmp/pb12_diagnostic_20261029_memretry` run completed with exit 0, three
batches, 3,000 accepted trajectories, no discards, `failed=false`, and a
successful policy export. The monitor recorded three journal batches and
checkpoints, no stop reason, and 178 disk scans; the maximum reported disk
amplification was 1.6458x. Each disk scan spans sequential filesystem reads,
so its live/store/temporary values are not an instantaneous component
breakdown. Compaction read/write amplification was 1.20128x/1.17755x.

The journal run counts at batch boundaries were catalog 3, 4, 2; rows 2, 3,
4; pages 1, 2, 3. This short observation does not establish a general bound on
run-count growth. Three checkpoints provide only two cycles, so the P99 gate is
inconclusive. The bucket size and timestamp match the Stage 9.4 baseline, but
its content hash remains unmeasured. Preserve all prior run folders. This
bounded result does not qualify Stage 9.4 or Stage 9.5.

After the process exits, inspect the journal without modifying it:

```powershell
$journal = Get-ChildItem tmp/pb12_diagnostic_20261029_memretry -File -Filter 'training.log.diagnostics.*.jsonl'
./scripts/analyze/summarize_pb12_diagnostic_journal.ps1 -Path $journal.FullName
```

Compare per-component publication sizes and run counts, compaction counts and
bytes, cache counters, time breakdowns, memory against its estimator, and
monitoring overhead. Compare disk samples against the 3x limit. Compaction
merges only one fan-in per call; an observed early reduction does not prove run
growth is bounded. A fresh Stage 9.4 retry would require complete monitor and
test evidence, a separately reviewed workload and authorization, and unchanged
hard stops. No result from this short diagnostic qualifies Stage 9.4 or 9.5.
