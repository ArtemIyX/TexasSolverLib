# Documentation generation, automation and sources

This reference is guidance for a separately requested automation task. It does not authorize changing CMake, CI, repository settings, installing tools or publishing a site. Apply `cmake-maintenance` before any CMake-related edit and the relevant project execution restrictions. Verify current provider documentation and exact supported versions before implementation.

## Design and scope

Start with Doxygen-generated HTML and curated Markdown when sufficient. Doxygen parses annotated C++ and supports Markdown pages. Select supported public headers from the current CMake classification and curated guides explicitly. Exclude vendor dependencies, build outputs, generated files and unrelated research by default; prepare a separate explicitly labeled internal view if requested. Set source visibility deliberately because generated pages may expose source or header text.

Sphinx + Breathe consumes Doxygen XML to integrate selected API descriptions with narrative guides. Exhale can generate API hierarchy pages. These add Python dependencies and configuration; use when navigation, cross-referencing or versioned guides justify the complexity. Pin compatible versions and check C++ parsing, macro definitions, platform-dependent declarations, and links against representative output.

Doxygen's `EXTRACT_ALL = YES` treats entities as documented and disables undocumented-member warnings. It is useful for an exploratory inventory, not proof of coverage. With `EXTRACT_ALL = NO`, global items need a documented file or group as appropriate. `WARN_IF_DOC_ERROR`, `WARN_IF_INCOMPLETE_DOC`, `WARN_NO_PARAMDOC`, `WARN_IF_UNDOCUMENTED`, `WARN_IF_UNDOC_ENUM_VAL`, and `WARN_AS_ERROR` have distinct roles and version-dependent availability; check the pinned version's manual. Warnings detect missing or malformed markup, not factual quality. Build a clean small scope first and expand it; do not globally silence warnings to force a pass.

## Future CI/CD behavior

1. On pull requests, build and validate the selected documentation with read-only permissions; upload the generated HTML as a review artifact. A PR build should never publish production documentation.
2. On successful default-branch pushes, deploy that exact built artifact. Release tags may publish separate immutable version paths if explicitly configured; preserve prior versions during deployment. Define which branch or tags may publish and explicitly guard manual runs too.
3. Support `workflow_dispatch` for rebuilding or publishing only from an authorized ref. Serialize production deployments, and choose cancellation behavior so an older deployment cannot replace a newer approved one. Ensure tag and branch deployments do not accidentally erase each other's versioned output.
4. Pin generator, theme, dependencies and reviewed action revisions; use reproducible downloads with integrity checks where practical. Keep build and deploy jobs separate, give the build minimum read permissions, and grant `pages: write` plus `id-token: write` only to the Pages deploy job. Use environment protection and GitHub's artifact/deploy mechanism, or an explicitly chosen alternative.
5. Check generator exit status and selected warning gates; inspect representative class, free function, enum, override and Markdown pages. Check local links and make examples compilable when requested and permitted. Preserve logs and artifacts on failure, and never deploy a failed build. Define how preview artifacts are accessed; an uploaded artifact is not automatically a hosted PR preview.
6. For release documentation, label revision/version and distinguish current development from stable releases. Document retention, redirects and rollback before relying on multi-version deployment. Confirm hosting visibility and platform-specific Pages availability in the target repository.

A local command and CI should consume the same config and inputs. Doxygen CMake integration is optional; if used, avoid making normal compilation depend on optional documentation tools. For a `gh-pages` branch served through Jekyll, consider `.nojekyll` where underscore-prefixed generated paths would otherwise be omitted; do not assume the same requirement for artifact-based custom deployment. Never commit generated HTML to source branches unless the project deliberately chooses that policy.

## Primary references

- [Doxygen documentation blocks](https://www.doxygen.nl/manual/docblocks.html), [special commands](https://www.doxygen.nl/manual/commands.html), [configuration](https://www.doxygen.nl/manual/config.html), [FAQ](https://www.doxygen.nl/manual/faq.html), and [preprocessing](https://www.doxygen.nl/manual/preprocessing.html).
- [CMake FindDoxygen](https://cmake.org/cmake/help/latest/module/FindDoxygen.html).
- [GitHub Pages custom workflows](https://docs.github.com/en/pages/getting-started-with-github-pages/using-custom-workflows-with-github-pages) and [publishing source setup](https://docs.github.com/en/pages/getting-started-with-github-pages/configuring-a-publishing-source-for-your-github-pages-site).
- [Sphinx](https://www.sphinx-doc.org/en/master/), [Breathe](https://breathe.readthedocs.io/), [Exhale](https://exhale.readthedocs.io/), and [Diátaxis documentation types](https://diataxis.fr/).

## Project examples, not current deployment guarantees

- [jsoncpp historical workflow](https://pigweed.googlesource.com/third_party/github/open-source-parsers/jsoncpp/%2B/refs/heads/upstream/master/.github/workflows/docs.yml): separate build/deploy, warning gate and pinned Doxygen checksum; inspect the current upstream version before reuse.
- [libCEC documentation build notes](https://github.com/Pulse-Eight/libcec/blob/master/docs/api/README.md): one site assembled from language-specific generators.
- [LogViewer documentation guide](https://bula173.github.io/LogViewer/d1/d38/md_docs_2BUILDING__DOCUMENTATION.html): PR artifacts and deployment described by the project.
- [C++ library template release deployment commit](https://github.com/donut-engineer/cpp-library-template/commit/29932945c9695fc52cbace430a1353dac5b5b2b4): historical example of documentation deployment on release tags.

These links illustrate approaches at the referenced time; they do not establish current workflow status, a successful run, current site availability or suitability for TexasSolver.
