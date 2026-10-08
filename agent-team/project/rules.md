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
  `.claude/settings.json` or the model guard, and Phase 4 onward. Checkpoint
  owner reviews (A0 layout, B `protocol.h`) are reported to the owner.
- **Delegation extended, owner 2026-09-27:** "Duyệt protocol.h bạn hãy bắt đầu
  phase 3, vẫn giao quyền như phase 1-2" — the same delegation covers Phase 3
  (todo T17–T24, through Checkpoint C), with the same exclusions. Checkpoint C
  is the go/no-go on the sans-I/O `Session`: a "go" is reported to the owner;
  a "no-go" (fall back to direction B) is the owner's decision, and Phase 4
  waits for the owner either way.
- **Checkpoint C: go; delegation extended to Phase 4, owner 2026-09-30:**
  "hãy đi hướng go, và giao quyền cho bạn như phase 4, trong phase này bạn có
  thể tự quyết định khi nào thì spawn sub agent developer để đạt hiệu quả
  implement cao nhất." Direction C stays (sans-I/O `Session` in core). The
  same delegation covers Phase 4 (todo T25–T29 plus the phase checkpoint),
  with the same exclusions. In Phase 4 only, the leader decides how many
  developers to spawn and when (e.g. one per group of tasks); tester and
  reviewer stay one fresh agent each for the phase. Phase 5 waits for the owner.
- **Delegation extended to Phase 5, owner 2026-09-30:** "Tiếp tục phase 5,
  vẫn giao quyền cho bạn như phase 4." Same terms as Phase 4 (todo T30–T34
  plus Checkpoint D, leader chooses how many developers and when), same
  exclusions. The Checkpoint D demo (`virtual_plc` ↔ `qt_console_poller`) is
  run by the owner; the leader commits Phase 5 once tester and reviewer pass
  and reports the demo steps. Phase 6 waits for the owner.
- **Delegation extended to Phase 6, owner 2026-10-01:** "Bạn hãy tiếp tục
  implement phase 6, vẫn giao quyền như phase 4-5." Same terms and
  exclusions. **Phase 6 runs as three batches** (owner decision): 6a = T35–T37
  → Checkpoint E1, 6b = T38–T42 → Checkpoint E2, 6c = T43–T46 → Checkpoint E.
  Each batch is treated as a phase for "Phase-batched verification": fresh
  tester and reviewer per batch, one commit per batch after its checkpoint
  passes. The "owner review before the tooling phase" item of Checkpoint E
  stays the owner's. Phase 7 waits for the owner.
- **Checkpoint E approved; delegation extended to Phase 7, owner 2026-10-02:**
  "Duyệt checkpoint E, giao quyền phase 7 như phase 4-6. Cho phép sửa script
  để build song song." Same terms and exclusions as Phases 4–6 (leader chooses
  how many developers and when; fresh tester and reviewer; one commit after
  Checkpoint F's first item passes). The "any `hil` work" exclusion is narrowed
  for Phase 7 only: the capture **tooling** (todo T47–T53, `tools/hil_capture`,
  replay tests, plans, `BENCH.md` generation) built and run against
  `virtual_plc`/`MockPlc` is delegated; anything that talks to a real PLC, any
  capture run on hardware, and Checkpoint F's owner gate (scratch areas, FX3
  manual) stay the owner's. Also delegated: making `scripts/check.ps1` and
  `scripts/check.sh` build in parallel (T-056). Phase 8 waits for the owner.
- **Delegation extended to Phase 8 (GUI tool), owner 2026-10-03:** `SPEC-gui-tool.md` approved;
  "Duyệt spec, giao Phase 8" — same terms and exclusions as Phases 4–7 (leader plans the tasks,
  chooses how many developers and when, fresh tester and reviewer, one commit after Checkpoint H's
  first item passes). A fresh agent per task (rule above). The Checkpoint H demo is the owner's.
  Writing outside the project is still excluded: ADS for MinGW is built into `build/` only.
  Phase 9 (bench and release) waits for the owner.
- **Phase 8 follow-ups delegated, owner 2026-10-04:** "Duyệt spec. Giao quyền như phase 8. Hãy tạo
  thành một task riêng để xử lí lỗi trong bản release." The `SPEC-mock-plc.md` (streams, a1) and
  `SPEC-gui-tool.md` (confirmation, C) amendments are approved; T-075, T-076 and T-077 (Release
  builds) run on Phase 8's terms: leader plans, fresh developer per task, one fresh tester and
  reviewer over the batch, one commit after both pass. A change T-077 needs outside the test code
  and build files (library behaviour, a spec) goes back to the owner. Phase 9 still waits.
- **Qt 5.15 support delegated, owner 2026-10-07:** scope "Toàn bộ, kể cả GUI"; "Follow-up trước Phase 9,
  giao quyền như Phase 8". T-078 and T-079 run on Phase 8's terms (fresh developer per task, one
  batch tester and reviewer, one commit). The leader may amend the Qt minimum in the specs and
  rules to "Qt 5.15 or 6.2+" (owner decision). Changing the vendored qpb stays the owner's choice
  (T-079 step 2). Checking `C:\Qt` for kits was allowed by the owner.
