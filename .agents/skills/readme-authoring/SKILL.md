---
name: readme-authoring
description: Create, update, restructure, or review TexasSolver project, module, example, and research READMEs with verified technical claims and balanced presentation. Use for README content and layout; use cpp-documentation for declaration-level API documentation.
---

# TexasSolver README Authoring

Make the README a useful entrance for its intended reader. Default to balanced technical presentation: clear purpose, a credible starting path, and links to deeper material, with optional visual identity and demonstrations. Follow current repository guidance and explicit user choices.

## Select the mode

- **Create:** establish audience and file location; select a template; substantiate every project-specific substitution.
- **Update:** change the requested material and any directly affected links or claims. Do not turn a correction into a redesign.
- **Restructure:** preserve accurate content and useful rationale; keep essential onboarding visible; link detailed material to its maintained home. Moving content into another file must stay within the requested scope.
- **Review:** report actionable defects with locations and evidence. Provide suggested wording or patch text when requested, but do not apply edits unless asked. Return no findings when sound.

Read [scope and workflow](references/scope-workflow.md) for substantial work. For a small correction, inspect only the affected content, evidence, and relevant reference. Never load the complete reference library by default.

## Mandatory correctness rules

1. Verify facts against current project files. Names, old READMEs, passing tests, and historical reports do not establish current support or guarantees.
2. Separate implemented behavior, measured results, active development, and planned work. Never infer playing strength, convergence, low exploitability, production readiness, or universal performance from software checks.
3. Where identity matters, describe an independent Pluribus-inspired reconstruction, not the original implementation. Treat published research, external recreations, and local evidence as separate sources.
4. Preserve correct content, attribution, legal information, and scope. Never invent commands, dependencies, deployment URLs, badges, screenshots, measurements, citations, or maintainer channels.
5. Resolve project links and assets relative to the README being edited. Use a specific maintained destination. Distinguish source docs, deployed docs, wiki resources, dependency upstreams, and scientific sources.
6. Keep essential setup and explanations available as text. Presentation must not imply unsupported capabilities or verified results.
7. Documentation work does not authorize configuration, builds, tests, benchmarks, solver jobs, dependency/CI edits, publishing, or commits. Report checks actually performed and unresolved evidence gaps.

## Defaults and supported choices

**Recommended:** establish purpose and audience early; offer one preferred starting path; show a concrete example or result; state relevant limits; provide documentation, support, contribution, and license routes. Adapt length and ordering to the reader. Link long technical details instead of duplicating a manual.

**Optional:** banners, logos, badges, screenshots, GIFs, diagrams, dependency links, wiki links, scientific citations, GitHub alerts, tables, and collapsed sections. Honest active-development notices are welcome. Use them for reader decisions, not as blanket disclaimers. No fixed badge count, word budget, section sequence, or diagram requirement applies.

Honor the current no-emoji project default unless the user overrides it. GitHub alert syntax remains supported; its renderer-provided icon is not a reason to prohibit alerts.

## Read the references needed for the task

| Reference | Read when |
| --- | --- |
| [Scope and workflow](references/scope-workflow.md) | Creating, restructuring, or coordinating an update/review |
| [Project facts](references/project-facts.md) | Making claims about TexasSolver behavior, structure, or support |
| [Audience journeys](references/audience-journeys.md) | Choosing the README's audience and primary next action |
| [Information architecture](references/information-architecture.md) | Ordering, splitting, or deduplicating sections |
| [Headers and navigation](references/headers-navigation.md) | Editing the opening, contents, or jump links |
| [Documentation and wiki](references/documentation-wiki.md) | Linking source guides, API docs, hosted docs, or a wiki |
| [Badges](references/badges.md) | Adding or checking badge meaning and destinations |
| [Images and media](references/images-media.md) | Adding branding, screenshots, GIFs, or recordings |
| [Diagrams](references/diagrams.md) | Explaining architecture or data flow graphically |
| [Alerts and details](references/github-alerts-details.md) | Using NOTE/TIP/IMPORTANT/WARNING/CAUTION or collapsible content |
| [Style and accessibility](references/style-accessibility.md) | Reviewing readability, visual density, themes, or inclusive access |
| [Internal and external links](references/internal-external-links.md) | Adding, moving, or repairing links |
| [Quickstart and requirements](references/quickstart-requirements.md) | Writing prerequisites, installation, or first-run guidance |
| [C++ and CMake integration](references/cpp-cmake-integration.md) | Documenting build flags, public targets, or package consumption |
| [Examples and output](references/examples-output.md) | Selecting runnable examples and explaining success |
| [Dependencies and licenses](references/dependencies-licenses.md) | Explaining third-party packages, submodules, or attribution |
| [Status and roadmap](references/status-roadmap.md) | Describing maturity, active development, or future work |
| [Support, contribution, security](references/support-contributing-security.md) | Defining assistance and reporting routes |
| [Scientific claims](references/scientific-claims.md) | Explaining research relationships or solver guarantees |
| [Benchmarks and reproducibility](references/benchmarks-reproducibility.md) | Reporting performance, resources, or experiment results |
| [Citations and research](references/citations-research.md) | Adding papers, secondary explanations, or bibliography entries |
| [Maintenance and review](references/maintenance-review.md) | Completing an edit or carrying out a review |
| [Research patterns and gallery](references/research-patterns-gallery.md) | Choosing examples from the 114-repository study |
| [Template selection](references/template-selection.md) | Creating a README or choosing a substantial rewrite structure |

The [template guide](references/template-selection.md) links all five output templates. Templates are adaptable scaffolds, not instructions to populate every section. Replace or omit substitution fields before delivering an authored README.

## Finish

Inspect the scoped diff and verify local paths, anchors, commands against their definitions, and the support for changed claims. Rendering, external-link checks, and execution are separate checks; do not imply they happened when only source was inspected. Use the project log skill after completed edits; a review alone does not authorize changing the reviewed files. Keep the completion report concise.
