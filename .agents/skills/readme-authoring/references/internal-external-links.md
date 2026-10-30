# Internal and external links

## Required

Resolve relative links from the README's containing directory. Moving a root link into a nested module README changes its base. Check file existence, exact case, and renamed headings. Windows accepting a case mismatch does not establish that GitHub or Linux will.

Use repository-relative paths for tracked project documents and images. Do not publish local absolute filesystem paths. Use ordinary HTTPS links for external projects, documentation, papers, and wiki pages. Prefer stable canonical destinations and DOI links for research where available.

Keep the label informative: name the guide and its purpose. A link does not substantiate a claim unless its destination supports that claim. If a destination is a repository landing page rather than a specific manual, label it accordingly.

## Check levels

| Check | Establishes | Does not establish |
| --- | --- | --- |
| Local path inspection | File exists with the expected identity | External availability |
| Source heading inspection | Intended section exists | Every renderer's generated anchor behavior |
| External metadata/source provenance | Destination was previously recorded | Current reachability |
| Authorized live check | Response or visible page at check time | Permanent availability or scientific correctness |

Respect the user's browsing restrictions. Do not resume internet research just to claim all links were live-checked. Identify unverified external availability concisely when it matters.

## Common failure

The research snapshot found the root README referring to `docs/pluribus_technical_report.md`, while the inspected report was `docs/pluribus_solver_technical_report.md`. Treat this as a dated example of why to inspect current files, not a permanent assertion or permission to repair unrelated content.

For citations and publication links, also read [citations and research](citations-research.md).
