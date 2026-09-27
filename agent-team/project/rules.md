# Project Rules — mc_protocol

Hard rules for every role. The reviewer checks them mechanically. Where these
contradict `agent-team/core/`, this file wins.

## Version control

- Commit identity: `dev28 <devgbit@gmail.com>`.
- **No AI attribution of any kind** in commits or PRs: no `Co-Authored-By`,
  no "Generated with" lines, no AI trailers.
- Commits happen **only when the owner says so** (Gate 2 approval is the
  trigger, given per task). No standing permission, except the delegation
  below.
- **Delegation, owner 2026-09-27:** for the rest of Phase 0 (T-003–T-005) and
  all of Phase 1 and Phase 2 of `tasks/plan.md` (todo T05–T16, through
  Checkpoint B), the leader holds Gate 1 and Gate 2: it plans, approves,
  orchestrates and commits each task itself once tester and reviewer pass.
  Owner quote: "Bạn được phép tự commit và được phép lên plan và điều phối cho
  cả phase 1 và phase 2 mà không cần approve của mình." Each task file records
  "Gate 1/2: leader, under owner delegation 2026-09-27". Not delegated: any
  `hil` work, anything the specs list under "Ask first" (third-party code,
  raising minimums, `.gitignore`, public layout changes), any change to
  `.claude/settings.json` or the model guard, and Phase 3 onward. Checkpoint
  owner reviews (A0 layout, B `protocol.h`) are reported to the owner; Phase 3
  does not start until the owner has seen Checkpoint B.
- Work on `main` unless the owner says otherwise.

## Phase-batched verification (owner decision 2026-09-27)

Owner quote: "bạn hãy để developer implement hết các task cả một phase, đến
checkpoint của phase mới gọi tester và reviewer chứ không gọi ở từng task
nữa." From T-008 on, this overrides the per-task flow of
`agent-team/core/WORKFLOW.md` and the tester/reviewer columns of
`task-classification.md`:

- **Fresh agents per phase** (owner decision 2026-09-27: "hãy gọi subagent
  developer, tester, reviewer mới ở mỗi phase"): each phase spawns a new
  developer, a new tester and a new reviewer; within the phase each one is
  re-used (resumed) for all its work. No agent carries over into the next
  phase, and no agent ever holds two roles.
- **Within a phase**, one developer implements the tasks one after another
  (kept alive and re-used between tasks). Each task still has its own task
  file, Plan and Dev notes, and must build and pass its own tests.
- **Per task, the leader** checks the developer's evidence by re-running the
  task's verification command itself (build + `ctest`) and marks the task
  `implemented`. **No commit per task** — the work stays in the working tree.
  Test report and Review verdict of the task say "deferred to the checkpoint
  task".
- **At the phase checkpoint task**, the tester verifies every acceptance
  criterion of every task in the phase, and the reviewer reviews the whole
  phase diff (from the last commit, i.e. the working tree against it),
  including the `protocol-core` reference diffs those tasks call for.
  Findings go back to the developer as rework; the checkpoint closes only
  when both pass.
- **One commit per phase** (owner decision 2026-09-27: "thay vì commit theo
  từng task bạn hãy commit theo khi hoàn thành một phase"): after the
  checkpoint passes, the leader commits the whole phase at once (Gate 2 under
  the delegation), fills every task's Commit record with that commit and
  moves all the phase's task files to `tasks/done/`. The commit body lists the
  tasks it contains.
- Tasks already reviewed and committed individually (T-001…T-007) are not
  re-reviewed.

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
