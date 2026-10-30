# Benchmarks and reproducibility

## Required context

For a performance or resource claim, identify the metric, units, measured phase, workload, configuration, environment, and source report. Preserve the distinction between throughput, elapsed time, process memory, external working-set observations, and algorithmic storage estimates.

State sample size/repetition, baseline, and uncertainty when those determine interpretation. Do not invent missing measurements. If a result is historical, say so; if conditions differ, do not present the difference as a controlled comparison.

For strategy comparisons, distinguish self-play output from independent cross-play evidence. Statistical significance, confidence intervals, evaluation adapters, and compatible records are not interchangeable.

## Recommended README summary

Give the reader the main bounded observation and link the full report. Include a material limitation beside the summary instead of burying it below a graph. A large benchmark table belongs in a maintained experiment document when it obscures onboarding.

The [PB-9 scaling report](../../../../docs/pb9_multiway_training_scaling_findings.md) is a project measurement source to inspect. Do not turn its fastest observed configuration into a universal worker-count recommendation or omit a different phase's memory peak when discussing resource requirements.

## Example pattern

"In the linked experiment, configuration A had the highest observed throughput for workload B. Hardware, run conditions, and resource caveats are in the report."

This is a wording pattern; A and B must be replaced with verified facts before publication.

## Scope boundary

README work does not authorize rerunning benchmarks, generating solver artifacts, installing tools, or running long experiments. Clearly distinguish source/report inspection from execution and independent reproduction.
