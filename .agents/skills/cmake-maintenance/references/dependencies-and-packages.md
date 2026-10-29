# Dependencies and packages

## Select an integration method

| Method | Use when | Important constraint |
|---|---|---|
| find_package | Dependency is installed or supplied by a package manager | Prefer package-provided imported targets; CONFIG is appropriate when the package ships a config file |
| add_subdirectory | Vendored source belongs in the current target graph | Control dependency options without leaking global state |
| FetchContent | Source must be populated during configuration | Configuration may access the network; pin an immutable revision or archive hash |
| ExternalProject_Add | Dependency requires a separate configure/build/install | Internal targets are not directly available to the parent; model artifacts and ordering explicitly |

Use `find_package(OpenSSL REQUIRED)` with `OpenSSL::SSL` and `OpenSSL::Crypto`, or `find_package(Threads REQUIRED)` with `Threads::Threads`, rather than guessing library filenames and flags. Do not assume every package supports CONFIG mode or identical target names: inspect its contract.

Avoid hardcoded SDK paths, link_directories(), manual .lib/.a selection, and -lpthread when an imported target exists. Imported targets can carry includes, definitions, configuration-specific locations, and transitive link requirements. Do not create a fake INTERFACE target to conceal a missing required package.

Do not add configure-time downloads to an offline/vendor workflow without task authorization. For reproducible FetchContent use a full commit hash or an archive URL plus URL_HASH. Do not use a moving branch as a reproducibility pin. Inspect package-manager/toolchain integration before adding another dependency mechanism.

## Export and installation rules

Use GNUInstallDirs for installation destinations. Keep build-tree include paths in BUILD_INTERFACE and relative installation include paths in INSTALL_INTERFACE. Export real targets under the public namespace. Do not embed source/build paths, machine-specific dependency paths, or absolute installation-prefix paths into a relocatable package.

Use `configure_package_config_file()` and `write_basic_package_version_file()` with a compatibility policy appropriate to the API. Choose exact, same-major, or other compatibility deliberately; do not promise ABI stability without evidence.

Rediscover external targets required by an exported target before including its targets file:

```cmake
# ExampleConfig.cmake.in
@PACKAGE_INIT@
include(CMakeFindDependencyMacro)
find_dependency(Threads)
include("${CMAKE_CURRENT_LIST_DIR}/ExampleTargets.cmake")
check_required_components(Example)
```

The template assumes the export file and package configuration are installed together. Add find_dependency calls only for dependencies actually present in the exported contract. PRIVATE static dependencies may still require discovery. If a vendored dependency target is referenced by an exported static library, determine whether it must also be exported or found as a separate installed package. Do not solve an export error by hiding a real final-link requirement.

For shared libraries, consider Windows DLL exports/imports, runtime deployment, and platform RPATH/install-name behavior when relevant. Do not impose Linux loader flags on Windows or macOS. Do not modify global search paths to hide deployment errors.

## Consumer verification when authorized

Build-tree success does not prove package correctness. Install to a temporary prefix, create a separate consumer using only find_package and the public target, then compile/link it. Relocate the prefix and repeat discovery where relocatability is claimed. Check optional components separately. Treat installation into system directories as a separate action requiring appropriate authorization.

References: [dependency guide](https://cmake.org/cmake/help/latest/guide/using-dependencies/index.html), [find_package](https://cmake.org/cmake/help/latest/command/find_package.html), [FetchContent](https://cmake.org/cmake/help/latest/module/FetchContent.html), [ExternalProject](https://cmake.org/cmake/help/latest/module/ExternalProject.html), [import/export guide](https://cmake.org/cmake/help/latest/guide/importing-exporting/index.html), [package helpers](https://cmake.org/cmake/help/latest/module/CMakePackageConfigHelpers.html), [install directories](https://cmake.org/cmake/help/latest/module/GNUInstallDirs.html).