- **Reconnect follow-up delegated, owner 2026-10-08:** after the v0.1.0 field report "3E Reconnect Stall", the
  owner chose the library changes the leader proposed ("Hãy làm bước 1 và giao quyền như các follow-up trước"):
  first-response grace after `linkUp` (no Ethernet resend), TCP graceful close, switchable socket options, a
  late-serving test server and `virtual_plc` option. The leader writes the spec amendments
  (`SPEC-core-session.md`, `SPEC-qt-device.md`; new `SessionConfig` field and config keys, both "Ask first",
  decided by the owner here). T-081 and T-082 run on Phase 8's terms (fresh developer per task, one batch tester
  and reviewer, one commit). Still excluded: any automatic reconnect or Ethernet resend, and a release/tag.
  Extended the same day: "duyệt T-083" — Qt 5.15 MSVC **32-bit** (`C:\Qt\5.15.0\msvc2019`, installed by the
  owner) joins the batch on the same terms; the leader amends the verified-kit note in `SPEC-build-packaging.md`.
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
- **Fresh agent per task when context is heavy** (owner, 2026-10-02: "sub agent
  bạn gọi đã chứa quá nhiều context nên trong lần implement tiếp task tiếp theo
  mình nghĩ bạn nên gọi sub agent mới"): a new task gets a newly spawned
  developer; resuming is only for the current task's rework and re-checks. The
  task file and project docs must carry everything the new agent needs. This
  overrides "kept alive and re-used between tasks" below.
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
- **Parallel multi-toolset builds** (owner, 2026-10-08: "Nếu tester, reviewer cần build test với nhiều bộ tool
  set và compile hãy yêu cầu chạy build song song thay vì chạy build từng cái"): when a tester or reviewer
  builds several toolsets/kits (Qt 6 MSVC, Qt 6 MinGW, Qt 5 x64, Qt 5 x86, Debug/Release, qmake), they start
  the builds **at the same time**, each as its own background process with its own fresh shell environment
  (vsdev per toolset) and its own build tree, splitting the 32 cores (e.g. `--parallel 8` / `jom -j 8` each),
  then collect every result. Not one after another. Limits:
  - each process keeps its own tree; never two processes on one tree; never `build/qtc-*` / `build/Desktop_Qt_*`;
  - `ctest` runs may also overlap, but a timing test that fails while others run is re-run alone before it is
    reported (lessons [T-071], [T-077]); serial-pair tests (`MC_TEST_SERIAL_PAIR`, COM54/COM55) run in one tree
    only, never in two at once;
  - logs go to the agent's `build/_scratch-*` folder, one per build;
  - builds are **incremental** in the existing trees (`build-env.md` rule 2); `scripts/check.ps1` always starts
    its five `build/check-*` folders from scratch with the same names for every kit, so two `check.ps1` runs
    never share one tree: each runs in its own worktree slot (below).
- **Worktree slots `.wt/wt1` … `.wt/wt4`** (owner, 2026-10-08: "hãy tạo nhiều .wt cho trường hợp cần build song
  song và untrack"; `/.wt/` is git-ignored, owner approval): git worktrees that **mirror** the main tree for
  parallel builds. Run `scripts/wt-sync.ps1 [-Name wtN]` from the main tree first: it copies the main tree's
  current files (uncommitted edits and new files included; unchanged files keep their timestamps, so builds
  there stay incremental) and the local `CMakeUserPresets.json` / `mc_local.pri` with ADS paths pointing at the
  main tree's `build/ads-*`. Rules:
  - **nobody edits sources in a slot**; edits happen only in the main tree, then re-sync;
  - one agent per slot at a time; the leader assigns slots in the spawn prompt (e.g. tester wt1–wt2,
    reviewer wt3–wt4); each slot has its own `build/` (first build there is a full build);
  - `git status` inside a slot lists LF/CRLF noise; use `git diff --stat` to see real changes;
  - slots are never committed from, never pushed, and are not deleted by agents.
  The leader writes this into every tester and reviewer spawn prompt; developers may do the same.

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
- **Exception, owner 2026-10-07:** for the Qt 5.15 work (T-078, T-079 and their batch tester and
  reviewer) subagents run on **Opus**. The owner changed `.claude/settings.json` themselves
  (`CLAUDE_CODE_SUBAGENT_MODEL=opus`, FORCE kept; verified by a probe agent: `claude-opus-5-5`).
  Spawns omit `model`. After that batch the owner decides whether to switch back to Sonnet.
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
  | developer | `test-driven-development`, `incremental-implementation`, `source-driven-development` (Qt 5.15 / 6.2 "since" checks), `debugging-and-error-recovery` |
  | tester | `test-driven-development` (the Prove-It pattern for bug reports) |
  | reviewer | `code-review-and-quality`; plus `doubt-driven-development` on `protocol-core` tasks |

- **Experiment, owner 2026-10-08:** developers spawned **after** T-081 in the reconnect follow-up (T-082, T-083
  and any rework developer of the batch) do **not** read or apply skill-pack skills; the spawn prompt says so. The
  owner wants to see whether implementation time and quality improve. T-081's developer (spawned before) used
  them and is the reference. Tester and reviewer keep their skills. The leader records per developer: wall time,
  tokens, tool calls, and the batch tester/reviewer findings per task, and reports the comparison to the owner.
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
- Qt usage compiles on **Qt 5.15 and Qt 6.2+**; the GUI `tools/mc_workbench` on Qt 5.15 or
  Qt 6.5+ (owner decisions 2026-09-27 and 2026-10-07; installed: Qt 5.15.0 `msvc2019_64` and
  Qt 6.11.1): the reviewer checks the "since" version in the Qt docs of every Qt class, function
  or enum a diff starts using.
- Doc comments follow `docs/rules/doc_comment_style.md` (owner decision
  2026-09-27: `/** */` + tags). Mechanically checked on every diff: each
  header under `include/mc/` has a `@file` block; every public symbol has a
  doc comment with explicit `@brief` and directed `@param[in|out|in,out]`;
  every public `core-*` function has `@par Complexity`; no `@throws`; no
  icons or emoji in any comment; no doc comments in `.cpp` files.
- A real PLC disagreeing with the reference spec is a **finding** (log, tag
  `known-divergence`, owner decides) — never a silent library change.
