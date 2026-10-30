# GitHub alerts and collapsed sections

## Alert selection

| Alert | Use when | Avoid |
| --- | --- | --- |
| NOTE | Relevant contextual information helps interpretation | Repeating ordinary prose in every section |
| TIP | An optional technique improves an already valid workflow | Hiding a required prerequisite |
| IMPORTANT | A condition is necessary for correct use | Treating every design preference as mandatory |
| WARNING | A specific action can cause a concrete adverse result | Vague claims that all research software is dangerous |
| CAUTION | A severe, concrete consequence requires exceptional attention | Using severity to decorate routine status |

Use GitHub blockquote syntax with the alert marker on its own quoted line. Keep the explanation specific and close to the affected instruction.

```markdown
> [!NOTE]
> This directory describes an experimental workflow. See the linked report for its validation scope.
```

This is example wording; substitute a verified status and real link when authoring. Renderer-provided alert icons are compatible with the project's no-emoji prose default.

## Collapsed details

Use `<details>` for optional platform variants, extended explanations, large output samples, or troubleshooting. Give `<summary>` a descriptive label and blank lines around Markdown content.

```markdown
<details>
<summary>Optional troubleshooting context</summary>

Explain the specific condition and recovery route here.

</details>
```

Keep the primary starting path and essential warnings visible. Do not make users expand several panels to discover a required dependency. Avoid nesting alerts inside complex HTML layouts when simple prose is clearer.

[GitHub's collapsed-section reference](https://docs.github.com/en/get-started/writing-on-github/working-with-advanced-formatting/organizing-information-with-collapsed-sections) documents the underlying syntax. Check any newly introduced advanced formatting in the actual renderer when available, and report if rendering was not checked.
