# C++ and CMake integration

## Required evidence

Inspect the current [root CMake definition](../../../../CMakeLists.txt), relevant target registration, and package/export templates before documenting requirements or consumer integration. A build-tree alias is not automatically an exported package name.

Distinguish the installed public library from opt-in research, compatibility, diagnostic, application, and test targets. Verify option defaults and dependencies between options. The presence of C++ files in a directory does not establish that the default build compiles them.

## Command guidance

- A single-configuration generator uses its configured build type; a multi-configuration generator selects configuration at build/test time.
- Document only the flags needed for the intended workflow. Link the full option reference if one is maintained.
- For an installed consumer example, verify the package name, imported target, installation prefix expectations, and an actual public header/example.
- Do not claim platform/toolchain support from generic CMake syntax alone. Distinguish intended support from checked configurations.

The [small public example](../../../../examples/solve_kuhn.cpp) is a discovery starting point, not proof it ran in the current session.

## Scope boundary

Reading CMake is appropriate for README evidence. Editing any CMake file requires the cmake-maintenance skill and authorized scope. README work does not authorize configuration, builds, tests, installs, or dependency resolution. If execution was not requested, perform source-level consistency checks and state that limit.

## Common mistake

Copying every research option into a minimal consumer quickstart makes optional internal targets appear to be required dependencies. Keep the public entry path accurate and move advanced variants to a linked guide when appropriate.
