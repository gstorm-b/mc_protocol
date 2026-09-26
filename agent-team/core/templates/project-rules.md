# Project Rules — <project name>

> TEMPLATE. Copy to `agent-team/project/rules.md` and fill in. These are HARD
> rules: every role obeys them, and the reviewer checks them mechanically.
> Where a rule here contradicts `agent-team/core/`, this file wins.

## Version control

- Commit identity: <name> `<email>`
- Attribution trailers (Co-Authored-By etc.): <allowed / forbidden>
- Commit cadence: <e.g. only at Gate 2, only when the human says "commit">
- Branching: <e.g. work on main / feature branches / never push without asking>

## Access boundaries

- <e.g. never read or write outside <project root> without asking the human>
- <secrets, credentials, external services — what is off limits>

## Hardware / environment gates  *(delete section if none)*

- <Anything that must wait for the human to prepare: devices, licences,
  network access. Name the prerequisite and who confirms it.>

## Code conventions the reviewer enforces

- <language/style rules not covered by formatters>
- <error-handling, logging, dependency policy>

## Anything else non-negotiable

- <...>
