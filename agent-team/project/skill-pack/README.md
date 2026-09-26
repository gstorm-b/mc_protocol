# Skill pack — vendored subset of addyosmani/agent-skills

Source: <https://github.com/addyosmani/agent-skills>, version 0.6.10, MIT
licence (`LICENSE` in this folder). Vendored on 2026-09-27 by owner decision so
that the team's skills are tracked in git while `.claude/` stays untracked.

The skills are **advisory techniques**. `agent-team/project/rules.md` ("Skill
pack"), the role contracts and the task's approved Plan win over any step in
them. Which role reads which skill is decided in `rules.md`, not here.

## Contents

| Path | Used by |
|---|---|
| `skills/test-driven-development/` | developer, tester |
| `skills/incremental-implementation/` | developer |
| `skills/source-driven-development/` | developer |
| `skills/debugging-and-error-recovery/` | developer |
| `skills/code-review-and-quality/` | reviewer |
| `skills/doubt-driven-development/` | reviewer (`protocol-core` tasks) |
| `references/orchestration-patterns.md` | linked from `doubt-driven-development` |

Files are byte-identical to upstream. Do not edit them; project-specific
changes go into `rules.md` as overrides, so an upstream update can be
re-copied without a merge.

## Links that do not resolve here

The skills link to files and skills that were deliberately not vendored:

- `../../references/definition-of-done.md` means
  `agent-team/project/definition-of-done.md` (the project's own version).
- `../../references/testing-patterns.md`, `security-checklist.md` and
  `performance-checklist.md` are JavaScript/web checklists, not applicable to
  this C++ library.
- Skills named in the text but absent here (`git-workflow-and-versioning`,
  `security-and-hardening`, `performance-optimization`,
  `browser-testing-with-devtools`, the `code-reviewer` persona, …) are not part
  of the team's toolset.
