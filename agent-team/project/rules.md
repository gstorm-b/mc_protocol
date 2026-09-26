# Project Rules — mc_protocol

Hard rules for every role. The reviewer checks them mechanically. Where these
contradict `agent-team/core/`, this file wins.

## Version control

- Commit identity: `dev28 <devgbit@gmail.com>`.
- **No AI attribution of any kind** in commits or PRs: no `Co-Authored-By`,
  no "Generated with" lines, no AI trailers.
- Commits happen **only when the owner says so** (Gate 2 approval is the
  trigger, given per task). No standing permission.
- Work on `main` unless the owner says otherwise.

## Access boundaries

- Never read or write **outside `C:\DGB\Project\mc_protocol`** without asking
  the owner first — including "just to check" reads. (Toolchain binaries under
  `C:\Qt\...` and the VS shell are exempt: running builds is fine.)
- Scratch/temporary documents go to `temp-docs/`, nowhere else.

## Subagents

Owner decision 2026-09-27; applies at **every level** — the leader's
subagents, their subagents, and so on down.

- Every subagent runs on **Sonnet**. Enforced by configuration, not by
  discipline:
  - `.claude/settings.json` `env`: `CLAUDE_CODE_SUBAGENT_MODEL=sonnet` with
    `CLAUDE_CODE_SUBAGENT_MODEL_FORCE=1` — overrides the per-call `model`
    parameter, agent frontmatter and inheritance, at every nesting level and
    for Workflow agents.
  - `agent-team/project/hooks/subagent-model-guard.ps1`, wired as a
    PreToolUse hook on `Agent` (fires inside subagents too): blocks the
    `fork` type, which always inherits the parent's model, and any explicit
    model other than `sonnet`.
- Spawns omit `model` or pass `sonnet` — never another model. Do not edit or
  bypass the two files above; changing them is an owner decision.
- The main session (leader) keeps its own model; the rule covers subagents.
- Verified 2026-09-27 after a session restart: `general-purpose` and
  `claude-code-guide` (normally Haiku) both reported Sonnet.

`.claude/` is untracked (owner decision), so on a fresh clone the leader
recreates `.claude/settings.json` with exactly this content, then restarts the
session:

```json
{
  "env": {
    "CLAUDE_CODE_SUBAGENT_MODEL": "sonnet",
    "CLAUDE_CODE_SUBAGENT_MODEL_FORCE": "1"
  },
  "hooks": {
    "PreToolUse": [
      {
        "matcher": "Agent",
        "hooks": [
          {
            "type": "command",
            "command": "powershell -NoProfile -NonInteractive -ExecutionPolicy Bypass -File \"$CLAUDE_PROJECT_DIR/agent-team/project/hooks/subagent-model-guard.ps1\""
          }
        ]
      }
    ]
  }
}
```

## Skill pack

A vendored subset of addyosmani/agent-skills (MIT) lives in
`agent-team/project/skill-pack/` (see its README). Owner decision 2026-09-27.

- Skills are **advisory techniques**. This file, the role contracts and the
  task's approved Plan win over any step in a skill.
- Each role reads its skills' `SKILL.md` before starting and applies them where
  they fit:

  | Role | Skills (`skill-pack/skills/…`) |
  |---|---|
  | developer | `test-driven-development`, `incremental-implementation`, `source-driven-development` (Qt 6.2 "since" checks), `debugging-and-error-recovery` |
  | tester | `test-driven-development` (the Prove-It pattern for bug reports) |
  | reviewer | `code-review-and-quality`; plus `doubt-driven-development` on `protocol-core` tasks |

- Void steps: anything in a skill that commits, pushes, opens or merges a PR,
  installs tools, or asks for one approval covering many tasks.
  `incremental-implementation` step 4 "Commit" becomes "record the slice in
  Dev notes" — no commit.
- Fresh-context reviewers spawned by `doubt-driven-development` are subagents:
  the Subagents rule applies.
- The pack's slash commands in `.claude/commands/` are not used: `/build` and
  `/build auto` commit by themselves (the agent-team workflow replaces them),
  `/constraints` installs npm tooling, `/ship` is a deploy checklist.

## Definition of Done

`agent-team/project/definition-of-done.md` is the standing bar for every task,
on top of its acceptance criteria. Links in the skill pack to
`references/definition-of-done.md` mean this file.

## Hardware / environment gates

- **HIL captures are owner-gated.** Before any capture run (Phase 7+), the
  owner must confirm: scratch areas decided, and the FX3 manual checks done
  (special devices + formats 1/4) — see `docs/spec/SPEC-hil-capture.md`,
  operator procedure. Remind the owner **at capture time**, not before.
- Real-PLC anything (even read-only smoke tests) is proposed at Gate 1 and run
  only with explicit owner approval for that task. Owner decision 2026-09-27:
  nothing touches a real PLC before Phase 8 (T54) — no smoke at Checkpoint D.

## Engineering rules the reviewer enforces

- Every task that adds a source file updates `CMakeLists.txt` **and** the
  matching `.pri` in the same change (guarded by BLD-04).
- Zero-allocation guarantees ship with the code they guard (ALC tests), never
  deferred "to a later task".
- Checkpoints require both compilers (MSVC + MinGW GCC 13.1) green. The daily
  loop of a task is MSVC (`build/cmake-debug`). v1 is verified on Windows only.
- Qt usage stays within the **Qt 6.2 API surface** (owner decision 2026-09-27;
  only 6.11 is installed): the reviewer checks the "since" version in the Qt
  docs of every Qt class, function or enum a diff starts using.
- Doc comments follow `docs/rules/doc_comment_style.md` (owner decision
  2026-09-27: `/** */` + tags). Mechanically checked on every diff: each
  header under `include/mc/` has a `@file` block; every public symbol has a
  doc comment with explicit `@brief` and directed `@param[in|out|in,out]`;
  every public `core-*` function has `@par Complexity`; no `@throws`; no
  icons or emoji in any comment; no doc comments in `.cpp` files.
- A real PLC disagreeing with the reference spec is a **finding** (log, tag
  `known-divergence`, owner decides) — never a silent library change.
