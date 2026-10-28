# PB-9 Multiway Training Scaling Findings

**Date:** 2026-10-25
**Branch:** `pb-9`
**Workload:** RelWithDebInfo, normal binary, fixed scheduler, 50 batches, 1,000 trajectories per batch, checkpointing enabled, unpinned workers.

## Results

| Workers | Trajectories/sec | Wall seconds | Wrapper peak working set | Checkpoint seconds | Payload hash |
| ---: | ---: | ---: | ---: | ---: | ---: |
| 2 | 678.893 | 73.65 | 25,289,744,384 | 47.46 | `6194587233657942608` |
| 4 | 878.833 | 56.89 | 25,292,238,848 | 46.77 | `3181775162114814513` |
| 8 | 1,039.930 | 48.08 | 25,300,115,456 | 46.48 | `5946562739452589295` |
| 16 | 1,002.080 | 49.90 | 25,306,861,568 | 51.08 | `1664139651383748648` |

All runs completed 50,000/50,000 trajectories with `failed=false`. The 8-worker run was the fastest observed. Increasing to 16 workers reduced throughput and changed the payload hash, so the 16-worker result is not interchangeable with the 8-worker reference.

## Memory findings

- The wrapper observed a nearly constant peak of approximately 25.3 GB across all worker counts.
- Training reached approximately 17.9 GB before checkpointing.
- The `after_checkpoint` snapshots were approximately 25.14-25.15 GB for every worker count.
- Memory dropped to approximately 18.78 GB before policy export.
- The post-training interval is therefore dominated by checkpoint writing and cleanup, not by policy export. Checkpoint writes took approximately 46-51 seconds.
- The wrapper high-water mark is the relevant safety value. The report's ordinary peak working-set field remained around 18.7 GB because it did not capture the same checkpoint transient.

## Guard and publication results

- Checkpoint size was 4,576,234,700 bytes in every run.
- Policy output size was 3,921,243,000 bytes.
- The 8-worker reference payload hash was `5946562739452589295`.
- Process and system export gates passed, and publication mode was `rename`.
- The system guard remained within the configured 1.5 GiB reserve on a 66,156,724,224-byte machine. It protects system-wide availability; it does not enforce the separate 20 GiB process qualification target.

## Interpretation

The useful scaling range for this workload is 2 through 8 workers, with diminishing returns after 8. Sixteen workers add memory pressure and coordinator/merge contention without improving end-to-end throughput. The checkpoint phase remains the dominant post-training memory event even with the bounded schema-5 writer.

Any transient 20k+ speed displayed by interval or worker-local progress telemetry is not comparable to the report's end-to-end `trajectories_per_second`. The report value includes the complete 50-batch training interval and is the metric used here.

## Follow-up

No implementation change is included in this finding. Before using 16 workers for qualification, investigate the payload-hash difference and the throughput regression. Keep the 8-worker configuration as the current qualification reference.
