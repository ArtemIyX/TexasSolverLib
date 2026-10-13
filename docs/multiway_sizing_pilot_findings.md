# Multiway F1 sizing pilot findings

Updated: 2026-10-11

## Purpose

This report records the first bounded sizing measurements for the six-player
multiway F1 workflow. The goal was to determine whether the current sampled
blueprint trainer can retain enough public states, sparse rows, and compact
storage for the planned 50,000,000 accepted trajectories.

These runs are sizing evidence only. They are not F1 training or playing
strength qualification.

## Bucket artifact qualification

The existing 15.37 GB bucket artifact was inspected with the current F1
configuration. The inspection report recorded:

- 22,100 flop tables
- 270,725 turn tables
- 2,598,960 river tables
- 3,140,843,160 live assignments
- 16 effective inspection threads
- `identity_matches=true`
- `payload_hash_matches=true`

The artifact can therefore be reused for the current model identity. The
adjacent historical `training.conf` contains different sizing metadata and is
not the source of truth for this conclusion.

## Pilot configurations and outcomes

### Initial 100-batch run

The checked-in sizing capacities were too small:

- `maximum_public_states=100000`
- `maximum_sparse_rows=500000`
- `maximum_sparse_values=2000000`
- `worker_delta_capacity=100000`

The run failed with `compact storage capacity exceeded` during the first
training interval. The memory preflight itself passed.

### Expanded 100-batch run

A temporary sizing configuration increased the limits to 1,000,000 public
states, 5,000,000 sparse rows, 50,000,000 sparse values, and 1,000,000 worker
delta entries. The run still failed with `compact storage capacity exceeded`.

The failure report recorded:

- `peak_public_states=979,329` of 1,000,000
- `peak_sparse_rows=390,843` of 5,000,000
- `peak_sparse_values=49,517,611` of 50,000,000
- `peak_compact_storage_bytes=800,000,000`
- `capacity_exhaustion_stage=0`

Sparse values were the immediate exhausted limit. Public-state capacity was
also nearly exhausted. The zero stage value means the exception occurred
before the trainer committed a detailed capacity stage to telemetry.

### One-batch diagnostic run

A temporary configuration doubled public-state capacity to 2,000,000 and
sparse-value capacity to 100,000,000. One batch completed successfully:

- 1,000 trajectories
- 0 discarded trajectories
- 22,995 public states
- 10,902 sparse rows
- 2,361,673 sparse values
- 1,400,000,000 compact-storage bytes reported
- 49,309,924-byte checkpoint
- 13,320,256,768-byte memory preflight estimate

### Ten-batch diagnostic run

The same expanded configuration completed ten batches:

- 10,000 trajectories
- 10,000 accepted trajectories
- 0 discarded trajectories
- 95.764 trajectories per second
- 18,855,165,952-byte peak process RSS, about 18.86 GB
- 155,674 public states
- 65,623 sparse rows
- 11,594,145 sparse values
- 1,201 peak worker-delta entries
- 1,400,000,000 compact-storage bytes reported
- 263,715,085-byte checkpoint
- Worker trajectory distribution: 62 to 63 per worker

## Interpretation

The trainer retains a rapidly growing shared set of public states and sparse
rows across batches. Increasing static capacities allows a larger pilot to
run, but it does not solve the growth mechanism. The 10,000-trajectory run
already consumed about 18.86 GB of resident memory.

A simple linear projection from 10,000 to 50,000,000 trajectories is a factor
of 5,000. It would imply roughly 778 million public states, 328 million sparse
rows, 58 billion sparse values, and about 94 TB of resident memory. This is an
extrapolation, not an acceptance measurement, but it is sufficient to reject
the current storage design for the planned F1 run.

The reported `peak_compact_storage_bytes` remains 1.4 GB while the sparse-value
count changes substantially. It should be treated as an allocator/capacity
telemetry field until its accounting is clarified. Peak RSS and the public
state, row, and value counters are the primary sizing signals.

## Decision and next action

Do not start the 50,000,000-trajectory F1 qualification run. Do not freeze the
current capacity values as production limits.

The next implementation step is to bound retained traversal state. The planned
immutable batch-epoch design, or an equivalent state compaction and release
mechanism, should be implemented and measured. After that change, repeat the
1-, 10-, and 100-batch pilots and compare:

- peak RSS and compact-storage accounting;
- public-state, row, value, and worker-delta growth;
- throughput and lock/coordinator wait time;
- deterministic training and checkpoint fingerprints.

Only after those measurements fit the machine budget should the F1 capacity
fields be frozen and the 50-million-trajectory qualification resumed.

## Evidence files

- `artifacts/f1_sizing_v1/buckets/inspection_f1_12x12x12.json`
- `artifacts/f1_sizing_v1/sizing_pilot_report.json`
- `artifacts/f1_sizing_v1/sizing_pilot_v2_report.json`
- `artifacts/f1_sizing_v1/batch1_report.json`
- `artifacts/f1_sizing_v1/batch10_report.json`

## Limitations

- The reports identify resource growth but do not qualify strategy strength.
- The 50-million projection assumes linear growth and must not be treated as a
  measured production result.
- Producer provenance remains `unknown` in the bucket evidence header.
- No build, test, or solver run was performed while preparing this document.
