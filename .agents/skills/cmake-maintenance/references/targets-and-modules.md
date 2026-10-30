# Targets and modules

## Source ownership and layout

Use a root CMakeLists.txt for project setup, options, and ordered `add_subdirectory()` calls. Put module declarations near their owning source when this simplifies maintenance. Shared helpers belong in `cmake/`; prefer functions for local variable scope. Avoid macros that unintentionally overwrite caller variables.

Directory boundaries and library boundaries are different decisions. Moving source lists to another file need not create new libraries. Create a separate target when it has a meaningful dependency, reuse, compilation, or installation contract. Avoid circular target dependencies and libraries introduced solely to shorten a file.

Prefer explicit source lists for production code. CMake discourages GLOB source discovery, including CONFIGURE_DEPENDS due to generator limitations and rebuild overhead. Preserve an existing discovery scheme for a focused unrelated change; do not migrate all tests opportunistically. With existing auto-discovery, verify that adding a source will not register it twice.

## Visibility decisions

| Scope | Used to build target | Usage requirements exposed to consumers |
|---|---|---|
| PRIVATE | Yes | No |
| PUBLIC | Yes | Yes |
| INTERFACE | No | Yes |

Use PUBLIC when public headers or consumer compilation require the dependency. Use PRIVATE for implementation requirements. Use INTERFACE for header-only contracts and interface configuration targets. PRIVATE does not mean a static library's dependency disappears from the final link; account for link-only dependencies when exporting packages.

Namespaced aliases make intended APIs explicit. Aliases are not separate installable targets: install/export the real target and configure its exported name/namespace.

## C++17 target example

Illustrative standalone module; adapt names and files. It assumes the root already called `project()` and loaded `GNUInstallDirs`.

```cmake
add_library(example_core STATIC core.cpp)
add_library(Example::core ALIAS example_core)
set_target_properties(example_core PROPERTIES
    EXPORT_NAME core
    CXX_EXTENSIONS OFF)
target_compile_features(example_core PUBLIC cxx_std_17)
target_include_directories(example_core PUBLIC
    "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
    "$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>")
find_package(Threads REQUIRED)
target_link_libraries(example_core PRIVATE Threads::Threads)
if(MSVC)
    target_compile_options(example_core PRIVATE /W4)
elseif(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(example_core PRIVATE -Wall -Wextra -Wpedantic)
endif()
```

Do not propagate developer warning policy or Werror to consumers. Do not globally enable native CPU flags, fast-math, LTO, sanitizers, or OpenMP as incidental cleanup. Solver numerical behavior, determinism, ABI, portability, and runtime dependencies can change. Add such behavior only within the requested scope and supported compiler configurations.

## Options and parent projects

Namespace project options to avoid collisions with dependencies. Do not FORCE cache values merely to make configuration pass. Preserve caller-supplied settings. When adapting a dependency with generic options such as BUILD_TESTS, understand normal-variable/cache scoping and policies before changing its options; ensure its tests do not leak into the parent build.

Prefer an explicit error for invalid option combinations over silently enabling substantial features. Do not replace existing project-specific test switches with BUILD_TESTING without accounting for scripts and CI consumers.

## Generated files

Generate into the binary tree, not source directories. Declare OUTPUT, DEPENDS, and necessary BYPRODUCTS with `add_custom_command`; use VERBATIM for arguments and attach generated outputs to the consuming target. Avoid configure-time `execute_process` for work that must rerun when build inputs change. For host tools during cross-compilation, distinguish host executables from target executables.

References: [buildsystem](https://cmake.org/cmake/help/latest/manual/cmake-buildsystem.7.html), [compile features](https://cmake.org/cmake/help/latest/manual/cmake-compile-features.7.html), [source glob limitations](https://cmake.org/cmake/help/latest/command/file.html#filesystem), [custom commands](https://cmake.org/cmake/help/latest/command/add_custom_command.html), [option policy CMP0077](https://cmake.org/cmake/help/latest/policy/CMP0077.html).
