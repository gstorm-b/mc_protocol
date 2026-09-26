# Team Workflow

Four roles: **team leader** (the main session), **developer**, **tester**,
**reviewer** (subagents spawned from `.claude/agents/team-*.md`). One task at a
time flows through them. The single source of truth for a task's state is its
**task file** `tasks/active/T-xxx.md` — never the conversation.

## Ground rules

1. **The task file is the baton.** A role that has not written its section into
   the task file has not finished. An empty section is proof a step was skipped.
2. **Subagents never talk to the human.** A blocked subagent writes its question
   into its section of the task file, sets `Status: blocked`, and returns to the
   leader. Only the leader asks the human.
3. **Two human gates, always:**
   - **Gate 1 — Plan approved.** No implementation before the human approves the
     Plan section. Tasks classified `hil` additionally wait for hardware prep.
   - **Gate 2 — Commit approved.** No commit before the human has seen the diff
     and the completed task file. Commits follow `agent-team/project/rules.md`.
4. **Classification decides the path.** The leader classifies every task per
   `agent-team/project/task-classification.md` and records the class in the task
   file. Skipping tester/reviewer is legal only when the class allows it.
5. **Project layer wins.** Where `agent-team/project/*` contradicts this file,
   the project layer wins. Core files are never edited per project.

## Task lifecycle

```
intake → plan → [GATE 1: human] → develop → test → review → [GATE 2: human]
                                     ↑________↙ FAIL  ↑___↙ FAIL
→ commit → close (move to tasks/done/, distill memory)
```

1. **Intake.** Leader creates `tasks/active/T-xxx.md` from
   `agent-team/core/templates/task.md`. IDs are sequential; `Plan ref` links the
   matching item in the project's own plan, if any.
2. **Plan.** Leader writes the Plan section: goal, approach, files expected to
   change, acceptance criteria, verification commands, classification.
3. **Gate 1.** Human approves (or edits) the plan. Leader records approval in
   the task file.
4. **Develop.** Leader spawns `team-developer` with the task file path. Developer
   implements exactly what the Plan says and fills **Dev notes**.
5. **Test.** (Unless the class skips it.) Leader spawns `team-tester`. Tester
   verifies the acceptance criteria independently and fills **Test report** with
   a PASS/FAIL verdict. FAIL → leader sends it back to develop with the report.
6. **Review.** (Unless the class skips it.) Leader spawns `team-reviewer`.
   Reviewer judges the diff and fills **Review verdict** PASS/FAIL with findings.
   FAIL → back to develop. Reviewers never fix code themselves.
7. **Gate 2.** Human reviews diff + task file. On approval the leader commits per
   project rules, fills **Commit record**.
8. **Close.** Move the file to `tasks/done/`. If the task produced a decision or
   a lesson worth keeping, the leader distills it into `agent-team/memory/`
   (one or two lines, linked to the task ID).

## Rework loops

A FAIL bounce goes **through the leader**, never subagent-to-subagent. The leader
re-spawns the developer with the failing report quoted in the prompt. After the
fix, the flow re-enters at the step that failed (test re-runs before review;
a review FAIL re-enters at develop, then test runs again if the class needs it).
Two consecutive FAILs on the same step → leader stops and consults the human.

## Session recovery

Any new session recovers state by reading, in order:
`CLAUDE.md` → `agent-team/core/roles/team-leader.md` →
`agent-team/project/*.md` → `agent-team/memory/*.md` → `tasks/active/*.md`.
The first empty section in an active task file is where work resumes.
