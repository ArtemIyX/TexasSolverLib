# Scientific claims and algorithm identity

## Required evidence classes

1. **Original research:** claims supported by the original paper or supplement, within its assumptions.
2. **Local implementation:** what this checkout implements, including deviations and engineering choices.
3. **Local validation:** what identified tests or experiments actually establish.
4. **Other recreations:** externally reported behavior, attributed to that implementation.
5. **Proposals:** intended or hypothetical work, explicitly labeled.

Do not merge these classes into a single statement of equivalence. "Inspired by" and "reconstructs public ideas from" do not imply algorithmic identity or reproduction of published results.

TexasSolver's [technical report](../../../../docs/pluribus_solver_technical_report.md) distinguishes original research, other papers, recreations, and proposals. Use it as a navigation aid and inspect relevant evidence. A statement appearing in that report is not automatically a proven property of current code.

## Claims requiring particular care

No software-test count or deterministic replay check establishes superhuman play, equilibrium convergence, low exploitability, profitable play, or superiority to an independent agent. Small-game exact results do not transfer automatically to sampled multiplayer solving. Two-player zero-sum guarantees must not be generalized silently to six-player settings.

## Before and after

Unsupported: "Contract tests prove the six-player solver converges."

Supported form when evidence matches: "Contract tests cover the specified software behavior. Six-player convergence and playing strength have not been established by those checks."

Use the minimum qualification needed for correctness. Do not remove real scientific motivation or replace useful explanation with generic warnings.

For bibliographic handling, read [citations and research](citations-research.md); for measurements, read [benchmarks and reproducibility](benchmarks-reproducibility.md).
