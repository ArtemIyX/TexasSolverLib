# Audience and reader journeys

Choose the primary journey from the user's request and the target directory. Do not assume every README must serve every audience equally.

| Reader | Questions to answer early | Useful next destination |
| --- | --- | --- |
| Evaluator | What problem is solved? What is implemented and unproven? | Capabilities, limits, evidence |
| C++ consumer | What environment is required? How do I integrate it? | Public example and package instructions |
| Researcher | Which algorithmic ideas are used? What is locally validated? | Technical report and experiment details |
| Contributor | Where is the subsystem? How are changes verified? | Contribution guide and architecture |
| Workflow operator | What inputs, resource limits, and outputs matter? | Workflow README and I/O contract |

## Recommended

Make one preferred next action visible, then give secondary routes. In a root README, short links can serve several readers without embedding every manual. A module README can assume basic project context, but must name its scope and boundaries. An experiment README should prioritize method and reproducibility over product promotion.

## Example

For a library consumer, lead with a verified minimal integration and expected result. Link full solver architecture separately. For a researcher comparing strategies, lead with experiment conditions and limits; a generic package command does not answer the main question.

Use [information architecture](information-architecture.md) to choose placement. The study's [catalog](../../../../docs/research/github-readmes-2026-10-30/CATALOG.md) shows both concise gateways and embedded guides. Those examples support choices; they do not establish which journey performs best for this project's readers.
