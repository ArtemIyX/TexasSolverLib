# Tracy upstream record

- Version: `0.14.2`
- Upstream project: <https://github.com/wolfpld/tracy>
- Imported source path: `external/tracy`
- Imported source version: `public/common/TracyVersion.hpp`
- Import date: `2026-10-14`
- Functional source changes: none
- License: BSD-3-Clause, retained in `external/tracy/LICENSE`

The imported tree is the complete Tracy source distribution used by the
diagnostic client and capture utilities. The TexasSolver build enables only
the client library through the private `texas_tracy` target. Tracy is not
installed or exported with `TexasSolver::texas`.

Capture files must be produced and opened with the matching Tracy `0.14.2`
viewer/capture tools. Do not mix trace files between incompatible Tracy
versions.
