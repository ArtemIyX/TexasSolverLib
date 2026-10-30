# Badges

Badges are supported for useful metadata, navigation, and optional project identity. They are not mandatory and do not independently establish quality.

## Required checks

- Identify what each badge represents and where its click leads.
- For CI, verify repository identity, workflow filename, branch/filter intent, and link destination against current definitions.
- For package/release badges, verify that the distribution or release channel exists. A CMake project version is not proof of a published package.
- For licensing, use the actual project terms; do not obscure third-party terms.
- Never manufacture passing status, coverage, downloads, certifications, or release availability with a static label.
- Distinguish badge-shaped links such as documentation or source navigation from measured status.

The [local CI workflow](../../../../.github/workflows/ci.yml) is the starting point for a TexasSolver CI badge. It does not prove the latest run passed. An unverified generated badge URL should not be presented as checked.

## Recommended

Choose badges that answer the reader's questions. Keep appearance consistent where practical. Link badges to evidence, a release, license, or useful action. Put detailed platform matrices or status inventories in linked documentation when they dominate the opening.

No fixed badge count applies. The source study found large outliers, including navigation buttons and repeated CI matrices; their counts are not a target.

## Examples and common mistakes

[TensorFlow](https://github.com/tensorflow/tensorflow#readme) illustrates multiple technical signals. [Spark](https://github.com/apache/spark#readme) illustrates a large CI matrix. Neither justifies adding equivalent badges without matching project evidence. Do not add a broken package badge merely to imitate a polished repository.
