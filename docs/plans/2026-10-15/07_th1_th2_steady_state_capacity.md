Task: Establish a valid steady-state scaling benchmark and repair per-worker scratch-capacity semantics; Codex can prepare source and measurement changes, but an operator must run the benchmark matrix.

# TH1-TH2 - Steady-state benchmark and capacity semantics

## Goal

Separate graph-growth costs from worker scaling and make scratch capacity mean
the same thing in admission, allocation, and reporting.

## Scope

### TH1 - Benchmark protocol

- Use one untimed graph-growth warmup batch.
- Measure four batches.
- Repeat five times.
- Test 1, 2, 4, and 8 workers.
- Test fixed and dynamic scheduling.
- Separate pinned and unpinned groups.
- Disable checkpoint and blueprint output during batch measurement.
- Keep seeds, workload, model identity, and binary provenance identical.

### TH2 - Capacity contract

- Treat `max_worker_delta_entries` as a per-worker limit.
- Allocate that capacity for every worker.
- Keep preflight as workers multiplied by per-worker capacity.
- Add a separate discovery-capacity limit.
- Report actual allocations in the memory ledger.
- Test dynamic skew where one worker processes more chunks.

## Agent work

- Repair the capacity contract and documentation.
- Add benchmark controls that do not alter strategy identity.
- Add source-level coverage for skew and allocation accounting.

## Operator work

- Build the benchmark targets.
- Run the full matrix and five repeats.
- Provide throughput, worker critical path, tail idle, memory, and correctness
  results.

## Acceptance

- No capacity exhaustion under tested skew.
- Zero discarded trajectories.
- Identical deterministic merge fingerprints.
- Cold and steady-state measurements are reported separately.

## Dependency

AD5-AD6 should be complete before final scaling qualification. Checkpoint and
blueprint output must remain disabled during batch measurements.

## Bounded smoke verification

On 2026-10-23, an unpinned six-run smoke was completed with one batch of 100
trajectories for workers `1`, `2`, and `4` under both fixed and dynamic
scheduling.

- All six runs accepted all `100` trajectories.
- Discarded trajectories, failures, and capacity exhaustion were zero.
- `allocation_probe_valid=true` in every report.
- Configured worker-delta capacity was `1,000,000` per worker and discovery
  capacity was `4,000,000`.
- Worker allocation counts and bytes were reported for every worker.
- All deterministic workload counts, checkpoint bytes (`17,082,301`), and
  merge fingerprints (`10519539386654739548`) matched across all six runs.

This validates the bounded capacity and accounting smoke only. The full TH1
five-repeat pinned/unpinned benchmark protocol remains pending.

## Bounded steady-state smoke

The unpinned steady-state smoke then ran fixed and dynamic scheduling with
workers `1`, `2`, and `4`, one warmup batch, four timed batches, one repeat,
and batch size `1,000`. Checkpoint and blueprint output were disabled by the
benchmark binary.

| Scheduler | 1 worker | 2 workers | 4 workers |
| --- | ---: | ---: | ---: |
| Fixed | `8,384.8` | `15,502.9` | `27,968.3` |
| Dynamic | `8,410.4` | `15,495.5` | `27,538.1` |

Values are median trajectories per second. The analyzer found all six worker
summaries and one common non-zero fingerprint:
`5060116991684512043`. This is a one-repeat throughput smoke, not final
operator qualification.

## Three-repeat unpinned qualification

The same six cases were repeated three times. The analyzer passed all six
groups with three samples per worker count and the same fingerprint in every
sample.

| Scheduler | 1 worker | 2 workers | 4 workers |
| --- | ---: | ---: | ---: |
| Fixed | `8,426.7` | `13,235.4` | `19,648.0` |
| Dynamic | `8,406.5` | `15,316.8` | `22,473.5` |

The values are median trajectories per second across the three benchmark
repeats. Correctness and fingerprint gates passed, but repeat throughput was
not stable enough at 2 workers to justify a code optimization: fixed 2-worker
samples ranged from `10,818.9` to `13,235.4`, and dynamic samples ranged from
`11,790.6` to `15,316.8`. Worker tail-idle time also varied substantially in
the fixed 4-worker samples. Treat this result as host/scheduler variance
evidence, not as a measured implementation bottleneck.

## Host-context and variance instrumentation

The steady-state runner now records per-group host snapshots before and after
each scheduler/placement group, including CPU frequency, load, total CPU,
logical processor count, and benchmark affinity metadata. The analyzer emits
minimum/maximum throughput, standard deviation, coefficient of variation,
relative range, host deltas, and a variance interpretation.

The final instrumented three-repeat run reported complete host context for all
six groups. The CPU stayed at `4,300 MHz`; boundary load changed by at most
one percentage point, and benchmark placement was unrestricted on
`32` logical processors. All six groups classified as `stable` under the
documented thresholds of coefficient of variation below `10%` and relative
range below `20%`. Relative throughput range was `0.28%-13.68%`.

The run still does not identify a stable solver bottleneck. The next formal
qualification remains the pinned/unpinned repeat matrix.

## Pinned versus unpinned bounded comparison

The pinned counterpart completed the same six cases with three repeats. All
groups had complete host context, stable variance classification, and the
common fingerprint `5060116991684512043`.

| Scheduler | Workers | Pinned versus unpinned throughput |
| --- | ---: | ---: |
| Dynamic | 1 | `+0.5%` |
| Dynamic | 2 | `+11.7%` |
| Dynamic | 4 | `+2.8%` |
| Fixed | 1 | `+0.3%` |
| Fixed | 2 | `+3.6%` |
| Fixed | 4 | `-1.7%` |

Pinned host context stayed at `4,300 MHz`, used the benchmark affinity mode
`pinned_logical_processor`, and reported `32` logical processors. Pinning is
therefore a workload- and worker-count-dependent effect, not a universal
throughput improvement. No solver implementation change is selected from
this comparison.

## Formal TH1-TH2 closure

The formal qualification completed with workers `1`, `2`, and `4`, fixed and
dynamic scheduling, pinned and unpinned placement, one warmup batch, four
timed batches, five repeats, and batch size `1,000`. The dedicated analyzer
verified `12` worker summaries and `60` repeat samples.

- Checkpoint and blueprint output were disabled.
- All groups had complete host context and the expected affinity metadata.
- All groups shared fingerprint `5060116991684512043`.
- The capacity smoke previously reported valid allocation accounting and zero
  capacity exhaustion.
- The formal qualification status is complete.

Two dynamic 2-worker groups exceeded the diagnostic repeat-relative-range
threshold (`21.54%` pinned and `21.81%` unpinned). They remain performance
variance review notes, not correctness or capacity failures. No solver
optimization is selected from them.
