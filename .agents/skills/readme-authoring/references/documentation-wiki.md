# Documentation, hosted reference, and wiki

Linking to other project documents is encouraged when it keeps the README useful and the details maintained in one place.

## Required distinctions

| Destination | Appropriate description | Verify |
| --- | --- | --- |
| Repository Markdown guide | Source guide or technical report | Actual path, current title, relative link |
| Generated API reference | API documentation | Public versus internal coverage and version |
| Hosted documentation | Published documentation | Actual deployment URL and availability evidence |
| Wiki | Project wiki or a specific wiki article | Wiki exists and article is relevant |
| External explanation | Background/resource | Authorship and authority for the associated claim |

The [Doxygen configuration](../../../../Doxyfile) and [documentation workflow](../../../../.github/workflows/docs.yml) are evidence of generation/publication configuration. Their presence alone does not establish a live URL, a successful deployment, or that every generated declaration is a supported public API.

## Recommended

Summarize what a reader will find at the destination. Link the exact guide or section when stable. Keep important version or maturity context beside the link. Prefer version-controlled project documentation for contracts that must change with code; a wiki is suitable for maintained community material when one actually exists.

## Examples

Useful label: "Technical architecture and reconstruction assumptions." Weak label: "More." Never guess a GitHub Pages address from the repository name or create a wiki route just because GitHub supports wikis.

If an existing README points to a missing guide, locate the real document rather than creating an empty replacement to satisfy the link. Correct only the in-scope route and report any unresolved ambiguity.
