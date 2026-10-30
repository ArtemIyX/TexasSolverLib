<!-- TEMPLATE: A local subsystem guide. Replace {{FIELDS}}, remove optional sections that
do not help, and resolve all links relative to this README's final directory. -->

# {{MODULE_NAME}}

{{MODULE_PURPOSE_AND_INTENDED_READER}}

## Scope and boundaries

<!-- Required: explain local responsibility and public/internal/research/compatibility status
where applicable. Do not infer installed support merely from a header directory. -->

{{RESPONSIBILITIES_AND_NONRESPONSIBILITIES}}

{{PUBLIC_OR_INTERNAL_STATUS_AND_SUPPORTED_ENTRYPOINTS}}

## Integration

{{VERIFIED_PREREQUISITES_AND_CALLER_OR_BUILD_INTEGRATION}}

{{MINIMAL_EXISTING_USAGE_EXAMPLE_OR_SPECIFIC_LINK}}

## Contracts

<!-- Required where callers depend on them: inputs, outputs, ownership, lifecycle, validity,
determinism/resource conditions. Link canonical API contracts instead of duplicating them. -->

{{RELEVANT_CONTRACT_SUMMARY_AND_CANONICAL_REFERENCES}}

<!-- Optional: concise architecture/data-flow diagram with a textual explanation. -->

## Development and validation

{{RELATED_SOURCE_AND_FOCUSED_VALIDATION_LOCATIONS}}

{{KNOWN_LIMITATIONS_AND_ACTUAL_VALIDATION_SCOPE}}

<!-- Validation locations are not a claim that checks ran in the current authoring session. -->

## Related documentation

{{ROOT_README_ARCHITECTURE_AND_API_LINKS}}

<!-- Optional: dependency attribution or scientific background specific to this module. -->
