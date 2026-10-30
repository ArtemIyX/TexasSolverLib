# Quickstart and requirements

## Required

Verify prerequisites and commands from current definitions. Explain the environment, working directory, relevant inputs, and expected result. Distinguish building a library, consuming an installed package, running a small example, and launching a research workflow.

Do not invent a clone URL, release binary, executable target, package-manager command, or configuration file. If a necessary value is unresolved, find it locally where possible; otherwise identify the missing value rather than presenting a placeholder as a runnable command.

Document submodule prerequisites when relevant. Optional logging, research tools, or diagnostics should not become universal requirements merely because they exist in the repository.

## Recommended

Offer one modest, practical entry path before alternatives. A bounded small-game example may explain the library without requiring a production-sized six-player training run. Link the full workflow and resource requirements rather than launching or prescribing expensive computation as an incidental quickstart check.

For Windows commands, follow the repository's PATH-normalizing wrapper when documenting the agent environment. Explain generator-specific configuration only where it affects the actual instructions. Read [C++ and CMake integration](cpp-cmake-integration.md) before writing configure/build/package examples.

## Success criterion

State what the command creates, prints, or makes available. Describe output semantics without fabricating exact values. An illustrative output must be labeled; an observed result needs provenance.

## Review example

A README says "run the solver" after a build command. Inspect which target is enabled, what input it requires, and which result proves the example completed. Repair those omissions within scope; do not execute the workload without authorization.
