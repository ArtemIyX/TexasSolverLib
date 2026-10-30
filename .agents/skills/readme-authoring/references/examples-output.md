# Examples and output

## Select a useful example

Prefer an existing, small, relevant example over an invented snippet. Identify whether it demonstrates the public library, an internal research workflow, or a diagnostic path. Verify the target and input contract before documenting invocation.

Explain the example's purpose, required inputs, success indication, and next step. A command without context or a screenshot without text leaves users guessing.

## Output labels

| Kind | Required treatment |
| --- | --- |
| Observed result | Link or identify the run/report and configuration |
| Illustrative output | Explicitly label as illustrative; avoid fabricated precision |
| Expected invariant | Substantiate from behavior or a contract; avoid exact values without evidence |
| Historical result | Date and scope it; do not present it as a current rerun |

Keep machine output distinct from prose. Do not edit output to look better while presenting it as an actual capture. Redact private paths or secrets when relevant and disclose meaningful redactions.

## Tips

Use a short excerpt and link full output when logs are large. Explain units, output artifact names, and whether results vary with configuration or randomness. For deterministic behavior, specify the documented conditions rather than promising identical behavior across all environments.

## Example wording

"This example demonstrates a small-game solving API; consult the example source for the printed fields." This is suitable only after checking the actual example. "The six-player solver reaches superhuman strength" cannot be inferred from the example completing.

Do not run examples, tests, or solver jobs merely to fill a README output block. Follow the user's execution authorization.
