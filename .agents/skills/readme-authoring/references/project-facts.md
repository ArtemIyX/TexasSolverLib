# Verify project facts

## Evidence map

Paths below are relative to the repository root, not to this reference. They are discovery locations, not permanent assertions about support.

| Claim | Inspect |
| --- | --- |
| Build minimums, flags, aliases, public/install boundaries | Root CMakeLists.txt and relevant CMake modules |
| Dependency identity and optionality | .gitmodules, root CMakeLists.txt, package manifests, upstream license files |
| Executable name, arguments, expected outputs | Relevant examples CMake registration, parser implementation, application/script I/O guide |
| API behavior and constraints | Declaration, implementation, callers, focused contract tests |
| Implementation milestones | Progress log and current implementation, with dates |
| Measured performance and memory | Experiment report, raw measurement provenance, configuration |
| Documentation generation versus publication | Doxyfile and the actual documentation workflow |

Current useful starting points include the [root build definition](../../../../CMakeLists.txt), [I/O guide](../../../../docs/application_and_script_io.md), and [progress log](../../../../docs/PLURIBUS_LOG.md). Reinspect them when relevant; this skill is not a frozen specification.

## Required distinctions

- A header under `include/` is not automatically an installed, supported public API.
- A CMake option existing does not establish that every combination or platform works.
- A workflow file establishes configured automation, not its latest successful run.
- An example existing does not establish that its commands have been executed in this session.
- A passing contract test does not establish strategic playing strength.

## Example

Before saying an application-only logging package is required to consume the library, inspect the conditional dependency and exported target boundary. Do not promote an optional development dependency into a universal prerequisite.

When evidence is missing, omit an unnecessary claim or state the specific uncertainty. Do not add speculative warnings throughout the document.
