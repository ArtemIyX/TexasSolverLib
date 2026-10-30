# Coverage and declaration rules

## Scope and evidence

A selected public scope has a 100% baseline: every public file, namespace, class, struct, enum, enumerator, free function, method, constructor and destructor must have an associated description, including overloads. Document each non-obvious public constant, alias and macro. Treat a base contract explicitly inherited by an override, or a clearly identified shared overload contract, as coverage for that declaration; verify that inheritance or grouping works when output can be generated. If a declaration is undocumented and no exception applies, coverage is incomplete. Do not manufacture comments to produce a clean count.

Define public from the current supported installed API and user's task, not merely `public:` or a path. Namespaces spanning files need one canonical description, not repeated competing descriptions. A selected header's file documentation should explain its responsibility even if all contents are already documented. Do not assume a private helper, external dependency, generated code, research or compatibility API is included unless the scope says so. Do not silently omit an explicit user-requested item.

Determine facts from actual code and relevant tests/callers. Separate guaranteed contract from current implementation detail. If information is ambiguous, leave that claim out, identify the missing evidence, and report an incomplete contract; do not silently claim 100% semantic completeness. Fix stale comments only within scope, and flag mismatches that would need behavioral changes.

## Contract matrix

| Item | Always document in scope | Include when applicable and established |
| --- | --- | --- |
| File | Purpose and major public contents; `@file` for file association | Intended audience, related modules, portability and key invariants |
| Namespace | Responsibility and public boundary at one canonical location | Stability, separation from internal namespaces |
| Type | Responsibility and how it is used | Ownership, lifetime, invariants, validity, thread safety, synchronization, cost and invalidation |
| Function/method | Purpose, every named parameter and return meaning | Direction, units, bounds, null/empty semantics, failure, exceptions, side effects, ownership, lifetime, concurrency, determinism and cost |
| Constructor | Purpose and all arguments | Initial state, ownership/borrowing, validity, failure and exceptions |
| Destructor | Destruction/cleanup meaning | Blocking, callbacks, exceptions, resources and external lifetimes |
| Enum/enumerator | Enum meaning and each value's semantic meaning | Numeric value stability, serialization and invalid values |
| Member/constant/alias | Meaning when not self-evident | Units, valid range, ownership, mutability, lifetime and stability |
| Macro | Purpose when non-obvious | Arguments, expansion, side effects, conditional availability and safety |

For `void`, no return section is needed; for parameterless functions no parameter section is needed. A named parameter's meaning must be stated for each applicable API, either explicitly or by an unambiguous shared contract. `@tparam` is required when the type's constraints or meaning are not obvious; record actual constraints, not a guessed C++ concept. Include `@throws` only for exceptions established by the implementation or contract, without claiming an exhaustive list unless verified. Explain failures that leave output unchanged or partially modified when observable. Describe `noexcept` implications only when they add useful information; it is not proof of thread safety or allocation freedom.

For numeric APIs specify units and valid bounds when meaningful, including whether endpoints are inclusive, sentinel values, overflow and rounding. Distinguish algorithmic determinism from bit-for-bit portability, randomized seeds, scheduling and floating-point variation. Specify complexity, memory, layout, aliasing, alignment and hot-path allocation guarantees only if supported by the code and intended contract. In TexasSolver document action-major `row[action][bucket]` only for a layout verified to use it, not as a universal claim.

## Declaration-specific rules

- **File:** attach `@file` to a documented file where global entities need it. Describe the role once; do not put a generic license/history block in place of purpose.
- **Namespace:** document it at a canonical declaration. Explain distinction between public namespace and `detail`; do not repeat the same text in every reopening.
- **Class/struct:** state responsibility before methods. Document only established ownership, lifetime, invalidation and thread-safety guarantees. A struct's fields need meaning when names are insufficient; do not duplicate obvious types or defaults.
- **Free functions and members:** place comments immediately before the canonical declaration. Describe parameter meaning, output and return, relevant failures and mutations; document lifetime/nullability for borrowed pointers or references. For `const`, `noexcept`, `[[nodiscard]]` and signatures, describe consequences rather than restating syntax.
- **Constructors:** describe what becomes valid after construction; identify borrowed versus owned arguments, transfer and allocation/failure where known. A self-evident defaulted constructor may have a single concise brief. If explicitly deleted, document the restriction when it affects the public contract rather than implying it can be called.
- **Destructor:** a trivial/defaulted destructor may have a single concise brief, e.g. `Destroys the object.` Describe nontrivial cleanup, callback ordering, blocking and required lifetimes when verified. Do not promise resources were released if error paths contradict that claim. For virtual destructors, explain polymorphic destruction when relevant.
- **Copy/move:** document availability and any material sharing, invalidation, moved-from state or restrictions; do not invent stronger guarantees than standard and implementation provide. Deleted operations can share a short explanation.
- **Overrides:** document the base contract once. An unchanged override may inherit it through Doxygen's `INHERIT_DOCS`, where supported and effective, or explicitly use `@copydoc` for the correct overload. If behavior adds material specifics, retain or link to the base contract and document differences without copying stale text. Doxygen inheritance is configuration-dependent; `@copydoc` needs an unambiguous target. Check rendered output if authorized; otherwise state that rendering was not checked. Do not suggest a derived override can weaken the base's promised preconditions or guarantees.
- **Overloads:** describe each distinct semantics and parameter set. Shared text or `@copydoc` is acceptable only if it applies exactly; explain each difference and avoid ambiguous overload links.
- **Templates:** document type requirements, accepted range and any compile-time constraints or specialization differences. Do not claim all integral types are accepted when operations instantiate narrower requirements.
- **Enums/constants:** describe each value and units or significance. Document numeric stability only when established; do not present incidental enum values as a wire format.
- **Operators/callbacks:** describe non-obvious equality, ordering, ownership, invocation timing, reentrancy and failure semantics. A callback's thread and lifetime must be stated when established.
- **Aliases/macros:** document non-obvious semantic role, constraints and side effects. Skip obvious mechanical aliases or include guards unless the user explicitly requires them. Do not promise single evaluation of macro arguments unless verified.
- **Deprecation:** use `@deprecated` only when actually deprecated, with replacement and migration details if known; do not invent removal dates.

## Pragmas and conditional compilation

Do not add noisy comments to routine `#pragma once`. For each non-obvious diagnostic suppression, provide a nearby ordinary comment identifying compiler, diagnostic and why that exact code needs suppression, and explain the region and restoration if not obvious. Keep push/disable/pop paired and narrow, under appropriate compiler guards. A comment must not claim the warning was fixed or harmless without evidence. Avoid suppressing warnings or changing pragma behavior as part of documentation-only work. Record feature/platform conditions when they materially alter available declarations or behavior.

## Coverage exceptions and completion

Trivial defaulted special members, obvious accessors and self-explanatory operations still receive a short description when part of the selected public baseline. Routine private implementation helpers, obvious include guards, boilerplate `#pragma once` and obvious mechanical aliases/constants can be skipped unless explicitly requested. Non-obvious internals require rationale when relevant to the requested scope. Inherited contracts, documented groupings and unambiguous canonical descriptions avoid duplication; each declaration must remain discoverable. Report omissions, uncertain contracts and unverified Doxygen associations rather than padding comments or claiming complete coverage.
