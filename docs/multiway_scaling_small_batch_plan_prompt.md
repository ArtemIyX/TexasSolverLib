# Prompt: Multiway Worker Scaling Optimization Plan

Create an implementation plan for diagnosing and improving multiway worker scaling.

Current problem:

- Pinned 3,000-trajectory results: 1 worker 96.08, 2 workers 92.85, 4 workers 95.79, 8 workers 92.88, 16 workers 70.94 trajectories/second.
- Unpinned results: 1 worker 93.87, 2 workers 91.98, 4 workers 80.09, 8 workers 77.18, 16 workers 59.76 trajectories/second.
- All runs completed successfully with the same deterministic merge fingerprint `2342594177256522980`.
- Barrier wait is negligible. Worker counts are balanced, but higher worker counts are slower.
- Peak RSS is approximately 19.7 GB. WPR profiling adds too much CPU, RAM, and disk overhead for benchmark measurements.

Plan requirements:

1. Add a PowerShell `-TrajectoriesPerBatch` option so smoke tests can use three small batches of 100-250 trajectories instead of 1,000.
2. Compare the current fixed worker partition with a deterministic dynamic chunk scheduler using pinned workers 4 and 8 only.
3. Preserve trajectory IDs, per-trajectory seeds, deterministic delta ordering, and matching merge fingerprints.
4. Define the measurements and acceptance criteria for deciding between workload imbalance, memory/cache contention, and synchronization overhead.
5. Do not run 10,000+ trajectory tests, use WPR for benchmark numbers, or make speculative architectural changes before the focused comparison.

Inspect the existing runner, worker scheduling, telemetry, deterministic merge tests, and PowerShell scripts. Return a small, ordered implementation and verification plan with files to change and commands to run.
