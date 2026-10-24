# Task L: Staged large-F1 qualification

## Trigger

`Implement the next tracker action: staged large-F1 qualification with hard RAM safety gates.`

## Objective

Add deterministic staged qualification for 50,000, 500,000, 5,000,000, and explicitly gated 50,000,000 trajectories. Reject unsafe stages before execution and abort cleanly if the external process working set approaches the configured hard gate.

## Constraints

- Preserve Task E accounting and Task G capacities.
- Preserve checkpoint/export mutual exclusion.
- Preserve deterministic fingerprints and transactional failed-batch behavior.
- Do not run the 50M stage without explicit user approval.

## Memory qualification context

- The approximately 21 GB final private estimate is conservative accounting, not observed RAM.
- The bounded Task K runs passed with zero discards and matching deterministic fingerprints.
- A 50M-trajectory run still requires staged qualification and explicit approval.
- Task E corrects memory accounting categories; it is not a RAM optimization.
- The default process working-set gate is approximately 48 GiB on a 64 GiB host. The legacy 64 GiB process limit remains configurable.

## Acceptance

- Focused tests cover stage progression, gates, saturation, compatibility, zero-discard success, determinism, and transactional failure.
- Focused and full builds/tests, analyzer self-test, `git diff --check`, and independent C++ review pass.
- Documentation states that the 21 GB value is a conservative private estimate, bounded K passed, Task E corrects accounting rather than RAM, and 50M remains staged.
- The project log records actual validation and the remaining 50M limitation.
