# Dependencies, upstreams, and licensing

## Required

Separate required library dependencies, optional application dependencies, development tools, vendored/submodule code, and external runtime assets. Check current build conditions and manifests rather than copying the existing README's dependency list uncritically.

Give upstream project links and explain the dependency's role when useful. Link the applicable third-party license without implying it is replaced by TexasSolver's license. Do not guess permissive terms from a dependency's popularity or badge.

The [.gitmodules file](../../../../.gitmodules) records the evaluator upstream. The [project license](../../../../LICENSE) and [evaluator license](../../../../external/pokerHandEvaluator/LICENSE) are actual legal sources to inspect. [HenryRLee/PokerHandEvaluator](https://github.com/HenryRLee/PokerHandEvaluator) is an upstream resource, not another name for TexasSolver.

## Recommended presentation

| Dependency/tool | Role | Required when | Upstream/terms |
| --- | --- | --- | --- |
| Verified name | Concrete subsystem purpose | Verified build/runtime condition | Real project and license links |

The row above is a schema example, not an actual dependency declaration. Use a short list when a table adds no clarity.

## Common mistakes

- Requiring optional `spdlog` or `tomlplusplus` application support for every library consumer without checking current configuration.
- Promising that a submodule is populated by an ordinary clone.
- Changing vendored code or dependency versions to make README instructions easier.
- Using technology logos as a substitute for version, role, and optionality information.

Dependency updates and vendored changes require their own authorized task. A README edit should describe the current boundary faithfully.
