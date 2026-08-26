# Project state

`TexasSolver::texas` is the installed C++17 library target. Stable public
headers are the explicit `TEXASSOLVER_PUBLIC_HEADERS` list in `CMakeLists.txt`.
Stable implementation sources are `TEXASSOLVER_SOURCES`; platform and research
sources are deliberately separate.

Optional build-tree research targets are `TexasSolver::legacy_vector`,
`TexasSolver::hunl_fixed_research`, and `TexasSolver::hunl_flat_mccfr_research`.
They are not installed APIs. Tests require both `BUILD_TESTING` and
`TEXASSOLVER_BUILD_TESTS`.

The multiway runtime contract is [multiway_runtime_contract.md](multiway_runtime_contract.md).
The [Pluribus technical report](pluribus_technical_report.md) is research context,
not evidence of runtime correctness or release readiness.

No build, test, install, benchmark, or solver validation is recorded for the
2026-08-26 CMake remediation work.
