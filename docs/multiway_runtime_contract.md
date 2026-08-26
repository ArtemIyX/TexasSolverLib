# Multiway runtime contract

`texas::solver::multiway::MultiwayResolver` accepts structured public state,
hero and opponent ranges, a model identity, and resolver configuration. It
returns a legal root policy plus diagnostics. Artifact identity must cover the
action abstraction and terminal-model hashes. Snapshots own persisted policy
data; resolver requests and diagnostics are caller-owned values.

Release-default search requires a verified and full blueprint, bucket model,
valid leaf evaluator, eligible postflop state, and admitted memory budget.
Otherwise the resolver reports its fallback provenance. `FallbackOnly` and
`ForcedFallback` are explicit rollback modes. `SearchShadow` does not replace
the published fallback; `SearchActive` may publish a completed normalized root.

## Contextual sizing decision

Release roots remain on compatibility sizing. Contextual sizing is not selected
until a versioned artifact contract exists. A future contextual request must
provide acting position, preflop situation, SPR, public-state identity, and
reroot history. Those fields must participate in artifact identity and be passed
unchanged through root-menu creation, translation, expansion, and rerooting.
Compatibility sizing remains the rollback mode for every contextual artifact.

Validation status: static inspection only. Solver quality, convergence,
latency, memory peak, package installation, and cross-platform behavior remain
unverified.
